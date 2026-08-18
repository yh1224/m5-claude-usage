# M5 Claude Usage

Shows Claude subscription usage on an M5Stack Core2/CoreS3: the session (5h) and weekly (7d) windows as bars with percentages, refreshed every few minutes.

The numbers come from `GET https://api.anthropic.com/api/oauth/usage`, the undocumented endpoint behind the `/usage` command of Claude Code.

## Setup

 1. Create the config from the template:

    ```
     cp src/config.h.template src/config.h
    ```

    Fill in `WIFI_SSID` and `WIFI_PASSPHRASE`.

 2. Mint an OAuth token pair for the device:

    ```
    python3 tools/mint_token.py            # prints an authorize URL
    python3 tools/mint_token.py '<code>'   # exchange the code shown after approval
    ```

    This runs the PKCE flow against the Claude Code public client, verifies the result against the usage endpoint, and writes both `#define` lines into `src/config.h` itself. The tokens are never printed, so they stay out of your shell history.

 3. Build and flash:

    ```
    pio run -e <env> -t upload
    ```

## Token handling

Access tokens live 8 hours. The firmware refreshes them on a `401` and stores the result in NVS, because the server rotates the refresh token on every refresh — the value in `config.h` is only a seed for the first boot. Re-minting and reflashing is detected (the seed is stored alongside), so the new tokens take effect without erasing NVS.

Refresh tokens carry their own deadline, reported as `refresh_token_expires_in` and observed at about 25 days. Refreshing does not extend it: the deadline is fixed when the authorization is granted, so the device stops working once it passes and needs re-minting and reflashing roughly monthly. The firmware recognises the resulting `invalid_grant` and shows `token expired - re-mint` rather than a status code.

## Notes

- `tools/mint_token.py --refresh` rotates the pair in `config.h` as a smoke test of the same flow the firmware performs.
- Polling is 180 s by default (`CLAUDE_FETCH_INTERVAL_SEC`); failures back off from 30 s to 10 min and the last good values stay on screen, dimmed.
- Touch button A forces an immediate refresh.
- `src/config.h` holds live credentials and is gitignored.
