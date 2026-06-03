#include "ClockFaces.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ClockPrimitives.h"
#include "fontIds.h"

namespace clockface {

using namespace clockgfx;

namespace {

// Format the two-digit hour for the numeric faces, honoring 12/24h mode.
void formatHourMinute(const FaceCtx& c, char hh[3], char mm[3]) {
  snprintf(hh, 3, "%02d", c.use24h ? c.hour24 : c.hour12);
  snprintf(mm, 3, "%02d", c.minute);
}

// ===========================================================================
// FACE 0 — Dot Matrix: 5x7 dot digits, hour/minute progress bars, month calendar.
// ===========================================================================

// Draw the day-of-week label, big 8x14 digits, and hour/minute progress bars inside the
// given area rect. Centering is relative to the area (not the screen) so this works for
// both a full-width column (portrait) and a half-width column (landscape).
void drawDotMatrixHiClockContent(const FaceCtx& c, int ax, int ay, int aw, int ah) {
  // Lowercase day-of-week label ("thursday"), centered in the area.
  char dayLow[16];
  snprintf(dayLow, sizeof(dayLow), "%s", c.dayBuf);
  for (char* p = dayLow; *p; ++p) {
    if (*p >= 'A' && *p <= 'Z') *p = *p - 'A' + 'a';
  }
  const int dayFontId = NOTOSANS_14_FONT_ID;
  const int dayLh = c.r.getLineHeight(dayFontId);
  const int dayY = ay + 6;
  const int dayTw = c.r.getTextWidth(dayFontId, dayLow, EpdFontFamily::REGULAR);
  c.r.drawText(dayFontId, ax + (aw - dayTw) / 2, dayY, dayLow, true, EpdFontFamily::REGULAR);

  // Clock region fills the area below the day label.
  const int sideMargin = 8;
  const int clockTop = dayY + dayLh + 6;
  const int clockBot = ay + ah - 6;
  const int clockH = clockBot - clockTop;

  // 4 digits × 8 cells + 3 inter-digit gaps × 1 cell = 35 cells wide, 14 tall.
  const int barReserve = 70;
  const int blockMaxW = aw - 2 * sideMargin;
  const int blockMaxH = clockH - barReserve;
  int px = std::min(blockMaxW / 35, blockMaxH / 14);
  if (px < 3) px = 3;

  const int digitW = 8 * px;
  const int digitH = 14 * px;
  const int gap = px;
  const int blockW = 4 * digitW + 3 * gap;
  const int blockX = ax + (aw - blockW) / 2;
  const int blockY = clockTop + (clockH - digitH) / 2;

  char hh[3], mm[3];
  formatHourMinute(c, hh, mm);
  int dx = blockX;
  drawBigDigit(c.r, dx, blockY, px, hh[0]);
  dx += digitW + gap;
  drawBigDigit(c.r, dx, blockY, px, hh[1]);
  dx += digitW + gap;
  drawBigDigit(c.r, dx, blockY, px, mm[0]);
  dx += digitW + gap;
  drawBigDigit(c.r, dx, blockY, px, mm[1]);

  // Progress bars above/below the digits.
  const int barGap = 22;
  const int hourBarY = blockY - barGap;
  const int minBarY = blockY + digitH + barGap;
  const float hourFrac = (c.hour24 + c.minute / 60.0f) / 24.0f;
  const float minFrac = c.minute / 60.0f;
  const int dotR = std::max(3, px / 2);
  drawLineProgressBar(c.r, blockX, hourBarY, blockW, hourFrac, dotR);
  drawLineProgressBar(c.r, blockX, minBarY, blockW, minFrac, dotR);

  // AM/PM bottom-right of the digit block (12h mode only).
  if (!c.use24h) {
    const int ampmFontId = UI_12_FONT_ID;
    const int ampmW = c.r.getTextWidth(ampmFontId, c.ampm, EpdFontFamily::BOLD);
    c.r.drawText(ampmFontId, blockX + blockW - ampmW, minBarY + 12, c.ampm, true, EpdFontFamily::BOLD);
  }
}

void drawFace_DotMatrixHi(const FaceCtx& c) {
  const bool landscape = c.w > c.h;

  if (landscape) {
    // Clock on the left half, calendar on the right half, vertical hairline between.
    const int half = c.w / 2;
    drawDotMatrixHiClockContent(c, 0, 0, half, c.h);
    c.r.drawLine(half, 16, half, c.h - 16, 1, true);
    drawMonthCalendar(c.r, *c.tmNow, half + 12, 16, c.w - half - 24, c.h - 32);
  } else {
    // Portrait: clock on top (~44%), calendar filling the bottom.
    const int topH = c.h * 44 / 100;
    drawDotMatrixHiClockContent(c, 0, 0, c.w, topH);
    const int calY = topH + 12;
    const int calH = c.h - calY - 8;
    drawMonthCalendar(c.r, *c.tmNow, 8, calY, c.w - 16, calH);
  }
}

// ===========================================================================
// FACE 1 — Word Clock: "IT IS HALF PAST NINE" letter grid.
// ===========================================================================

void drawFace_WordClock(const FaceCtx& c) {
  static constexpr char GRID[10][12] = {
      "ITLISASTIME",  //
      "ACQUARTERDC",  //
      "TWENTYFIVEX",  //
      "HALFBTENFTO",  //
      "PASTERUNINE",  //
      "ONESIXTHREE",  //
      "FOURFIVETWO",  //
      "EIGHTELEVEN",  //
      "SEVENTWELVE",  //
      "TENSEOCLOCK",  //
  };

  // Round to the nearest 5 minutes and decide past/to + which hour to name.
  int slot = (c.minute + 2) / 5;  // 0..12
  int displayHour = c.hour12;
  bool past = true;
  if (slot == 0) {
    // O'CLOCK
  } else if (slot >= 7) {
    past = false;
    displayHour = (c.hour12 % 12) + 1;
    slot = 12 - slot;
  }

  bool active[10][11] = {{false}};
  auto mark = [&](int row, int col, int len) {
    for (int i = 0; i < len; ++i) active[row][col + i] = true;
  };

  mark(0, 0, 2);  // IT
  mark(0, 3, 2);  // IS

  switch (slot) {
    case 1:
      mark(2, 6, 4);  // FIVE
      break;
    case 2:
      mark(3, 5, 3);  // TEN
      break;
    case 3:
      mark(1, 0, 1);  // A
      mark(1, 2, 7);  // QUARTER
      break;
    case 4:
      mark(2, 0, 6);  // TWENTY
      break;
    case 5:
      mark(2, 0, 6);  // TWENTY
      mark(2, 6, 4);  // FIVE
      break;
    case 6:
      mark(3, 0, 4);  // HALF
      break;
    default:
      break;
  }
  if (slot != 0) {
    if (past)
      mark(4, 0, 4);  // PAST
    else
      mark(3, 9, 2);  // TO
  }

  switch (displayHour) {
    case 1:
      mark(5, 0, 3);
      break;
    case 2:
      mark(6, 8, 3);
      break;
    case 3:
      mark(5, 6, 5);
      break;
    case 4:
      mark(6, 0, 4);
      break;
    case 5:
      mark(6, 4, 4);
      break;
    case 6:
      mark(5, 3, 3);
      break;
    case 7:
      mark(8, 0, 5);
      break;
    case 8:
      mark(7, 0, 5);
      break;
    case 9:
      mark(4, 7, 4);
      break;
    case 10:
      mark(9, 0, 3);
      break;
    case 11:
      mark(7, 5, 6);
      break;
    case 12:
      mark(8, 5, 6);
      break;
  }

  if (slot == 0) mark(9, 5, 6);  // O'CLOCK

  const int margin = 20;
  const int cellW = (c.w - 2 * margin) / 11;
  const int cellH = (c.h - 2 * margin) / 10;
  const int gridX = margin + ((c.w - 2 * margin) - cellW * 11) / 2;
  const int gridY = margin + ((c.h - 2 * margin) - cellH * 10) / 2;

  const int activeFont = NOTOSANS_18_FONT_ID;
  const int inactiveFont = NOTOSANS_14_FONT_ID;
  const int activeLh = c.r.getLineHeight(activeFont);
  const int inactiveLh = c.r.getLineHeight(inactiveFont);

  for (int row = 0; row < 10; ++row) {
    for (int col = 0; col < 11; ++col) {
      char buf[2] = {GRID[row][col], 0};
      const int cellLeft = gridX + col * cellW;
      const int cellTop = gridY + row * cellH;
      if (active[row][col]) {
        const int tw = c.r.getTextWidth(activeFont, buf, EpdFontFamily::BOLD);
        const int x = cellLeft + (cellW - tw) / 2;
        const int y = cellTop + (cellH - activeLh) / 2;
        c.r.drawText(activeFont, x, y, buf, true, EpdFontFamily::BOLD);
      } else {
        const int tw = c.r.getTextWidth(inactiveFont, buf, EpdFontFamily::REGULAR);
        const int x = cellLeft + (cellW - tw) / 2;
        const int y = cellTop + (cellH - inactiveLh) / 2;
        c.r.drawText(inactiveFont, x, y, buf, true, EpdFontFamily::REGULAR);
        grayOutRect(c.r, x - 1, y, tw + 2, inactiveLh);  // dim inactive letters
      }
    }
  }
}

}  // namespace

void dispatchFace(const FaceCtx& c, uint8_t faceIdx) {
  switch (faceIdx) {
    case 1:
      drawFace_WordClock(c);
      return;
    case 0:
    default:
      drawFace_DotMatrixHi(c);
      return;
  }
}

}  // namespace clockface
