#pragma once

#include <ctime>

class GfxRenderer;

// Low-level drawing helpers shared by the clock faces. These know nothing about the
// clock's state or the FaceCtx — they only take a renderer and geometry, so they can be
// unit-reasoned and reused across faces. All lookup tables are constexpr (flash-resident).
namespace clockgfx {

// Cell rendering style for the grid digits.
enum CellStyle { CELL_FILLED, CELL_DOT, CELL_OUTLINE };

// Draw a single grid cell of size s at (x, y) in the given style.
void drawCell(const GfxRenderer& r, int x, int y, int s, CellStyle style, bool color = true);

// Render one ASCII digit '0'..'9' from the 5x7 font, each pixel a `cell`-sized block.
// Non-digit characters are ignored.
void drawGridDigit(const GfxRenderer& r, int x, int y, int cell, char ch, CellStyle style, bool color = true);

// Render one ASCII digit '0'..'9' from the chunky 8x14 font as discrete inset dots
// (true dot-matrix look). `px` is the per-pixel cell size. Non-digit characters are ignored.
void drawBigDigit(const GfxRenderer& r, int x, int y, int px, char ch);

// Render a time-like string (digits and ':' separators) using the 8x14 dot font, centered
// horizontally on cx with its top at topY. Colons render as two stacked dots. px is the
// per-pixel cell size. Returns the total pixel width drawn.
int drawBigTimeText(const GfxRenderer& r, int cx, int topY, int px, const char* s);

// A thin horizontal line with a filled dot at `frac` (0..1) along its width — used as a
// non-numeric hour/minute progress indicator.
void drawLineProgressBar(const GfxRenderer& r, int x, int y, int totalW, float frac, int dotR);

// Filled circle via a bounded scan; cheap for the small radii used by the progress dots.
void drawFilledCircle(const GfxRenderer& r, int cx, int cy, int radius, bool color = true);

// Halftone (gray) the already-drawn pixels in a rectangle by erasing every other pixel to
// white. Used to dim inactive word-clock letters and out-of-month calendar days.
void grayOutRect(const GfxRenderer& r, int x, int y, int w, int h);

// Number of days in month0 (0 = January) for the given full year, leap-aware.
int daysInMonth(int year, int month0);

// Draw a month calendar for the date in `tmNow` inside the rectangle (x, y, w, h): a
// bold "MONTH YEAR" header, weekday row, and the day grid with today inverted and
// leading/trailing days from adjacent months grayed.
void drawMonthCalendar(const GfxRenderer& r, const struct tm& tmNow, int x, int y, int w, int h);

}  // namespace clockgfx
