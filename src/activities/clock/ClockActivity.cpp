#include "ClockActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <time.h>

#include <cstdio>

#include "ClockFaces.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "WifiCredentialStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "components/icons/wifi.h"
#include "fontIds.h"

using clocksub::SubApp;

static_assert(clockface::FACE_COUNT == CrossPointSettings::CLOCK_FACE_COUNT,
              "Face count mismatch between ClockFaces and CrossPointSettings");

// ===========================================================================
// Time helpers
// ===========================================================================

int ClockActivity::configuredOffsetMinutes() const {
  uint8_t q = SETTINGS.clockUtcOffsetQ;
  if (q > 104) q = 48;  // clamp corrupt persisted values to UTC+0
  return (static_cast<int>(q) - 48) * 15;
}

bool ClockActivity::getTimeForOffset(int offsetMinutes, struct tm& out) const {
  // System time is kept in UTC (configTime offset 0); false means NTP hasn't set it yet.
  struct tm probe;
  if (!getLocalTime(&probe, 0)) return false;
  const time_t utc = time(nullptr);
  const time_t local = utc + static_cast<time_t>(offsetMinutes) * 60;
  gmtime_r(&local, &out);
  return true;
}

bool ClockActivity::getDisplayTime(struct tm& out) const { return getTimeForOffset(configuredOffsetMinutes(), out); }

// ===========================================================================
// WiFi + NTP
// ===========================================================================

void ClockActivity::disconnectWifi() const {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  LOG_INF("CLOCK", "WiFi disconnected to preserve battery");
}

void ClockActivity::enterSyncing() {
  state = HostState::SYNCING;
  syncStartMs = millis();
  // Sync in UTC; per-zone offsets are applied at display time, so changing the time zone or
  // selecting a world city never requires a re-sync.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  LOG_INF("CLOCK", "NTP sync started");
  requestUpdate();
}

void ClockActivity::enterScanning() {
  WiFi.mode(WIFI_STA);
  WiFi.scanNetworks(true);  // async; results polled in loop()
  state = HostState::SCANNING;
  scanStartMs = millis();
  LOG_INF("CLOCK", "Scanning for remembered networks");
  requestUpdate();
}

void ClockActivity::connectToSsid(const std::string& ssid) {
  const WifiCredential* cred = WIFI_STORE.findCredential(ssid);
  WiFi.mode(WIFI_STA);
  if (cred && !cred->password.empty()) {
    WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  } else {
    WiFi.begin(ssid.c_str());
  }
  state = HostState::RECONNECTING;
  reconnectStartMs = millis();
  LOG_INF("CLOCK", "Auto-connecting to strongest remembered \"%s\"", ssid.c_str());
  requestUpdate();
}

void ClockActivity::launchWifiSelection() {
  state = HostState::NEEDS_WIFI;
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false),
                         [this](const ActivityResult&) {
                           if (WiFi.status() == WL_CONNECTED) {
                             enterSyncing();
                           } else {
                             LOG_ERR("CLOCK", "WiFi not connected after selection");
                             state = HostState::READY;  // sub-app shows the "needs sync" placeholder
                             requestUpdate();
                           }
                         });
}

void ClockActivity::requestSync() {
  if (state != HostState::READY) return;  // a sync is already in flight
  if (WiFi.status() == WL_CONNECTED) {
    enterSyncing();
  } else if (!WIFI_STORE.getCredentials().empty()) {
    enterScanning();  // scan, then auto-connect to the strongest remembered network in range
  } else {
    launchWifiSelection();
  }
}

// ===========================================================================
// Sub-app / face / orientation cycling
// ===========================================================================

void ClockActivity::switchSubApp(int dir) {
  subApp = static_cast<uint8_t>((subApp + clocksub::SUBAPP_COUNT + dir) % clocksub::SUBAPP_COUNT);
  SETTINGS.clockSubAppIndex = subApp;
  SETTINGS.clockTimerMinutes = static_cast<uint8_t>(timer.setSeconds / 60);  // persist with the same write
  SETTINGS.saveToFile();
  lastShownUnit = -1;
  showFaceIndicator = false;
  applyOrientationForSubApp();  // non-clock sub-apps go vertical; returning to Clock restores its orientation
  if (clocksub::needsTime(subApp) && !timeSynced) requestSync();
  requestUpdate();
}

bool ClockActivity::isLandscape(uint8_t orientation) {
  return orientation == GfxRenderer::LandscapeClockwise || orientation == GfxRenderer::LandscapeCounterClockwise;
}

