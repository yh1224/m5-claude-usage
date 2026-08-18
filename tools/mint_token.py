#!/usr/bin/env python3
"""Mint a dedicated OAuth token pair for the M5Stack usage display.

Runs the Claude Code PKCE flow with the public client id and writes the
resulting pair straight into src/config.h, which is the only place the
tokens live outside the device's NVS.

Usage:
    python3 tools/mint_token.py            # step 1: print the authorize URL
    python3 tools/mint_token.py <code>     # step 2: exchange the pasted code
    python3 tools/mint_token.py --refresh  # rotate the stored pair (smoke test)

Whether the resulting grant is independent of the Claude Code login on the
same account is unverified; see the token handling section of README.md.
"""

import base64
import hashlib
import json
import os
import re
import secrets
import sys
import urllib.parse
import urllib.request

CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e"
AUTHORIZE_URL = "https://claude.ai/oauth/authorize"
TOKEN_URL = "https://console.anthropic.com/v1/oauth/token"
REDIRECT_URI = "https://console.anthropic.com/oauth/code/callback"
# user:profile is what GET /api/oauth/usage requires.
SCOPES = "org:create_api_key user:profile user:inference"
TOKEN_UA = "anthropic-sdk-typescript/0.70.0 userOAuthProvider"
USAGE_UA = "claude-code/2.1.233"


def b64url(raw: bytes) -> str:
    return base64.urlsafe_b64encode(raw).decode().rstrip("=")


def post_json(url: str, payload: dict) -> dict:
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode(),
        headers={
            "Content-Type": "application/json",
            # The token endpoint routes on User-Agent: anything else gets a
            # bogus 404/429 from the edge instead of reaching the handler.
            # This is the string the Claude Code SDK sends when refreshing.
            "User-Agent": TOKEN_UA,
            "anthropic-beta": "oauth-2025-04-20",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req) as res:
            return json.load(res)
    except urllib.error.HTTPError as e:
        body = e.read().decode(errors="replace")
        sys.exit(f"ERROR: {url} -> HTTP {e.code}\n{body}")


STATE_FILE = "/tmp/m5_pkce_verifier.txt"


def step1() -> None:
    verifier = b64url(secrets.token_bytes(32))
    challenge = b64url(hashlib.sha256(verifier.encode()).digest())
    with open(STATE_FILE, "w") as f:
        os.chmod(STATE_FILE, 0o600)
        f.write(verifier)

    params = {
        "code": "true",
        "client_id": CLIENT_ID,
        "response_type": "code",
        "redirect_uri": REDIRECT_URI,
        "scope": SCOPES,
        "code_challenge": challenge,
        "code_challenge_method": "S256",
        "state": verifier,
    }
    print("Open this URL in your browser and approve:\n")
    print(f"{AUTHORIZE_URL}?{urllib.parse.urlencode(params)}")
    print("\nThen re-run:  python3 tools/mint_token.py '<pasted code>'")


CONFIG_FILE = os.path.normpath(
    os.path.join(os.path.dirname(__file__), os.pardir, "src", "config.h")
)
ACCESS_KEY = "CLAUDE_OAUTH_ACCESS_TOKEN"
REFRESH_KEY = "CLAUDE_OAUTH_REFRESH_TOKEN"


def read_config() -> str:
    try:
        with open(CONFIG_FILE) as f:
            return f.read()
    except FileNotFoundError:
        sys.exit(f"{CONFIG_FILE} missing - copy it from src/config.h.template first")


def read_token(key: str) -> str:
    m = re.search(rf'#define {key} "([^"]*)"', read_config())
    return m.group(1) if m else ""


def write_tokens(access: str, refresh: str) -> None:
    """Replace both token values in config.h, leaving the rest of it alone.

    The tokens never go to stdout: they would end up in the shell history or
    the transcript of whatever ran this, and config.h is where they belong.
    """
    text = read_config()
    for key, value in ((ACCESS_KEY, access), (REFRESH_KEY, refresh)):
        text, count = re.subn(
            rf'(#define {key} )"[^"]*"',
            # A function, so that characters special to re are copied verbatim.
            lambda m, v=value: m.group(1) + json.dumps(v),
            text,
        )
        if count != 1:
            sys.exit(f'expected exactly one `#define {key} "..."` in {CONFIG_FILE}')
    with open(CONFIG_FILE, "w") as f:
        f.write(text)
    print(f"\nWrote both tokens to {CONFIG_FILE}. Reflash to apply.")


def check_usage(access: str) -> None:
    req = urllib.request.Request(
        "https://api.anthropic.com/api/oauth/usage",
        headers={
            "Authorization": f"Bearer {access}",
            "anthropic-beta": "oauth-2025-04-20",
            "User-Agent": USAGE_UA,
        },
    )
    try:
        with urllib.request.urlopen(req) as res:
            usage = json.load(res)
        print(
            "usage check: OK  five_hour=%s%%  seven_day=%s%%"
            % (
                usage.get("five_hour", {}).get("utilization"),
                usage.get("seven_day", {}).get("utilization"),
            )
        )
    except urllib.error.HTTPError as e:
        print(f"usage check: FAILED HTTP {e.code} {e.read().decode(errors='replace')[:300]}")


def refresh_stored() -> None:
    """Exercise the same refresh the firmware performs, and store the result."""
    old = read_token(REFRESH_KEY)
    if not old:
        sys.exit(f"no {REFRESH_KEY} in {CONFIG_FILE}")

    token = post_json(
        TOKEN_URL,
        {
            "grant_type": "refresh_token",
            "refresh_token": old,
            "client_id": CLIENT_ID,
        },
    )
    access = token.get("access_token", "")
    new = token.get("refresh_token", "")
    print("--- refresh ok ---")
    print("scope:      ", token.get("scope"))
    print("expires_in: ", token.get("expires_in"), "sec")
    rexp = token.get("refresh_token_expires_in")
    if rexp is not None:
        print("refresh_token_expires_in:", rexp, "sec =", round(int(rexp) / 86400, 1), "days")
    print("rotated:    ", "yes (refresh_token changed)" if new != old else "no (same token reusable)")
    check_usage(access)
    write_tokens(access, new or old)


def main() -> None:
    if len(sys.argv) < 2:
        step1()
        return
    if sys.argv[1] == "--refresh":
        refresh_stored()
        return

    try:
        with open(STATE_FILE) as f:
            verifier = f.read().strip()
    except FileNotFoundError:
        sys.exit(f"{STATE_FILE} missing - run step 1 first")

    code, _, state = sys.argv[1].strip().partition("#")
    token = post_json(
        TOKEN_URL,
        {
            "grant_type": "authorization_code",
            "code": code,
            "state": state or verifier,
            "client_id": CLIENT_ID,
            "redirect_uri": REDIRECT_URI,
            "code_verifier": verifier,
        },
    )

    access = token.get("access_token", "")
    refresh = token.get("refresh_token", "")
    if not refresh:
        sys.exit(f"no refresh_token in response: {json.dumps(token)[:400]}")

    print("\n--- token acquired ---")
    print("scope:      ", token.get("scope"))
    print("expires_in: ", token.get("expires_in"), "sec")
    check_usage(access)
    write_tokens(access, refresh)


if __name__ == "__main__":
    main()
