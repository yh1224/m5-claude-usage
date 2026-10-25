#if !defined(APP_USAGEVIEW_H)
#define APP_USAGEVIEW_H

#include <ctime>
#include <string>

#include "lib/claude_api.h"

/**
 * Renders the two usage windows on the 320x240 display.
 *
 * Normally both windows are shown as labelled bars, each carrying a vertical
 * time marker at the point the window has elapsed to, so that usage can be
 * read against the pace of the window. Once a window hits its limit there is
 * nothing left to pace, so the view switches to counting down to the reset.
 * toggleView() overrides that choice either way.
 */
class UsageView {
public:
    void begin();

    /**
     * Redraw the content area. Call only when something changed, it repaints
     * the whole area.
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
     * Switch to the view the automatic choice is not showing: the countdown
     * while under the limits, the bars while at them. The override is dropped
     * again as soon as a limit is reached or cleared, so it never outlives the
     * state it was made in.
     */
    void toggleView();

    /**
     * Redraw the time of the last successful fetch at the top right.
     *
     * @param text time to show, e.g. "12:34"
     */
    void drawUpdated(const std::string &text);

    /**
     * Redraw whatever follows the local clock: the countdown while it is the
     * view up. Cheap enough to call every loop, it repaints only once its
     * displayed value changes.
     */
    void tick();

private:
    /**
     * Repaint the content area with the view that is due, clearing it first
     * when that is not the view already up.
     */
    void draw();

    /**
     * @param y vertical origin of the block
     * @param label text above the bar
     * @param window values to show
     * @param windowSec length of the window, for placing the time marker
     * @param stale true to dim the values because the last fetch failed
     */
    void drawBar(int y, const char *label, const UsageWindow &window, long windowSec,
                 bool stale);

    /**
     * Redraw the countdown to the reset of the window it is showing. Does
     * nothing while the bars are up.
     *
     * @param force true to repaint even when the remaining minutes are
     *              unchanged, needed after the content area was cleared
     */
    void drawCountdown(bool force);

    // Last values drawn, kept so that toggleView() can repaint without them.
    Usage _usage;
    bool _stale = true;

    bool _limited = false;  // a window is at its limit, i.e. countdown by default
    bool _flipped = false;  // show the other view than the automatic one

    // Countdown state, valid while _countdownShown.
    bool _countdownShown = false;
    std::string _countdownLabel;
    std::string _countdownTitle;
    time_t _countdownResetsAt = 0;
    long _countdownWindowSec = 0;
    bool _countdownStale = false;
    long _lastRemainMin = -1;
};

#endif // !defined(APP_USAGEVIEW_H)