void ClockActivity::applyOrientationForSubApp() {
  uint8_t ori;
  if (subApp == clocksub::CLOCK) {
    ori = SETTINGS.clockOrientation;  // Clock may be portrait or landscape
  } else {
    // Other sub-apps are vertical only; fall back to Portrait when the clock is landscape.
    ori = isLandscape(SETTINGS.clockOrientation) ? GfxRenderer::Portrait : SETTINGS.clockOrientation;
  }
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(ori));
}

bool ClockActivity::barVisible() const {
  return !(subApp == clocksub::CLOCK && isLandscape(SETTINGS.clockOrientation));
}

void ClockActivity::cycleOrientation(int direction) {
  const uint8_t ori = (SETTINGS.clockOrientation + 4 + direction) % 4;
  if (ori == SETTINGS.clockOrientation) return;
  SETTINGS.clockOrientation = ori;
  SETTINGS.saveToFile();
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(ori));
  requestUpdate();
}

void ClockActivity::cycleFace(int direction) {
  const uint8_t count = CrossPointSettings::CLOCK_FACE_COUNT;
  const uint8_t next = (SETTINGS.clockFaceIndex + count + direction) % count;
  if (next == SETTINGS.clockFaceIndex) return;
  SETTINGS.clockFaceIndex = next;
  SETTINGS.saveToFile();
  showFaceIndicator = true;
  faceIndicatorHideMs = millis() + FACE_INDICATOR_MS;
  requestUpdate();
}

// ===========================================================================
// Lifecycle
// ===========================================================================

void ClockActivity::onEnter() {
  Activity::onEnter();
  if (SETTINGS.clockFaceIndex >= CrossPointSettings::CLOCK_FACE_COUNT) SETTINGS.clockFaceIndex = 0;
  if (SETTINGS.clockSubAppIndex >= clocksub::SUBAPP_COUNT) SETTINGS.clockSubAppIndex = 0;
  subApp = SETTINGS.clockSubAppIndex;
  applyOrientationForSubApp();

  // Restore the previously configured timer duration.
  const uint8_t timerMins = (SETTINGS.clockTimerMinutes <= 99) ? SETTINGS.clockTimerMinutes : 5;
  timer.setSeconds = static_cast<int>(timerMins) * 60;
  timer.remainingMs = static_cast<unsigned long>(timer.setSeconds) * 1000UL;

  state = HostState::READY;
  lastShownUnit = -1;
  if (clocksub::needsTime(subApp)) requestSync();
  requestUpdate();
}

void ClockActivity::onExit() {
  // Persist the timer duration so it's remembered next time (change-guarded to avoid a needless write).
  const uint8_t timerMins = static_cast<uint8_t>(timer.setSeconds / 60);
  if (timerMins != SETTINGS.clockTimerMinutes) {
    SETTINGS.clockTimerMinutes = timerMins;
    SETTINGS.saveToFile();
  }
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(SETTINGS.orientation));
  Activity::onExit();
}

// ===========================================================================
// Per-sub-app ticks (input + redraw scheduling)
// ===========================================================================

void ClockActivity::tickClock(unsigned long now) {
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    cycleFace(+1);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    cycleOrientation(-1);  // rotate (inverted direction)
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    requestSync();  // manual re-sync
    return;
  }

  struct tm t;
  if (getDisplayTime(t)) {
    if (t.tm_min != lastShownUnit) {
      lastShownUnit = t.tm_min;
      requestUpdate();
    }
    if (timeSynced && now - lastSyncMs > AUTO_SYNC_INTERVAL_MS) requestSync();
  }
}

void ClockActivity::tickWorldClock(unsigned long now) {
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    world.prev();
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    world.next();
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    requestSync();
    return;
  }

  struct tm t;
  if (getDisplayTime(t)) {
    if (t.tm_min != lastShownUnit) {
      lastShownUnit = t.tm_min;
      requestUpdate();
    }
    if (timeSynced && now - lastSyncMs > AUTO_SYNC_INTERVAL_MS) requestSync();
  }
}

void ClockActivity::tickStopwatch(unsigned long now) {
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    stopwatch.startStop(now);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    stopwatch.lapOrReset(now);
    requestUpdate();
  }

  if (stopwatch.running) {
    const long sec = static_cast<long>(stopwatch.elapsed(now) / 1000);
    if (sec != lastShownUnit) {
      lastShownUnit = sec;
      requestUpdate();
    }
  }
}

