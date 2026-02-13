#include "led_events.h"

#include <WiFi.h>
#include <vector>
#include <algorithm>
#include <time.h>

#include "config.h"
#include "led_controller.h"
#include "setup_state.h"

#if LED_STATUS_EVENTS_ENABLED && LED_STATUS_EVENT_USE_MINUTE_LEDS
#include "grid_layout.h"
#endif
#if LED_STATUS_EVENTS_ENABLED && LED_STATUS_EVENT_LED_COUNT > 0 && defined(PRODUCT_VARIANT_MINI)
#include "time_mapper.h"
#endif

extern bool g_wifiHadCredentialsAtBoot;

namespace {

struct BlinkState {
  bool on = false;
  unsigned long lastToggleMs = 0;
  uint8_t blinkCount = 0;
  unsigned long pauseUntilMs = 0;
};

struct EventState {
  bool active = false;
};

EventState g_eventStates[8];
bool g_pulseFirmwareCheck = false;
LedEvent g_currentEvent = LedEvent::FirmwareCheck;
BlinkState g_eventBlinkState;
static const uint8_t kBlinkScale = 13; // ~5% of 255

#if LED_STATUS_EVENTS_ENABLED && LED_STATUS_EVENT_USE_MINUTE_LEDS
static std::vector<uint16_t> g_minuteEventLedVec;
static const std::vector<uint16_t>& getEventLedVector() {
  if (EXTRA_MINUTE_LED_COUNT == 0 || EXTRA_MINUTE_LEDS == nullptr) {
    g_minuteEventLedVec.clear();
    return g_minuteEventLedVec;
  }
  g_minuteEventLedVec.assign(EXTRA_MINUTE_LEDS, EXTRA_MINUTE_LEDS + EXTRA_MINUTE_LED_COUNT);
  return g_minuteEventLedVec;
}
#elif LED_STATUS_EVENTS_ENABLED && LED_STATUS_EVENT_LED_COUNT > 0
static const uint16_t kEventLeds[] = { LED_STATUS_EVENT_LED_IDS };
#if defined(PRODUCT_VARIANT_MINI)
// wordclock-mini: use corner LEDs for events, but skip any corner that is currently lit for the time display
static std::vector<uint16_t> g_miniEventLedVec;
static const std::vector<uint16_t>& getEventLedVector() {
  g_miniEventLedVec.clear();
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) {
    // No time: use all corner LEDs for event feedback
    g_miniEventLedVec.assign(kEventLeds, kEventLeds + LED_STATUS_EVENT_LED_COUNT);
    return g_miniEventLedVec;
  }
  std::vector<uint16_t> timeLeds = get_led_indices_for_time(&timeinfo);
  for (size_t i = 0; i < LED_STATUS_EVENT_LED_COUNT; ++i) {
    uint16_t id = kEventLeds[i];
    if (std::find(timeLeds.begin(), timeLeds.end(), id) == timeLeds.end()) {
      g_miniEventLedVec.push_back(id);
    }
  }
  // If all corners are lit for current time, still show event feedback (e.g. BLE after WiFi reset)
  if (g_miniEventLedVec.empty()) {
    g_miniEventLedVec.assign(kEventLeds, kEventLeds + LED_STATUS_EVENT_LED_COUNT);
  }
  return g_miniEventLedVec;
}
#else
static const std::vector<uint16_t> kEventLedVec(
  kEventLeds,
  kEventLeds + LED_STATUS_EVENT_LED_COUNT
);
static const std::vector<uint16_t>& getEventLedVector() {
  return kEventLedVec;
}
#endif
#else
static const std::vector<uint16_t> kEventLedVec;
static const std::vector<uint16_t>& getEventLedVector() {
  return kEventLedVec;
}
#endif

uint8_t scaleChannel(uint8_t value, uint8_t scaleValue) {
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * scaleValue) / 255);
}

