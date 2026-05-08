#include "system_utils.h"

#include <Arduino.h>
#include "log.h"

#ifndef WORDCLOCK_BOOTSTRAP
// Per-device build: pulls in the runtime singletons whose state we flush.
// Bootstrap firmware has no settings to persist, so these headers and the
// flush helper are excluded entirely to keep the bootstrap link minimal.
#include "display_settings.h"
#include "led_state.h"
#include "night_mode.h"

void flushAllSettings() {
  logDebug("Flushing all settings to persistent storage...");
  ledState.flush();
  displaySettings.flush();
  nightMode.flush();
  logDebug("Settings flush complete");
}
#else
#include <WiFi.h>
#endif

void safeRestart() {
#ifndef WORDCLOCK_BOOTSTRAP
  flushAllSettings();
#else
  // Belt-and-suspenders for the persistent(false) call in bootstrap_main:
  // on the way out, explicitly erase any Wi-Fi credentials that may have
  // landed in nvs.net80211 (e.g. from prior firmwares before persistence
  // was disabled). The per-device firmware that boots next must enroll
  // fresh — bootstrap creds must never carry over.
  WiFi.disconnect(true /* wifioff */, true /* eraseAP */);
#endif
  delay(100);  // Allow flash write to complete
  ESP.restart();
}
