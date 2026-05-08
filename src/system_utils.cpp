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
#endif

void safeRestart() {
#ifndef WORDCLOCK_BOOTSTRAP
  flushAllSettings();
#endif
  delay(100);  // Allow flash write to complete
  ESP.restart();
}