bool runBlinkPattern(unsigned long nowMs,
                     const std::vector<uint16_t>& leds,
                     uint8_t r, uint8_t g, uint8_t b,
                     unsigned long onMs,
                     unsigned long offMs,
                     uint8_t flashes,
                     unsigned long pauseMs,
                     bool repeat,
                     BlinkState& state) {
  if (state.pauseUntilMs != 0) {
    if (nowMs >= state.pauseUntilMs) {
      state.pauseUntilMs = 0;
      state.lastToggleMs = 0;
    } else {
      setLedsColorOverlay(leds, 0, 0, 0, 0);
      return true;
    }
  }

  if (state.lastToggleMs == 0 || nowMs - state.lastToggleMs >= (state.on ? onMs : offMs)) {
    state.on = !state.on;
    state.lastToggleMs = nowMs;
    if (state.on) {
      setLedsColorOverlay(leds, scaleChannel(r, kBlinkScale), scaleChannel(g, kBlinkScale), scaleChannel(b, kBlinkScale), 0);
    } else {
      setLedsColorOverlay(leds, 0, 0, 0, 0);
      state.blinkCount += 1;
      if (state.blinkCount >= flashes) {
        if (repeat) {
          state.blinkCount = 0;
          if (pauseMs > 0) {
            state.pauseUntilMs = nowMs + pauseMs;
          }
        } else {
          state.blinkCount = 0;
          return false;
        }
      }
    }
  }
  return true;
}

bool handleSetupBlink(unsigned long nowMs) {
#if !LED_STATUS_EVENTS_ENABLED
  (void)nowMs;
  return false;
#else
  const std::vector<uint16_t>& eventLeds = getEventLedVector();
  if (eventLeds.empty()) {
    return false;
  }
  static bool lastHasWifiConfig = false;
  static unsigned long greenUntilMs = 0;
  static BlinkState setupBlinkState;

  const bool complete = setupState.isComplete();

  if (complete && setupState.takeCompletionPulse()) {
    greenUntilMs = nowMs + 1000;
    setLedsColorOverlay(eventLeds, 0, scaleChannel(255, kBlinkScale), 0, 0);
    return true;
  }

  if (greenUntilMs != 0) {
    if (nowMs >= greenUntilMs) {
      greenUntilMs = 0;
      setLedsColorOverlay(eventLeds, 0, 0, 0, 0);
      return false;
    }
    return true;
  }

  if (complete) {
    return false;
  }

  const bool hasSavedSsid = WiFi.SSID().length() > 0;
  const bool hasWifiConfig = g_wifiHadCredentialsAtBoot || hasSavedSsid;
  if (hasWifiConfig != lastHasWifiConfig) {
    lastHasWifiConfig = hasWifiConfig;
    setupBlinkState = BlinkState{};
  }

  const uint8_t r = 255;
  const uint8_t g = hasWifiConfig ? 140 : 0;
  const uint8_t b = 0;

  return runBlinkPattern(nowMs, eventLeds, r, g, b,
                         200, 200, 2, 5000, true, setupBlinkState);
#endif
}

LedEvent pickHighestPriorityEvent() {
  if (g_eventStates[static_cast<uint8_t>(LedEvent::BleProvisioning)].active) {
    return LedEvent::BleProvisioning;
  }
  if (g_eventStates[static_cast<uint8_t>(LedEvent::WifiManagerPortal)].active) {
    return LedEvent::WifiManagerPortal;
  }
  if (g_eventStates[static_cast<uint8_t>(LedEvent::FirmwareApplying)].active) {
    return LedEvent::FirmwareApplying;
  }
  if (g_eventStates[static_cast<uint8_t>(LedEvent::FirmwareDownloading)].active) {
    return LedEvent::FirmwareDownloading;
  }
  if (g_eventStates[static_cast<uint8_t>(LedEvent::FirmwareAvailable)].active) {
    return LedEvent::FirmwareAvailable;
  }
  if (g_eventStates[static_cast<uint8_t>(LedEvent::NtpFailed)].active) {
    return LedEvent::NtpFailed;
  }
  if (g_eventStates[static_cast<uint8_t>(LedEvent::MqttDisconnected)].active) {
    return LedEvent::MqttDisconnected;
  }
  if (g_pulseFirmwareCheck) {
    return LedEvent::FirmwareCheck;
  }
  return LedEvent::FirmwareCheck;
}

