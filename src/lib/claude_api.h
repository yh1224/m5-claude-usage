#if !defined(LIB_CLAUDE_API_H)
#define LIB_CLAUDE_API_H

#include <ctime>
#include <string>

/**
 * One rate limit window as reported by GET /api/oauth/usage.
 */
struct UsageWindow {
    bool valid = false;
    float utilization = 0.0f;  // percent consumed, 0-100
    time_t resetsAt = 0;       // epoch seconds, 0 when the API reports null
};

struct Usage {
    UsageWindow session;  // five_hour
    UsageWindow weekly;   // seven_day
    time_t fetchedAt = 0;
};

/**
 * Reads Claude subscription usage through the OAuth endpoint that backs
 * the /usage command of Claude Code.
 *
 * Access tokens live 8 hours, so the client refreshes them on demand. The
 * refresh token is rotated by the server on every refresh, hence the new
 * one is persisted to NVS and preferred over the compiled-in seed.
 */
class ClaudeApi {
public:
    /**
     * Load the persisted token pair, or adopt the compiled-in seed when
     * nothing is stored yet or the seed differs from the one the stored pair
     * came from (i.e. after re-minting and reflashing).
     *
     * @param accessTokenSeed access token from config.h (may be empty)
     * @param refreshTokenSeed refresh token from config.h
     */
    void begin(const char *accessTokenSeed, const char *refreshTokenSeed);

    /**
     * Fetch the current usage, refreshing the access token once if it expired.
     *
     * @param out receives the usage on success
     * @return true: success, false: failure (see lastError())
     */
    bool fetchUsage(Usage &out);

    const char *lastError() const { return _lastError.c_str(); }

    /**
     * True once the server rejected the refresh token itself. Refresh tokens
     * carry an absolute deadline of roughly 25 days that refreshing does not
     * extend, so this state is permanent until new tokens are minted and
     * flashed - retrying cannot clear it.
     */
    bool tokenExpired() const { return _tokenExpired; }

private:
    /**
     * @param out receives the raw response body
     * @return HTTP status code, or a negative HTTPClient error code
     */
    int requestUsage(std::string &out);

    /**
     * Exchange the refresh token for a new access token and persist both.
     *
     * @return true: success, false: failure
     */
    bool refreshAccessToken();

    bool parseUsage(const std::string &body, Usage &out);

    std::string _accessToken;
    std::string _refreshToken;
    std::string _lastError;
    bool _tokenExpired = false;
};

#endif // !defined(LIB_CLAUDE_API_H)
