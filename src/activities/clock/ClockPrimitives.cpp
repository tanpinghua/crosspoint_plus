#include "ClockPrimitives.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "fontIds.h"

namespace clockgfx {

namespace {

// 5x7 digit bitmap. Each row uses the low 5 bits; bit4 = leftmost column.
constexpr uint8_t DIGIT_5x7[10][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},  // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},  // 2
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},  // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},  // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},  // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},  // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},  // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},  // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},  // 9
};

// 8x14 chunky digit bitmap. Each row: bit7 = leftmost column, bit0 = rightmost.
constexpr uint8_t DIGIT_8x14[10][14] = {
    {0x3C, 0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0x7E, 0x3C},  // 0
    {0x0C, 0x1C, 0x3C, 0x6C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x7E},  // 1
    {0x7E, 0xFF, 0xC3, 0x03, 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0xC0, 0xFF, 0xFF},  // 2
    {0xFF, 0xFF, 0x03, 0x03, 0x06, 0x7E, 0x7E, 0x06, 0x03, 0x03, 0x03, 0xC3, 0xFF, 0x7E},  // 3
    {0x06, 0x0E, 0x1E, 0x36, 0x66, 0xC6, 0xC6, 0xFF, 0xFF, 0x06, 0x06, 0x06, 0x06, 0x06},  // 4
    {0xFF, 0xFF, 0xC0, 0xC0, 0xC0, 0xFC, 0x7E, 0x03, 0x03, 0x03, 0x03, 0xC3, 0xFF, 0x7E},  // 5
    {0x3C, 0x7E, 0xC0, 0xC0, 0xC0, 0xFC, 0xFE, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0x7E, 0x3C},  // 6
    {0xFF, 0xFF, 0x03, 0x06, 0x06, 0x0C, 0x0C, 0x18, 0x18, 0x30, 0x30, 0x60, 0x60, 0xC0},  // 7
    {0x7E, 0xFF, 0xC3, 0xC3, 0xC3, 0x7E, 0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xFF, 0x7E},  // 8
    {0x7E, 0xFF, 0xC3, 0xC3, 0xC3, 0xC3, 0xFF, 0x7F, 0x03, 0x03, 0x03, 0xC3, 0xFF, 0x7E},  // 9
};

constexpr char MONTHS[12][10] = {"JANUARY",   "FEBRUARY", "MARCH",  "APRIL",    "MAY",      "JUNE",
                                 "JULY",      "AUGUST",   "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
constexpr char WEEKDAYS[7][2] = {"S", "M", "T", "W", "T", "F", "S"};

}  // namespace

void drawCell(const GfxRenderer& r, int x, int y, int s, CellStyle style, bool color) {
  switch (style) {
    case CELL_FILLED:
      r.fillRect(x, y, s, s, color);
      break;
    case CELL_DOT: {
      const int pad = std::max(1, s / 6);
      r.fillRect(x + pad, y + pad, s - 2 * pad, s - 2 * pad, color);
      break;
    }
    case CELL_OUTLINE: {
      const int t = std::max(1, s / 6);
      r.fillRect(x, y, s, t, color);
      r.fillRect(x, y + s - t, s, t, color);
      r.fillRect(x, y, t, s, color);
      r.fillRect(x + s - t, y, t, s, color);
      break;
    }
  }
}

void drawGridDigit(const GfxRenderer& r, int x, int y, int cell, char ch, CellStyle style, bool color) {
  if (ch < '0' || ch > '9') return;
  const auto& bmp = DIGIT_5x7[ch - '0'];
  for (int row = 0; row < 7; ++row) {
    const uint8_t bits = bmp[row];
    for (int col = 0; col < 5; ++col) {
      if (bits & (0x10 >> col)) {
        drawCell(r, x + col * cell, y + row * cell, cell, style, color);
      }
    }
  }
}

void drawBigDigit(const GfxRenderer& r, int x, int y, int px, char ch) {
  if (ch < '0' || ch > '9') return;
  const auto& bmp = DIGIT_8x14[ch - '0'];
  for (int row = 0; row < 14; ++row) {
    const uint8_t bits = bmp[row];
    for (int col = 0; col < 8; ++col) {
      if (bits & (0x80 >> col)) {
        drawCell(r, x + col * px, y + row * px, px, CELL_DOT, true);
      }
    }
  }
}

int drawBigTimeText(const GfxRenderer& r, int cx, int topY, int px, const char* s) {
  const int digitW = 8 * px;
  const int colonW = 3 * px;
  const int gap = px;

  // First pass: total width so we can center.
  int total = 0;
  for (const char* p = s; *p; ++p) {
    if (p != s) total += gap;
    total += (*p == ':') ? colonW : digitW;
  }

  int x = cx - total / 2;
  for (const char* p = s; *p; ++p) {
    if (*p == ':') {
      const int dotSize = 2 * px;
      const int dotX = x + (colonW - dotSize) / 2;
      drawCell(r, dotX, topY + 4 * px, dotSize, CELL_DOT, true);
      drawCell(r, dotX, topY + 9 * px, dotSize, CELL_DOT, true);
      x += colonW + gap;
    } else {
      drawBigDigit(r, x, topY, px, *p);  // ignores non-digits
      x += digitW + gap;
    }
  }
  return total;
}

void drawFilledCircle(const GfxRenderer& r, int cx, int cy, int radius, bool color) {
  if (radius < 1) {
    r.drawPixel(cx, cy, color);
    return;
  }
  const int r2 = radius * radius;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dy * dy <= r2) r.drawPixel(cx + dx, cy + dy, color);
    }
  }
}

