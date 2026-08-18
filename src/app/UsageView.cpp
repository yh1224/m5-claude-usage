#include <M5Unified.h>
#include <cstdio>

#include "app/UsageView.h"

namespace {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;

constexpr int HEADER_H = 34;
constexpr int STATUS_Y = SCREEN_H - 22;

constexpr int BAR_X = 16;
constexpr int BAR_W = SCREEN_W - BAR_X * 2;
constexpr int BAR_H = 26;

// Vertical origin of each block (label row; the bar sits below it).
constexpr int BLOCK1_Y = HEADER_H + 12;
constexpr int BLOCK2_Y = BLOCK1_Y + 84;
constexpr int BLOCK_H = 78;
// The percentage is set slightly above the origin, so the block has to be
// cleared from above it: drawString only paints the background behind the
// glyphs themselves, and a narrower value would leave the old one showing.
constexpr int BLOCK_PAD_TOP = 6;

// Right aligned text in a proportional font shrinks leftwards, so the clock
// needs its own area cleared for the same reason.
constexpr int CLOCK_Y = 8;
constexpr int CLOCK_H = 22;
constexpr int CLOCK_W = 70;

constexpr int COLOR_BG = 0x0000;         // black
constexpr int COLOR_FG = 0xFFFF;         // white
constexpr int COLOR_DIM = 0x8410;        // grey
constexpr int COLOR_TRACK = 0x2124;      // dark grey
constexpr int COLOR_ACCENT = 0xFD00;     // Claude orange
constexpr int COLOR_OK = 0x07E0;         // green
constexpr int COLOR_WARN = 0xFFE0;       // yellow
constexpr int COLOR_HIGH = 0xFC00;       // orange
constexpr int COLOR_CRIT = 0xF800;       // red
constexpr int COLOR_ERROR = 0xF800;

/**
 * Bar colour by how much of the window is consumed.
 */
int barColor(float percent) {
    if (percent >= 90.0f) {
        return COLOR_CRIT;
    }
    if (percent >= 75.0f) {
        return COLOR_HIGH;
    }
    if (percent >= 50.0f) {
        return COLOR_WARN;
    }
    return COLOR_OK;
}

/**
 * Format the time left until a window resets, e.g. "resets in 1h58m".
 *
 * @param resetsAt epoch seconds, 0 when unknown
 * @param out receives the text
 * @param size size of out
 */
void formatReset(time_t resetsAt, char *out, size_t size) {
    if (resetsAt == 0) {
        snprintf(out, size, "reset time unknown");
        return;
    }
    long remain = static_cast<long>(resetsAt - time(nullptr));
    if (remain <= 0) {
        snprintf(out, size, "resetting now");
        return;
    }
    const long days = remain / 86400;
    const long hours = (remain % 86400) / 3600;
    const long minutes = (remain % 3600) / 60;
    if (days > 0) {
        snprintf(out, size, "resets in %ldd %ldh", days, hours);
    } else if (hours > 0) {
        snprintf(out, size, "resets in %ldh %02ldm", hours, minutes);
    } else {
        snprintf(out, size, "resets in %ldm", minutes);
    }
}

} // namespace

void UsageView::begin() {
    M5.Display.setRotation(1);
    M5.Display.fillScreen(COLOR_BG);

    M5.Display.setTextColor(COLOR_ACCENT, COLOR_BG);
    M5.Display.setTextSize(1);
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.setCursor(BAR_X, 8);
    M5.Display.print("Claude Usage");

    M5.Display.drawFastHLine(0, HEADER_H - 2, SCREEN_W, COLOR_TRACK);
    _lastClock.clear();
}

void UsageView::drawClock() {
    struct tm now{};
    if (!getLocalTime(&now, 0)) {
        return;
    }
    char text[6];
    strftime(text, sizeof(text), "%H:%M", &now);
    if (_lastClock == text) {
        return;
    }
    _lastClock = text;

    M5.Display.fillRect(SCREEN_W - BAR_X - CLOCK_W, CLOCK_Y, CLOCK_W, CLOCK_H, COLOR_BG);
    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.setTextColor(COLOR_DIM, COLOR_BG);
    M5.Display.setTextDatum(top_right);
    M5.Display.drawString(text, SCREEN_W - BAR_X, 12);
    M5.Display.setTextDatum(top_left);
}

void UsageView::drawBar(int y, const char *label, const UsageWindow &window, bool stale) {
    M5.Display.fillRect(0, y - BLOCK_PAD_TOP, SCREEN_W, BLOCK_H + BLOCK_PAD_TOP, COLOR_BG);

    const float percent = window.valid ? window.utilization : 0.0f;
    const int fill = window.valid
                         ? static_cast<int>(BAR_W * std::min(percent, 100.0f) / 100.0f + 0.5f)
                         : 0;
    const int color = stale || !window.valid ? COLOR_DIM : barColor(percent);

    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.setTextColor(stale ? COLOR_DIM : COLOR_FG, COLOR_BG);
    M5.Display.setTextDatum(top_left);
    M5.Display.drawString(label, BAR_X, y);

    char value[8];
    snprintf(value, sizeof(value), window.valid ? "%.0f%%" : "--", percent);
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.setTextColor(color, COLOR_BG);
    M5.Display.setTextDatum(top_right);
    M5.Display.drawString(value, BAR_X + BAR_W, y - 4);
    M5.Display.setTextDatum(top_left);

    const int barY = y + 24;
    M5.Display.fillRoundRect(BAR_X, barY, BAR_W, BAR_H, 4, COLOR_TRACK);
    if (fill > 0) {
        M5.Display.fillRoundRect(BAR_X, barY, std::max(fill, 8), BAR_H, 4, color);
    }
    M5.Display.drawRoundRect(BAR_X, barY, BAR_W, BAR_H, 4, COLOR_TRACK);

    char reset[32];
    formatReset(window.resetsAt, reset, sizeof(reset));
    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.setTextColor(COLOR_DIM, COLOR_BG);
    M5.Display.drawString(window.valid ? reset : "no data", BAR_X, barY + BAR_H + 4);
}

void UsageView::drawUsage(const Usage &usage, bool stale) {
    drawBar(BLOCK1_Y, "Session (5h)", usage.session, stale);
    drawBar(BLOCK2_Y, "Weekly (7d)", usage.weekly, stale);
}

void UsageView::drawStatus(const std::string &message, bool isError) {
    M5.Display.fillRect(0, STATUS_Y, SCREEN_W, SCREEN_H - STATUS_Y, COLOR_BG);
    M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5.Display.setTextColor(isError ? COLOR_ERROR : COLOR_DIM, COLOR_BG);
    M5.Display.setTextDatum(top_left);
    M5.Display.drawString(message.c_str(), BAR_X, STATUS_Y);
}
