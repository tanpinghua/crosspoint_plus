#pragma once

#include <cstdint>

#include "ClockSubApps.h"
#include "activities/Activity.h"

// Full-screen clock app for the X4 (which has no battery-backed RTC — unlike the X3's DS3231).
// Hosts a carousel of sub-apps (Clock, World Clock, Stopwatch, Timer) cycled with the side
// buttons, with a bottom indicator bar. Time-dependent sub-apps sync NTP lazily over WiFi; the
// device is kept awake (preventAutoSleep) so the clock/stopwatch/timer stay live.
class ClockActivity final : public Activity {
  // READY = a sub-app is on screen. The other states are transient WiFi/NTP sync steps that
  // briefly take over the screen for the time-dependent sub-apps.
  enum class HostState { READY, NEEDS_WIFI, SCANNING, RECONNECTING, SYNCING };

  static constexpr unsigned long NTP_TIMEOUT_MS = 15000UL;
  static constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;
  static constexpr unsigned long WIFI_SCAN_TIMEOUT_MS = 10000UL;
  static constexpr unsigned long AUTO_SYNC_INTERVAL_MS = 3UL * 60 * 60 * 1000;  // 3 h
  static constexpr unsigned long FACE_INDICATOR_MS = 1000UL;
  static constexpr unsigned long TIMER_FLASH_MS = 700UL;
  static constexpr unsigned long MINUTE_HOLD_INITIAL_MS = 500UL;  // delay before hold-repeat starts
  static constexpr unsigned long MINUTE_HOLD_REPEAT_MS = 120UL;   // hold-repeat interval
  static constexpr unsigned long TIMER_RESET_HOLD_MS = 1000UL;    // hold Select to reset the timer

  HostState state = HostState::READY;
  uint8_t subApp = clocksub::CLOCK;
  unsigned long syncStartMs = 0;
  unsigned long reconnectStartMs = 0;
  unsigned long scanStartMs = 0;
  unsigned long lastSyncMs = 0;
  bool timeSynced = false;

  // Tracks the currently displayed time unit (minute for clocks, second for stopwatch/timer) so
  // we only repaint the e-ink when the shown value actually changes.
  long lastShownUnit = -1;

  // Clock sub-app: brief "1/2" overlay after Left cycles the face.
  unsigned long faceIndicatorHideMs = 0;
  bool showFaceIndicator = false;

  // Timer sub-app: visual alarm flashing + minute-adjust hold-repeat state.
  bool flashOn = false;
  unsigned long flashToggleMs = 0;
  unsigned long minuteRepeatNextMs = 0;

  clocksub::WorldClockState world;
  clocksub::StopwatchState stopwatch;
  clocksub::TimerState timer;

  void enterSyncing();
  void enterScanning();                          // async scan for remembered networks
  void connectToSsid(const std::string& ssid);  // connect to a specific remembered SSID
  void launchWifiSelection();
  void requestSync();  // start the sync flow (used on entry/switch for time-dependent sub-apps)
  void disconnectWifi() const;
  void switchSubApp(int dir);  // +1 = next, -1 = previous; persists and may trigger a sync
  void cycleOrientation(int direction);
  void cycleFace(int direction);

  static bool isLandscape(uint8_t orientation);
  // Apply the orientation for the active sub-app: the Clock keeps its own (possibly landscape)
  // orientation; every other sub-app is forced vertical.
  void applyOrientationForSubApp();
  // The sub-app indicator bar is hidden only for the Clock sub-app in landscape.
  bool barVisible() const;

  // Per-sub-app input + per-tick redraw scheduling.
  void tickClock(unsigned long now);
  void tickWorldClock(unsigned long now);
  void tickStopwatch(unsigned long now);
  void tickTimer(unsigned long now);

  void renderSubApp(int w, int h);

  int configuredOffsetMinutes() const;
  // Fill `out` with wall-clock time at the given UTC offset (minutes). False until NTP-synced.
  bool getTimeForOffset(int offsetMinutes, struct tm& out) const;
  bool getDisplayTime(struct tm& out) const;  // at the configured time zone

 public:
  explicit ClockActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Clock", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  // Keep the device awake while the clock is on screen — bypass auto-sleep entirely.
  bool preventAutoSleep() override { return true; }
  // Lets main.cpp persist "last activity was clock" so a power-off resumes here.
  bool isClockActivity() const override { return true; }
};