void ClockActivity::tickTimer(unsigned long now) {
  // Left = -1 min, Right = +1 min, with press-and-hold auto-repeat. adjustMinutes() is a no-op
  // while running, so holding during a countdown does nothing.
  auto adjustHeld = [&](MappedInputManager::Button btn, int delta) {
    if (mappedInput.wasPressed(btn)) {
      timer.adjustMinutes(delta);
      minuteRepeatNextMs = now + MINUTE_HOLD_INITIAL_MS;
      requestUpdate();
    } else if (mappedInput.isPressed(btn) && static_cast<long>(now - minuteRepeatNextMs) >= 0) {
      timer.adjustMinutes(delta);
      minuteRepeatNextMs = now + MINUTE_HOLD_REPEAT_MS;
      requestUpdate();
    }
  };
  adjustHeld(MappedInputManager::Button::Left, -1);
  adjustHeld(MappedInputManager::Button::Right, +1);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() >= TIMER_RESET_HOLD_MS) {
      timer.reset();  // hold Select = reset to the configured duration
    } else {
      timer.startStop(now);  // tap Select = start / pause (pausing keeps the remaining time)
    }
    flashOn = false;
    requestUpdate();
  }

  if (timer.poll(now)) {  // just hit zero
    flashOn = true;
    flashToggleMs = now + TIMER_FLASH_MS;
    requestUpdate();
    return;
  }

  if (timer.finished) {
    if (static_cast<long>(now - flashToggleMs) >= 0) {
      flashOn = !flashOn;
      flashToggleMs = now + TIMER_FLASH_MS;
      requestUpdate();
    }
    return;
  }

  if (timer.running) {
    const long sec = static_cast<long>((timer.remaining(now) + 999) / 1000);
    if (sec != lastShownUnit) {
      lastShownUnit = sec;
      requestUpdate();
    }
  }
}

void ClockActivity::loop() {
  const unsigned long now = millis();

  if (showFaceIndicator && static_cast<long>(now - faceIndicatorHideMs) >= 0) {
    showFaceIndicator = false;
    requestUpdate();
  }

  // --- Transient sync states take over until resolved ---
  if (state == HostState::SCANNING) {
    const int found = WiFi.scanComplete();
    if (found >= 0) {
      // Pick the strongest in-range network we have credentials for.
      std::string bestSsid;
      int32_t bestRssi = -127;
      for (int i = 0; i < found; ++i) {
        const std::string ssid = WiFi.SSID(i).c_str();
        if (WIFI_STORE.findCredential(ssid) && WiFi.RSSI(i) > bestRssi) {
          bestRssi = WiFi.RSSI(i);
          bestSsid = ssid;
        }
      }
      WiFi.scanDelete();
      if (!bestSsid.empty()) {
        connectToSsid(bestSsid);
      } else {
        launchWifiSelection();  // no remembered network in range — let the user pick
      }
    } else if (found == WIFI_SCAN_FAILED || now - scanStartMs > WIFI_SCAN_TIMEOUT_MS) {
      WiFi.scanDelete();
      launchWifiSelection();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      disconnectWifi();
      onGoHome(HomeMenuItem::CLOCK);
    }
    return;
  }

  if (state == HostState::RECONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      enterSyncing();
    } else if (now - reconnectStartMs > WIFI_CONNECT_TIMEOUT_MS) {
      LOG_ERR("CLOCK", "Auto-connect timed out");
      disconnectWifi();
      state = HostState::READY;
      lastShownUnit = -1;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      disconnectWifi();
      onGoHome(HomeMenuItem::CLOCK);  // return Home with the Clock item selected
    }
    return;
  }

  if (state == HostState::SYNCING) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
      lastSyncMs = now;
      timeSynced = true;
      lastShownUnit = -1;
      disconnectWifi();
      state = HostState::READY;
      requestUpdate();
    } else if (now - syncStartMs > NTP_TIMEOUT_MS) {
      LOG_ERR("CLOCK", "NTP sync timed out");
      disconnectWifi();
      state = HostState::READY;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      disconnectWifi();
      onGoHome(HomeMenuItem::CLOCK);  // return Home with the Clock item selected
    }
    return;
  }

  // --- READY: sub-app carousel ---
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome(HomeMenuItem::CLOCK);  // return Home with the Clock item selected
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    switchSubApp(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    switchSubApp(+1);
    return;
  }

  switch (subApp) {
    case clocksub::CLOCK:
      tickClock(now);
      break;
    case clocksub::WORLD_CLOCK:
      tickWorldClock(now);
      break;
    case clocksub::STOPWATCH:
      tickStopwatch(now);
      break;
    case clocksub::TIMER:
      tickTimer(now);
      break;
  }
}

