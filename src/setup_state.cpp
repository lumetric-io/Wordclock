#include "setup_state.h"

#include "log.h"

namespace {
constexpr uint8_t SETUP_STATE_VERSION = 1;
constexpr const char* PREF_NAMESPACE = "wc_setup";  // Renamed namespace
}

SetupState setupState;

void SetupState::persist() {
  prefs_.putBool("done", completed_);
  prefs_.putUChar("ver", version_);
}

void SetupState::begin(bool hasLegacyConfig) {
  prefs_.begin(PREF_NAMESPACE, false);
  migratedFromLegacy_ = false;
  const bool hasDoneKey = prefs_.isKey("done");
  const bool hasVerKey = prefs_.isKey("ver");
  completed_ = prefs_.getBool("done", false);
  version_ = prefs_.getUChar("ver", 0);

  const bool firstInit = !hasDoneKey && !hasVerKey;
  if (firstInit) {
    // For devices that already had config (WiFi/grid), assume setup was done.
    if (hasLegacyConfig) {
      completed_ = true;
      migratedFromLegacy_ = true;
      logInfo("ℹ️ Setup state initialized as completed (legacy config detected)");
    } else {
      completed_ = false;
      logInfo("ℹ️ Setup state initialized as pending");
    }
  }

  // Always bump to current schema version
  version_ = SETUP_STATE_VERSION;
  persist();
  prefs_.end();
  
  dirty_ = false;
  lastFlush_ = millis();
}

void SetupState::markComplete() {
  if (completed_) return;  // Early exit if no change
  completed_ = true;
  version_ = SETUP_STATE_VERSION;
  markDirty();
  logInfo("✅ Setup marked as complete");
}

void SetupState::reset() {
  if (!completed_) return;  // Early exit if no change
  completed_ = false;
  version_ = SETUP_STATE_VERSION;
  markDirty();
  logInfo("ℹ️ Setup state reset (wizard required)");
}

void SetupState::flush() {
  if (!dirty_) return;
  
  prefs_.begin(PREF_NAMESPACE, false);
  persist();
  prefs_.end();
  
  dirty_ = false;
  lastFlush_ = millis();
}

void SetupState::loop() {
  if (dirty_ && (millis() - lastFlush_) >= AUTO_FLUSH_DELAY_MS) {
    flush();
  }
}

void SetupState::markDirty() {
  if (!dirty_) {
    dirty_ = true;
    lastFlush_ = millis();
  }
}
