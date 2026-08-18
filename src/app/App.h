#if !defined(APP_APP_H)
#define APP_APP_H

#include <M5Unified.h>
#include <memory>

#include "app/UsageView.h"
#include "lib/claude_api.h"

class App {
public:
    App(std::shared_ptr<ClaudeApi> api, std::shared_ptr<UsageView> view)
        : _api(std::move(api)), _view(std::move(view)) {}

    void setup();

    void loop();

private:
    /**
     * Fetch the usage and repaint. Schedules the next attempt, backing off
     * on failure so a persistent outage does not hammer the API.
     */
    void refresh();

    std::shared_ptr<ClaudeApi> _api;
    std::shared_ptr<UsageView> _view;

    Usage _usage;
    bool _hasUsage = false;
    uint32_t _nextFetchAt = 0;   // millis()
    uint32_t _backoffSec = 0;    // 0 while the last fetch succeeded
};

#endif // !defined(APP_APP_H)
