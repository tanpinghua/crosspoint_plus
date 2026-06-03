#include "ClockSubApps.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <time.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ClockPrimitives.h"
#include "fontIds.h"

namespace clocksub {

using namespace clockgfx;

namespace {

// Format a duration in seconds as "H:MM:SS" (>= 1h) or "MM:SS".
void formatHMS(unsigned long totalSec, char* buf, size_t bufSize) {
  const unsigned long h = totalSec / 3600;
  const unsigned long m = (totalSec % 3600) / 60;
  const unsigned long s = totalSec % 60;
  if (h > 0) {
    snprintf(buf, bufSize, "%lu:%02lu:%02lu", h, m, s);
  } else {
    snprintf(buf, bufSize, "%02lu:%02lu", m, s);
  }
}

// Choose a per-pixel cell size so the 8x14 dot string `s` fits the area width/height.
int bigTimePx(const char* s, int areaW, int areaH, int sideMargin) {
  int cells = 0;
  for (const char* p = s; *p; ++p) {
    if (p != s) cells += 1;  // inter-char gap
    cells += (*p == ':') ? 3 : 8;
  }
  if (cells < 1) cells = 1;
  int px = std::min((areaW - 2 * sideMargin) / cells, areaH / 14);
  if (px < 3) px = 3;
  return px;
}

// Build "HH:MM" (24h) or "h:MM" (12h) for an hour/minute pair.
void formatClock(char* buf, size_t bufSize, int hour24, int minute, bool use24h) {
  if (use24h) {
    snprintf(buf, bufSize, "%02d:%02d", hour24, minute);
  } else {
    const int h12 = (hour24 % 12 == 0) ? 12 : hour24 % 12;
    snprintf(buf, bufSize, "%d:%02d", h12, minute);
  }
}

// Shared big-time sizing so the Stopwatch, Timer, and World Clock local time all render at the
// same numeral size (width-bound in portrait, so they match).
int stdBigPx(const char* s, int w, int areaH) { return bigTimePx(s, w, areaH * 36 / 100, 70); }

// Format a duration as plain "MM:SS" where minutes are NOT rolled into hours (e.g. 99:00).
void formatMMSS(unsigned long totalSec, char* buf, size_t bufSize) {
  snprintf(buf, bufSize, "%02lu:%02lu", totalSec / 60, totalSec % 60);
}

}  // namespace

// ===========================================================================
// Shared
// ===========================================================================

bool needsTime(uint8_t subApp) { return subApp == CLOCK || subApp == WORLD_CLOCK; }

const char* subAppName(uint8_t subApp) {
  switch (subApp) {
    case WORLD_CLOCK:
      return tr(STR_WORLD_CLOCK);
    case STOPWATCH:
      return tr(STR_STOPWATCH);
    case TIMER:
      return tr(STR_TIMER);
    case CLOCK:
    default:
      return tr(STR_CLOCK);
  }
}

void drawSubAppBar(const GfxRenderer& r, int w, int h, uint8_t current) {
  const int y0 = h - BAR_HEIGHT;
  r.drawLine(0, y0, w, y0, 1, true);

  // Active sub-app name.
  r.drawCenteredText(SMALL_FONT_ID, y0 + 3, subAppName(current), true, EpdFontFamily::BOLD);

  // Dot row: active dot enlarged.
  const int spacing = 18;
  const int totalW = (SUBAPP_COUNT - 1) * spacing;
  const int startX = w / 2 - totalW / 2;
  const int dotY = h - 9;
  for (int i = 0; i < SUBAPP_COUNT; ++i) {
    const int dx = startX + i * spacing;
    drawFilledCircle(r, dx, dotY, (i == current) ? 4 : 2, true);
  }
}

// ===========================================================================
// World clock
// ===========================================================================

const WorldCity WORLD_CITIES[WORLD_CITY_COUNT] = {
    {"Los Angeles", -480}, {"New York", -300}, {"Sao Paulo", -180}, {"London", 0},
    {"Paris", 60},         {"Cairo", 120},     {"Dubai", 240},      {"Mumbai", 330},
    {"Singapore", 480},    {"Tokyo", 540},     {"Sydney", 600},     {"Auckland", 720},
};

// Format a single city's time cell ("HH:MM", plus AM/PM in 12h, plus a +1/-1 day marker).
static void formatCityTime(char* buf, size_t bufSize, const struct tm& cityTm, bool use24h, int dayDelta) {
  char timePart[8];
  formatClock(timePart, sizeof(timePart), cityTm.tm_hour, cityTm.tm_min, use24h);
  const char* ampm = use24h ? "" : ((cityTm.tm_hour < 12) ? " AM" : " PM");
  if (dayDelta != 0) {
    snprintf(buf, bufSize, "%s%s %+d", timePart, ampm, dayDelta);
  } else {
    snprintf(buf, bufSize, "%s%s", timePart, ampm);
  }
}

void drawWorldClock(const GfxRenderer& r, int w, int h, bool use24h, const struct tm& localTm, time_t utcNow,
                    int localOffsetMinutes, int selectedCity) {
  const int areaH = h - BAR_HEIGHT;
  const int margin = 16;

  // --- Local (configured timezone) time, large, top band ---
  const int labelLh = r.getLineHeight(SMALL_FONT_ID);
  r.drawCenteredText(SMALL_FONT_ID, 4, tr(STR_LOCAL), true, EpdFontFamily::BOLD);

  char localBuf[8];
  formatClock(localBuf, sizeof(localBuf), localTm.tm_hour, localTm.tm_min, use24h);
  const int localTop = 4 + labelLh + 4;
  const int px = stdBigPx(localBuf, w, areaH);
  const int blockW = drawBigTimeText(r, w / 2, localTop, px, localBuf);
  if (!use24h) {
    // AM/PM at the bottom-right of the big clock.
    const char* ampm = (localTm.tm_hour < 12) ? "AM" : "PM";
    const int alh = r.getLineHeight(UI_12_FONT_ID);
    r.drawText(UI_12_FONT_ID, w / 2 + blockW / 2 + 4, localTop + 14 * px - alh, ampm, true, EpdFontFamily::BOLD);
  }

  // --- City list: name (left column) | time (right column), selected row highlighted ---
  const int listTop = localTop + 14 * px + 8;
  r.drawLine(margin, listTop - 4, w - margin, listTop - 4, 1, true);
  const int listBottom = areaH - 4;
  const int rowH = (listBottom - listTop) / WORLD_CITY_COUNT;
  const int font = SMALL_FONT_ID;
  const int flh = r.getLineHeight(font);

  for (int i = 0; i < WORLD_CITY_COUNT; ++i) {
    const WorldCity& c = WORLD_CITIES[i];
    const int rowTop = listTop + i * rowH;
    const int textY = rowTop + (rowH - flh) / 2;
    const bool sel = (i == selectedCity);

    struct tm cityTm;
    const time_t cityEpoch = utcNow + static_cast<time_t>(c.offsetMinutes) * 60;
    gmtime_r(&cityEpoch, &cityTm);
    const int dayDelta = static_cast<int>((cityEpoch / 86400) -
                                          ((utcNow + static_cast<time_t>(localOffsetMinutes) * 60) / 86400));
    char timeBuf[16];
    formatCityTime(timeBuf, sizeof(timeBuf), cityTm, use24h, dayDelta);

    if (sel) r.fillRect(margin, rowTop, w - 2 * margin, rowH, true);
    const bool black = !sel;  // white text on the highlighted (black) row
    r.drawText(font, margin + 6, textY, c.name, black, EpdFontFamily::BOLD);
    const int tw = r.getTextWidth(font, timeBuf, EpdFontFamily::REGULAR);
    r.drawText(font, w - margin - 6 - tw, textY, timeBuf, black, EpdFontFamily::REGULAR);
  }
}

void drawNeedsSync(const GfxRenderer& r, int w, int h) {
  const int lh = r.getLineHeight(UI_12_FONT_ID);
  const int areaH = h - BAR_HEIGHT;
  r.drawCenteredText(UI_12_FONT_ID, (areaH - lh) / 2, tr(STR_CLOCK_SYNC_RETRY), true);
}

// ===========================================================================
// Stopwatch
// ===========================================================================

void drawStopwatch(const GfxRenderer& r, int w, int h, unsigned long elapsedMs, bool running, bool hasLap,
                   unsigned long lapMs) {
  const int areaH = h - BAR_HEIGHT;

  // Main MM:SS (or H:MM:SS) big; centiseconds as a smaller bottom-right suffix. The e-ink only
  // repaints ~1x/s, so the centiseconds are jumpy while running but exact once stopped.
  char buf[12];
  formatHMS(elapsedMs / 1000, buf, sizeof(buf));
  const int px = stdBigPx(buf, w, areaH);
  const int top = areaH * 24 / 100;
  const int blockW = drawBigTimeText(r, w / 2, top, px, buf);

  // Centiseconds suffix: "--" while running (e-ink can't track 1/100s live); exact once stopped.
  char csBuf[6];
  if (running) {
    snprintf(csBuf, sizeof(csBuf), ".--");
  } else {
    snprintf(csBuf, sizeof(csBuf), ".%02d", static_cast<int>((elapsedMs % 1000) / 10));
  }
  const int csLh = r.getLineHeight(UI_12_FONT_ID);
  r.drawText(UI_12_FONT_ID, w / 2 + blockW / 2 + 4, top + 14 * px - csLh, csBuf, true, EpdFontFamily::BOLD);

  // Running/paused status.
  const int statusY = top + 14 * px + 12;
  r.drawCenteredText(UI_12_FONT_ID, statusY, running ? tr(STR_RUNNING) : tr(STR_PAUSED), true, EpdFontFamily::BOLD);

  // Last lap (with centiseconds).
  if (hasLap) {
    char lapBuf[28];
    char lapTime[12];
    formatHMS(lapMs / 1000, lapTime, sizeof(lapTime));
    snprintf(lapBuf, sizeof(lapBuf), "%s  %s.%02d", tr(STR_LAP), lapTime, static_cast<int>((lapMs % 1000) / 10));
    r.drawCenteredText(UI_12_FONT_ID, statusY + r.getLineHeight(UI_12_FONT_ID) + 6, lapBuf, true,
                       EpdFontFamily::REGULAR);
  }
}

// ===========================================================================
// Timer
// ===========================================================================

void drawTimer(const GfxRenderer& r, int w, int h, unsigned long remainingMs, bool running, bool finished,
               bool flashOn) {
  const int areaH = h - BAR_HEIGHT;

  if (finished) {
    // Visual alarm: invert the content area on each flash phase (no buzzer on this hardware).
    if (flashOn) r.fillRect(0, 0, w, areaH, true);
    const bool textBlack = !flashOn;
    const int lh = r.getLineHeight(NOTOSANS_18_FONT_ID);
    r.drawCenteredText(NOTOSANS_18_FONT_ID, (areaH - lh) / 2, tr(STR_TIMES_UP), textBlack, EpdFontFamily::BOLD);
    return;
  }

  // Always show the remaining time (set duration when idle, live remaining while running, frozen
  // remaining while paused). Minutes are shown directly (e.g. 99:00), never rolled into hours.
  const unsigned long sec = (remainingMs + 999) / 1000;
  char buf[12];
  formatMMSS(sec, buf, sizeof(buf));
  const int px = stdBigPx(buf, w, areaH);
  const int top = areaH * 24 / 100;
  drawBigTimeText(r, w / 2, top, px, buf);

  const int statusY = top + 14 * px + 12;
  r.drawCenteredText(UI_12_FONT_ID, statusY, running ? tr(STR_RUNNING) : tr(STR_TIMER), true, EpdFontFamily::BOLD);
}

}  // namespace clocksub