// ===========================================================================
// Rendering
// ===========================================================================

void ClockActivity::renderSubApp(int w, int h) {
  const bool use24h = (SETTINGS.clockFormat != 1);  // clockFormat: 0 = 24h, 1 = 12h

  switch (subApp) {
    case clocksub::CLOCK: {
      struct tm t;
      if (!getDisplayTime(t)) {
        clocksub::drawNeedsSync(renderer, w, h);
        break;
      }
      char dayBuf[16];
      strftime(dayBuf, sizeof(dayBuf), "%A", &t);
      const int hour12 = (t.tm_hour % 12 == 0) ? 12 : t.tm_hour % 12;
      const char* ampm = (t.tm_hour < 12) ? "AM" : "PM";
      const int faceH = barVisible() ? (h - clocksub::BAR_HEIGHT) : h;  // landscape clock uses full height
      clockface::FaceCtx ctx{renderer, w, faceH, t.tm_hour, hour12, t.tm_min, use24h, ampm, dayBuf, &t};
      clockface::dispatchFace(ctx, SETTINGS.clockFaceIndex);
      break;
    }
    case clocksub::WORLD_CLOCK: {
      struct tm localT;
      if (!getDisplayTime(localT)) {
        clocksub::drawNeedsSync(renderer, w, h);
        break;
      }
      clocksub::drawWorldClock(renderer, w, h, use24h, localT, time(nullptr), configuredOffsetMinutes(),
                               world.cityIndex);
      break;
    }
    case clocksub::STOPWATCH:
      clocksub::drawStopwatch(renderer, w, h, stopwatch.elapsed(millis()), stopwatch.running, stopwatch.hasLap,
                              stopwatch.lapTotalMs);
      break;
    case clocksub::TIMER:
      clocksub::drawTimer(renderer, w, h, timer.remaining(millis()), timer.running, timer.finished, flashOn);
      break;
  }
}

void ClockActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int lineH12 = renderer.getLineHeight(UI_12_FONT_ID);

  if (state == HostState::SYNCING || state == HostState::RECONNECTING || state == HostState::SCANNING) {
    renderer.drawIcon(WifiIcon, w - 36, 4, 32, 32);
    renderer.fillRect(w - 34, 40, 6, 6);
    renderer.fillRect(w - 26, 40, 6, 6);
    renderer.fillRect(w - 18, 40, 6, 6);
    if (state == HostState::SYNCING) {
      const int y = (h - lineH12) / 2;
      renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_CLOCK_SYNCING), true);
    }
    renderer.displayBuffer();
    return;
  }

  // Stopwatch/Timer show a button-hint footer aligned to the physical front buttons; reserve
  // that height so the sub-app content and indicator bar sit above it.
  const bool showHints = (subApp == clocksub::STOPWATCH || subApp == clocksub::TIMER);
  const int hintH = showHints ? UITheme::getInstance().getMetrics().buttonHintsHeight : 0;
  const int hAvail = h - hintH;

  renderSubApp(w, hAvail);
  if (barVisible()) clocksub::drawSubAppBar(renderer, w, hAvail, subApp);

  if (subApp == clocksub::STOPWATCH) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", stopwatch.running ? tr(STR_STOP) : tr(STR_START),
                                              stopwatch.running ? tr(STR_LAP) : tr(STR_RESET));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (subApp == clocksub::TIMER) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), timer.running ? tr(STR_STOP) : tr(STR_START),
                                              tr(STR_MINUS_MIN), tr(STR_PLUS_MIN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  // Face indicator overlay (clock sub-app only).
  if (subApp == clocksub::CLOCK && showFaceIndicator) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%u/%u", static_cast<unsigned>(SETTINGS.clockFaceIndex + 1),
             static_cast<unsigned>(CrossPointSettings::CLOCK_FACE_COUNT));
    const int tw = renderer.getTextWidth(UI_12_FONT_ID, buf);
    const int pad = 4;
    const int boxW = tw + pad * 2;
    const int boxH = lineH12 + pad * 2;
    const int x = w - boxW - 6;
    const int y = 6;
    renderer.fillRect(x, y, boxW, boxH, false);
    renderer.drawRect(x, y, boxW, boxH, 1, true);
    renderer.drawText(UI_12_FONT_ID, x + pad, y + pad, buf, true);
  }

  renderer.displayBuffer();
}
