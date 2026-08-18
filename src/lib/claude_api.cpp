#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>

#include "lib/claude_api.h"
#include "lib/root_ca.h"

namespace {

constexpr char USAGE_URL[] = "https://api.anthropic.com/api/oauth/usage";
constexpr char TOKEN_URL[] = "https://console.anthropic.com/v1/oauth/token";
constexpr char CLIENT_ID[] = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
constexpr char BETA_HEADER[] = "oauth-2025-04-20";

// Both hosts route on User-Agent and they disagree on what they want:
// api.anthropic.com puts unknown agents into an aggressively rate limited
// bucket, while console.anthropic.com answers them with a bogus 404/429.
// These are the strings Claude Code itself sends to each.
constexpr char USAGE_UA[] = "claude-code/2.1.233";
constexpr char TOKEN_UA[] = "anthropic-sdk-typescript/0.70.0 userOAuthProvider";

constexpr char NVS_NAMESPACE[] = "claude";
constexpr char NVS_KEY_ACCESS[] = "access";
constexpr char NVS_KEY_REFRESH[] = "refresh";
// The seed the stored pair was derived from, so that reflashing with freshly
// minted tokens takes effect instead of being shadowed by the stored ones.
constexpr char NVS_KEY_SEED[] = "seed";

constexpr int HTTP_TIMEOUT_MS = 15000;

/**
 * Days since 1970-01-01 for a proleptic Gregorian date. Used instead of
 * timegm(), which newlib does not expose here, and mktime(), which would
 * apply the local timezone.
 */
long daysFromCivil(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const long era = (year >= 0 ? year : year - 399) / 400;
    const long yoe = year - era * 400;                                  // [0, 399]
    const long doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;             // [0, 146096]
    return era * 146097 + doe - 719468;
}

/**
 * Parse an ISO 8601 timestamp such as "2026-08-15T16:19:59.808157+00:00".
 * The API always reports UTC, so the offset is ignored.
 *
 * @return epoch seconds, or 0 when the input is not a timestamp
 */
time_t parseIso8601(const char *s) {
    if (s == nullptr) {
        return 0;
    }
    int year, month, day, hour, minute, second;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute,
               &second) != 6) {
        return 0;
    }
    return daysFromCivil(year, month, day) * 86400L + hour * 3600L + minute * 60L +
           second;
}

/**
 * Read one window ("five_hour" / "seven_day") out of the response.
 */
UsageWindow readWindow(JsonVariantConst v) {
    UsageWindow w;
    if (v.isNull()) {
        return w;
    }
    w.valid = true;
    w.utilization = v["utilization"] | 0.0f;
    w.resetsAt = parseIso8601(v["resets_at"].as<const char *>());
    return w;
}

} // namespace

void ClaudeApi::begin(const char *accessTokenSeed, const char *refreshTokenSeed) {
    const std::string seed = refreshTokenSeed != nullptr ? refreshTokenSeed : "";

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    const std::string storedSeed = prefs.getString(NVS_KEY_SEED, "").c_str();
    const std::string storedRefresh = prefs.getString(NVS_KEY_REFRESH, "").c_str();
    _accessToken = prefs.getString(NVS_KEY_ACCESS, "").c_str();
    prefs.end();

    // The stored pair wins, because the server rotated the compiled-in token
    // away on the first refresh. It is only discarded when config.h carries a
    // seed the stored pair did not come from, i.e. after re-minting.
    if (!storedRefresh.empty() && storedSeed == seed) {
        _refreshToken = storedRefresh;
        return;
    }

    _refreshToken = seed;
    _accessToken = accessTokenSeed != nullptr ? accessTokenSeed : "";

    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_KEY_SEED, seed.c_str());
    prefs.putString(NVS_KEY_ACCESS, _accessToken.c_str());
    prefs.putString(NVS_KEY_REFRESH, _refreshToken.c_str());
    prefs.end();
}

