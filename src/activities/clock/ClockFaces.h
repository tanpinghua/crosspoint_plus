#pragma once

#include <cstdint>
#include <ctime>

class GfxRenderer;

namespace clockface {

// Everything a face needs to render one frame. Computed once per render in ClockActivity
// and passed by const ref so faces stay pure draw functions with no state of their own.
struct FaceCtx {
  const GfxRenderer& r;
  int w;
  int h;
  int hour24;          // 0..23
  int hour12;          // 1..12
  int minute;          // 0..59
  bool use24h;         // true = 24-hour display (no AM/PM), from SETTINGS.clockFormat
  const char* ampm;    // "AM"/"PM"; only drawn when !use24h
  const char* dayBuf;  // localized weekday name ("Thursday")
  const struct tm* tmNow;
};

// Number of selectable faces. Must match CrossPointSettings::CLOCK_FACE_COUNT.
// Face 0 = Dot-Matrix Hi (with calendar), face 1 = Word Clock.
constexpr uint8_t FACE_COUNT = 2;

// Draw the face selected by faceIdx (clamped/wrapped by the caller). Out-of-range falls
// back to face 0.
void dispatchFace(const FaceCtx& c, uint8_t faceIdx);

}  // namespace clockface