bool runEventPattern(LedEvent event, unsigned long nowMs) {
  const std::vector<uint16_t>& leds = getEventLedVector();
  switch (event) {
    case LedEvent::BleProvisioning:
      return runBlinkPattern(nowMs, leds, 0, 120, 255, 120, 880, 2, 5000, true, g_eventBlinkState);
    case LedEvent::WifiManagerPortal:
      return runBlinkPattern(nowMs, leds, 160, 0, 200, 150, 150, 2, 2000, true, g_eventBlinkState);
    case LedEvent::FirmwareApplying:
      return runBlinkPattern(nowMs, leds, 255, 255, 255, 100, 100, 2, 1000, true, g_eventBlinkState);
    case LedEvent::FirmwareDownloading:
      return runBlinkPattern(nowMs, leds, 0, 120, 255, 100, 100, 2, 1000, true, g_eventBlinkState);
    case LedEvent::FirmwareAvailable:
      return runBlinkPattern(nowMs, leds, 140, 0, 255, 1000, 1000, 1, 0, true, g_eventBlinkState);
    case LedEvent::NtpFailed:
      return runBlinkPattern(nowMs, leds, 255, 140, 0, 150, 150, 3, 10000, true, g_eventBlinkState);
    case LedEvent::MqttDisconnected:
      return runBlinkPattern(nowMs, leds, 0, 80, 255, 150, 150, 1, 30000, true, g_eventBlinkState);
    case LedEvent::FirmwareCheck: {
      bool active = runBlinkPattern(nowMs, leds, 0, 200, 200, 150, 150, 2, 0, false, g_eventBlinkState);
      if (!active) {
        g_pulseFirmwareCheck = false;
      }
      return true;
    }
    default:
      return false;
  }
}

} // namespace

void ledEventStart(LedEvent event) {
  g_eventStates[static_cast<uint8_t>(event)].active = true;
}

void ledEventStop(LedEvent event) {
  g_eventStates[static_cast<uint8_t>(event)].active = false;
}

void ledEventPulse(LedEvent event) {
  if (event == LedEvent::FirmwareCheck) {
    g_pulseFirmwareCheck = true;
  }
}

bool ledEventsTick(unsigned long nowMs) {
#if !LED_STATUS_EVENTS_ENABLED
  (void)nowMs;
  return false;
#else
  if (!setupState.isComplete() &&
      !g_eventStates[static_cast<uint8_t>(LedEvent::BleProvisioning)].active &&
      !g_eventStates[static_cast<uint8_t>(LedEvent::WifiManagerPortal)].active) {
    return handleSetupBlink(nowMs);
  }

  LedEvent next = pickHighestPriorityEvent();
  bool hasEvent = g_pulseFirmwareCheck ||
                  g_eventStates[static_cast<uint8_t>(LedEvent::BleProvisioning)].active ||
                  g_eventStates[static_cast<uint8_t>(LedEvent::WifiManagerPortal)].active ||
                  g_eventStates[static_cast<uint8_t>(LedEvent::FirmwareApplying)].active ||
                  g_eventStates[static_cast<uint8_t>(LedEvent::FirmwareDownloading)].active ||
                  g_eventStates[static_cast<uint8_t>(LedEvent::FirmwareAvailable)].active ||
                  g_eventStates[static_cast<uint8_t>(LedEvent::NtpFailed)].active ||
                  g_eventStates[static_cast<uint8_t>(LedEvent::MqttDisconnected)].active;

  if (!hasEvent) {
    return handleSetupBlink(nowMs);
  }

  if (next != g_currentEvent) {
    g_currentEvent = next;
    g_eventBlinkState = BlinkState{};
  }

  runEventPattern(next, nowMs);
  return true;
#endif
}

LedEvent ledEventGetCurrent(void) {
  return pickHighestPriorityEvent();
}

bool ledEventIsActive(void) {
#if !LED_STATUS_EVENTS_ENABLED
  return false;
#else
  return g_pulseFirmwareCheck ||
         g_eventStates[static_cast<uint8_t>(LedEvent::BleProvisioning)].active ||
         g_eventStates[static_cast<uint8_t>(LedEvent::WifiManagerPortal)].active ||
         g_eventStates[static_cast<uint8_t>(LedEvent::FirmwareApplying)].active ||
         g_eventStates[static_cast<uint8_t>(LedEvent::FirmwareDownloading)].active ||
         g_eventStates[static_cast<uint8_t>(LedEvent::FirmwareAvailable)].active ||
         g_eventStates[static_cast<uint8_t>(LedEvent::NtpFailed)].active ||
         g_eventStates[static_cast<uint8_t>(LedEvent::MqttDisconnected)].active;
#endif
}
