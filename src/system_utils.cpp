#include "system_utils.h"

#include <Arduino.h>

#include "display_settings.h"
#include "led_state.h"
#include "log.h"
#include "night_mode.h"
#include "setup_state.h"

void flushAllSettings() {
  logDebug("Flushing all settings to persistent storage...");
  ledState.flush();
  displaySettings.flush();
  nightMode.flush();
  setupState.flush();
  logDebug("Settings flush complete");
}

void safeRestart() {
  flushAllSettings();
  delay(100);  // Allow flash write to complete
  ESP.restart();
}
