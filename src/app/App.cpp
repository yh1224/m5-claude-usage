#include "config.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <sys/time.h>

#include "app/App.h"

#include "lib/network.h"

namespace {

// The usage endpoint tolerates polling at this rate; anything faster only
// risks the rate limited bucket without showing fresher numbers.
constexpr uint32_t FETCH_INTERVAL_SEC = CLAUDE_FETCH_INTERVAL_SEC;
constexpr uint32_t BACKOFF_MIN_SEC = 30;
constexpr uint32_t BACKOFF_MAX_SEC = 600;

// How far past a boundary a fetch is aimed. millis() and the wall clock drift
// apart slightly, and a fetch that fired just short of the minute would be
// stamped with the minute before.
constexpr uint32_t FETCH_MARGIN_MS = 1000;
// Closest a scheduled fetch may follow the one before it.
constexpr uint32_t FETCH_MIN_GAP_MS = 10000;

/**
 * Milliseconds until just past the next multiple of the fetch interval in UTC,
 * so that fetches land on the minute (for a 60 s interval) rather than
 * drifting with whenever the device booted.
 */
uint32_t msUntilNextFetch() {
    constexpr uint64_t intervalMs = FETCH_INTERVAL_SEC * 1000ULL;
    // Without NTP the boundaries mean nothing.
    if (!clockSynced()) {
        return intervalMs;
    }
    struct timeval tv{};
    gettimeofday(&tv, nullptr);
    const uint64_t nowMs = static_cast<uint64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    uint64_t waitMs = intervalMs - nowMs % intervalMs + FETCH_MARGIN_MS;
    // A manual refresh or a retry close to a boundary would otherwise be
    // followed by another fetch almost at once, so skip to the one after.
    if (waitMs < FETCH_MIN_GAP_MS) {
        waitMs += intervalMs;
    }
    return waitMs;
}

} // namespace

[[noreturn]] void halt()
{
    while (true) { delay(1000); }
}

void App::setup()
{
    auto cfg = m5::M5Unified::config();
    cfg.external_spk = true;
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);

    M5.Display.setTextSize(2);
    M5.Display.setCursor(0, 0);

    // Connect
    if (!connectNetwork(WIFI_SSID, WIFI_PASSPHRASE)) {
        Serial.println("ERROR: Failed to connect network. Rebooting...");
        delay(5000);
        ESP.restart();
        halt();
    }

    // Synchronize time. Required before the first TLS handshake, since
    // certificate validity is checked against the clock.
    Serial.printf("Synchronizing time: %s (%s)\n", NTP_SERVER, TIMEZONE);
    syncTime(TIMEZONE, NTP_SERVER);

    _api->begin(CLAUDE_OAUTH_ACCESS_TOKEN, CLAUDE_OAUTH_REFRESH_TOKEN);

    _view->begin();
    _view->drawUsage(_usage, true);
    _view->drawStatus("Loading...", false);
    refresh();
}

void App::refresh()
{
    Usage usage;
    if (_api->fetchUsage(usage)) {
        _usage = usage;
        _hasUsage = true;
        _backoffSec = 0;

        char stamp[16];
        struct tm now{};
        if (clockSynced() && getLocalTime(&now, 0)) {
            strftime(stamp, sizeof(stamp), "%H:%M", &now);
        } else {
            snprintf(stamp, sizeof(stamp), "--:--");
        }
        Serial.printf("Usage: session=%.0f%% weekly=%.0f%%\n",
                      _usage.session.utilization, _usage.weekly.utilization);
        _view->drawUsage(_usage, false);
        _view->drawUpdated(stamp);
        _view->drawStatus("", false);
        _nextFetchAt = millis() + msUntilNextFetch();
        return;
    }

    // Keep the previous numbers on screen but dimmed, so a failed refresh
    // never looks like fresh data.
    if (_hasUsage) {
        _view->drawUsage(_usage, true);
    }

    // An expired refresh token cannot be retried out of, so stop suggesting a
    // countdown and just poll slowly in case new tokens get flashed.
    if (_api->tokenExpired()) {
        _backoffSec = BACKOFF_MAX_SEC;
        Serial.printf("ERROR: %s\n", _api->lastError());
        _view->drawStatus(_api->lastError(), true);
        _nextFetchAt = millis() + _backoffSec * 1000;
        return;
    }

    _backoffSec = _backoffSec == 0 ? BACKOFF_MIN_SEC
                                   : std::min(_backoffSec * 2, BACKOFF_MAX_SEC);
    Serial.printf("ERROR: %s (retry in %us)\n", _api->lastError(), _backoffSec);
    _view->drawStatus(std::string(_api->lastError()) + " - retry in " +
                          std::to_string(_backoffSec) + "s",
                      true);
    _nextFetchAt = millis() + _backoffSec * 1000;
}

void App::loop()
{
    M5.update();

    // Touch button A forces an immediate refresh.
    if (M5.BtnA.wasPressed()) {
        _view->drawStatus("Refreshing...", false);
        refresh();
    }

    // Touch button C switches between the bars and the countdown.
    if (M5.BtnC.wasPressed()) {
        _view->toggleView();
    }

    if (static_cast<int32_t>(millis() - _nextFetchAt) >= 0) {
        refresh();
    }

    _view->tick();
    delay(50);
}