int ClaudeApi::requestUsage(std::string &out) {
    WiFiClientSecure client;
    client.setCACert(ROOT_CA_API_ANTHROPIC);
    client.setTimeout(HTTP_TIMEOUT_MS / 1000);

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(client, USAGE_URL)) {
        _lastError = "begin failed";
        return -1;
    }
    http.setUserAgent(USAGE_UA);
    http.addHeader("Authorization", ("Bearer " + _accessToken).c_str());
    http.addHeader("anthropic-beta", BETA_HEADER);

    const int status = http.GET();
    if (status > 0) {
        out = http.getString().c_str();
    }
    http.end();
    return status;
}

bool ClaudeApi::refreshAccessToken() {
    _tokenExpired = false;
    if (_refreshToken.empty()) {
        _lastError = "no refresh token";
        _tokenExpired = true;
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(ROOT_CA_CONSOLE_ANTHROPIC);
    client.setTimeout(HTTP_TIMEOUT_MS / 1000);

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(client, TOKEN_URL)) {
        _lastError = "refresh begin failed";
        return false;
    }
    http.setUserAgent(TOKEN_UA);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("anthropic-beta", BETA_HEADER);

    JsonDocument req;
    req["grant_type"] = "refresh_token";
    req["refresh_token"] = _refreshToken;
    req["client_id"] = CLIENT_ID;
    std::string payload;
    serializeJson(req, payload);

    const int status = http.POST(reinterpret_cast<uint8_t *>(&payload[0]), payload.size());
    String body;
    if (status > 0) {
        body = http.getString();
    }
    http.end();

    if (status != HTTP_CODE_OK) {
        // The refresh token outlived its deadline, or was superseded. Either
        // way only re-minting helps, so say so instead of showing a status code.
        if (body.indexOf("invalid_grant") >= 0) {
            _lastError = "token expired - re-mint";
            _tokenExpired = true;
        } else {
            _lastError = "refresh HTTP " + std::to_string(status);
        }
        return false;
    }

    JsonDocument res;
    if (deserializeJson(res, body) != DeserializationError::Ok) {
        _lastError = "refresh parse error";
        return false;
    }
    const char *access = res["access_token"];
    if (access == nullptr) {
        _lastError = "no access_token";
        return false;
    }
    _accessToken = access;
    // The server rotates the refresh token, so the previous one is now dead.
    const char *refresh = res["refresh_token"];
    if (refresh != nullptr) {
        _refreshToken = refresh;
    }

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_KEY_ACCESS, _accessToken.c_str());
    prefs.putString(NVS_KEY_REFRESH, _refreshToken.c_str());
    prefs.end();
    return true;
}

bool ClaudeApi::parseUsage(const std::string &body, Usage &out) {
    JsonDocument filter;
    filter["five_hour"]["utilization"] = true;
    filter["five_hour"]["resets_at"] = true;
    filter["seven_day"]["utilization"] = true;
    filter["seven_day"]["resets_at"] = true;

    JsonDocument doc;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) !=
        DeserializationError::Ok) {
        _lastError = "parse error";
        return false;
    }

    out.session = readWindow(doc["five_hour"]);
    out.weekly = readWindow(doc["seven_day"]);
    if (!out.session.valid && !out.weekly.valid) {
        _lastError = "no usage in response";
        return false;
    }
    out.fetchedAt = time(nullptr);
    return true;
}

bool ClaudeApi::fetchUsage(Usage &out) {
    _lastError.clear();

    std::string body;
    int status = requestUsage(body);

    // An expired access token is the expected steady state: refresh once and
    // retry before treating it as an error.
    if (status == HTTP_CODE_UNAUTHORIZED || _accessToken.empty()) {
        if (!refreshAccessToken()) {
            return false;
        }
        status = requestUsage(body);
    }

    if (status != HTTP_CODE_OK) {
        _lastError = status > 0 ? "HTTP " + std::to_string(status)
                                : "network error " + std::to_string(status);
        return false;
    }
    return parseUsage(body, out);
}