void drawLineProgressBar(const GfxRenderer& r, int x, int y, int totalW, float frac, int dotR) {
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  r.drawLine(x, y, x + totalW, y, 2, true);
  const int dotX = x + static_cast<int>(frac * totalW);
  drawFilledCircle(r, dotX, y, dotR, true);
}

void grayOutRect(const GfxRenderer& r, int x, int y, int w, int h) {
  for (int yy = y; yy < y + h; ++yy) {
    const int startX = x + (yy & 1);
    for (int xx = startX; xx < x + w; xx += 2) {
      r.drawPixel(xx, yy, false);
    }
  }
}

int daysInMonth(int year, int month0) {
  static constexpr int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month0 == 1) {
    const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return leap ? 29 : 28;
  }
  return d[month0];
}

void drawMonthCalendar(const GfxRenderer& r, const struct tm& tmNow, int x, int y, int w, int h) {
  const int year = tmNow.tm_year + 1900;
  const int month0 = tmNow.tm_mon;
  const int today = tmNow.tm_mday;
  const int dim = daysInMonth(year, month0);
  // Weekday (0=Sun) of the 1st of the month, derived from today's weekday.
  const int firstWday = (tmNow.tm_wday - ((tmNow.tm_mday - 1) % 7) + 7) % 7;

  char header[24];
  snprintf(header, sizeof(header), "%s %d", MONTHS[month0], year);
  const int headerFontId = NOTOSANS_16_FONT_ID;
  const int headerLh = r.getLineHeight(headerFontId);
  // Center within this calendar's area (x..x+w), not the whole screen — otherwise the
  // header lands over the clock when the calendar occupies only half the screen (landscape).
  const int headerTw = r.getTextWidth(headerFontId, header, EpdFontFamily::BOLD);
  r.drawText(headerFontId, x + (w - headerTw) / 2, y + 2, header, true, EpdFontFamily::BOLD);

  const int gridY = y + headerLh + 8;
  const int gridH = h - (gridY - y) - 4;
  const int cols = 7;
  const int rows = 7;  // 1 weekday header row + up to 6 week rows
  const int cellW = w / cols;
  const int cellH = gridH / rows;
  const int gridX = x + (w - cellW * cols) / 2;

  const int dayFontId = NOTOSANS_14_FONT_ID;
  const int dayLh = r.getLineHeight(dayFontId);

  // Weekday header row
  for (int i = 0; i < 7; ++i) {
    const int tw = r.getTextWidth(dayFontId, WEEKDAYS[i], EpdFontFamily::BOLD);
    const int cx = gridX + i * cellW + (cellW - tw) / 2;
    const int cy = gridY + (cellH - dayLh) / 2;
    r.drawText(dayFontId, cx, cy, WEEKDAYS[i], true, EpdFontFamily::BOLD);
  }
  r.drawLine(gridX, gridY + cellH, gridX + cols * cellW, gridY + cellH, 1, true);

  // Render a day number into a grid slot, optionally grayed (adjacent-month fillers).
  auto renderDayAt = [&](int day, int gridIdx, bool grayed) {
    const int row = gridIdx / 7 + 1;
    const int col = gridIdx % 7;
    if (row >= rows) return;
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", day);
    const int tw = r.getTextWidth(dayFontId, buf);
    const int cellLeft = gridX + col * cellW;
    const int cellTop = gridY + row * cellH;
    const int cx = cellLeft + (cellW - tw) / 2;
    const int cy = cellTop + (cellH - dayLh) / 2;
    r.drawText(dayFontId, cx, cy, buf, true);
    if (grayed) grayOutRect(r, cx - 1, cy, tw + 2, dayLh);
  };

  // Leading days from the previous month (grayed)
  if (firstWday > 0) {
    const int prevMonth = (month0 + 11) % 12;
    const int prevYear = (month0 == 0) ? year - 1 : year;
    const int prevDim = daysInMonth(prevYear, prevMonth);
    for (int slot = 0; slot < firstWday; ++slot) {
      const int day = prevDim - firstWday + 1 + slot;
      renderDayAt(day, slot, /*grayed=*/true);
    }
  }

  // Current month days; today gets an inverted (filled) cell.
  for (int day = 1; day <= dim; ++day) {
    const int gridIdx = firstWday + day - 1;
    const int row = gridIdx / 7 + 1;
    const int col = gridIdx % 7;
    if (row >= rows) break;

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", day);
    const int tw = r.getTextWidth(dayFontId, buf);
    const int cellLeft = gridX + col * cellW;
    const int cellTop = gridY + row * cellH;
    const int cx = cellLeft + (cellW - tw) / 2;
    const int cy = cellTop + (cellH - dayLh) / 2;

    if (day == today) {
      const int sq = std::min(cellW, cellH) - 6;
      const int sx = cellLeft + (cellW - sq) / 2;
      const int sy = cellTop + (cellH - sq) / 2;
      r.fillRect(sx, sy, sq, sq, true);
      r.drawText(dayFontId, cx, cy, buf, false);
    } else {
      r.drawText(dayFontId, cx, cy, buf, true);
    }
  }

  // Trailing days from the next month (grayed) — fill remaining slots in visible rows.
  const int totalSlots = (rows - 1) * 7;
  const int usedSlots = firstWday + dim;
  int nextDay = 1;
  for (int slot = usedSlots; slot < totalSlots; ++slot) {
    renderDayAt(nextDay++, slot, /*grayed=*/true);
  }
}

}  // namespace clockgfx
