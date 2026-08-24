#if !defined(APP_USAGEVIEW_H)
#define APP_USAGEVIEW_H

#include <string>

#include "lib/claude_api.h"

/**
 * Renders the two usage windows as labelled bars on the 320x240 display.
 *
 * Each bar carries a vertical time marker at the point the window has
 * elapsed to, so that usage can be read against the pace of the window.
 */
class UsageView {
public:
    void begin();

    /**
     * Redraw the bars. Call only when something changed, it repaints the
     * whole content area.
     *
     * @param usage values to show
     * @param stale true to dim the values because the last fetch failed
     */
    void drawUsage(const Usage &usage, bool stale);

    /**
     * Redraw the status line at the bottom.
     *
     * @param message text to show, empty to clear
     * @param isError true to draw it in the error colour
     */
    void drawStatus(const std::string &message, bool isError);

    /**
     * Redraw the clock in the header. Cheap enough to call every second.
     */
    void drawClock();

private:
    /**
     * @param y vertical origin of the block
     * @param label text above the bar
     * @param window values to show
     * @param windowSec length of the window, for placing the time marker
     * @param stale true to dim the values because the last fetch failed
     */
    void drawBar(int y, const char *label, const UsageWindow &window, long windowSec,
                 bool stale);

    std::string _lastClock;
};

#endif // !defined(APP_USAGEVIEW_H)
