#pragma once

#include <cstdint>
#include <ctime>

class GfxRenderer;

// The Clock activity hosts a carousel of sub-apps. This module holds the per-sub-app state
// (plain structs with trivial logic) and the draw functions. ClockActivity owns one instance
// of each state struct, routes input to them, and calls the draw functions — keeping the host
// lean and the sub-apps free of any activity/lifecycle coupling.
namespace clocksub {

enum SubApp : uint8_t { CLOCK = 0, WORLD_CLOCK, STOPWATCH, TIMER, SUBAPP_COUNT };

// True for sub-apps that need a synced wall clock (Clock, World Clock). Stopwatch/Timer run
// purely off millis() and work with no network/time.
bool needsTime(uint8_t subApp);

// Reserved height for the bottom indicator bar; sub-apps render in (0 .. h - BAR_HEIGHT).
constexpr int BAR_HEIGHT = 34;

// Localized display name for a sub-app (used by the indicator bar).
const char* subAppName(uint8_t subApp);

// Bottom indicator bar: the active sub-app's name centered, with a row of 4 dots (active
// dot enlarged) below it. Drawn over every sub-app.
void drawSubAppBar(const GfxRenderer& r, int w, int h, uint8_t current);

// ===========================================================================
// World clock
// ===========================================================================

struct WorldCity {
  const char* name;
  int offsetMinutes;  // fixed standard offset from UTC (no daylight saving)
};
constexpr int WORLD_CITY_COUNT = 12;
extern const WorldCity WORLD_CITIES[WORLD_CITY_COUNT];

struct WorldClockState {
  int cityIndex = 0;
  void next() { cityIndex = (cityIndex + 1) % WORLD_CITY_COUNT; }
  void prev() { cityIndex = (cityIndex + WORLD_CITY_COUNT - 1) % WORLD_CITY_COUNT; }
};

// Draw the configured-timezone ("local") time large on top (AM/PM at its bottom-right when
// 12h), then a two-column list of all cities (name | time) below, with `selectedCity`
// highlighted. utcNow is the current UTC epoch; localOffsetMinutes is the configured offset
// (used to compute each city's day delta).
void drawWorldClock(const GfxRenderer& r, int w, int h, bool use24h, const struct tm& localTm, time_t utcNow,
                    int localOffsetMinutes, int selectedCity);

// Placeholder shown by Clock/World Clock sub-apps when time isn't synced yet.
void drawNeedsSync(const GfxRenderer& r, int w, int h);

// ===========================================================================
// Stopwatch
// ===========================================================================

struct StopwatchState {
  bool running = false;
  unsigned long startMs = 0;     // millis() at last (re)start
  unsigned long accumMs = 0;     // accumulated elapsed while paused
  bool hasLap = false;
  unsigned long lapTotalMs = 0;  // total elapsed captured at the last lap

  unsigned long elapsed(unsigned long now) const { return accumMs + (running ? (now - startMs) : 0); }

  void startStop(unsigned long now) {
    if (running) {
      accumMs += now - startMs;
      running = false;
    } else {
      startMs = now;
      running = true;
    }
  }

  // While running: capture a lap. While stopped: reset to zero.
  void lapOrReset(unsigned long now) {
    if (running) {
      lapTotalMs = elapsed(now);
      hasLap = true;
    } else {
      accumMs = 0;
      hasLap = false;
      lapTotalMs = 0;
    }
  }
};

void drawStopwatch(const GfxRenderer& r, int w, int h, unsigned long elapsedMs, bool running, bool hasLap,
                   unsigned long lapMs);

// ===========================================================================
// Timer
// ===========================================================================

constexpr int TIMER_MINUTES_WRAP = 100;  // minutes wrap 0..99

struct TimerState {
  bool running = false;
  bool finished = false;
  int setSeconds = 5 * 60;                     // configured countdown duration
  unsigned long endMs = 0;                     // target millis() while running
  unsigned long remainingMs = 5UL * 60 * 1000; // remaining (set duration when idle, frozen when paused)

  unsigned long remaining(unsigned long now) const {
    if (running) return (now < endMs) ? (endMs - now) : 0;
    return remainingMs;
  }

  // Adjust the configured duration by whole minutes (ignored while running). Wraps 0..99, so
  // -1 at 0 goes to 99 and +1 at 99 goes to 0.
  void adjustMinutes(int delta) {
    if (running) return;
    int m = setSeconds / 60;
    m = ((m + delta) % TIMER_MINUTES_WRAP + TIMER_MINUTES_WRAP) % TIMER_MINUTES_WRAP;
    setSeconds = m * 60;
    remainingMs = static_cast<unsigned long>(setSeconds) * 1000UL;
    finished = false;
  }

  void startStop(unsigned long now) {
    if (finished) {  // dismiss the alarm and re-arm to the configured duration
      finished = false;
      remainingMs = static_cast<unsigned long>(setSeconds) * 1000UL;
      return;
    }
    if (running) {
      remainingMs = remaining(now);
      running = false;
    } else {
      if (remainingMs == 0) remainingMs = static_cast<unsigned long>(setSeconds) * 1000UL;
      if (remainingMs == 0) return;  // nothing set
      endMs = now + remainingMs;
      running = true;
    }
  }

  // Stop and re-arm to the configured duration (hold-Select reset).
  void reset() {
    running = false;
    finished = false;
    remainingMs = static_cast<unsigned long>(setSeconds) * 1000UL;
  }

  // Returns true exactly once, on the transition to finished.
  bool poll(unsigned long now) {
    if (running && now >= endMs) {
      running = false;
      finished = true;
      remainingMs = 0;
      return true;
    }
    return false;
  }
};

void drawTimer(const GfxRenderer& r, int w, int h, unsigned long remainingMs, bool running, bool finished,
               bool flashOn);

}  // namespace clocksub
