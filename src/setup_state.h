#pragma once

#include <Preferences.h>

// Tracks whether the initial setup wizard has been completed.
class SetupState {
public:
  void begin(bool hasLegacyConfig);
  void markComplete();
  void reset();

  bool isComplete() const { return completed_; }
  uint8_t getVersion() const { return version_; }
  bool wasMigrated() const { return migratedFromLegacy_; }

  /**
   * @brief Force immediate write to persistent storage
   * @note Call before critical operations (OTA, deep sleep, restart)
   */
  void flush();

  /**
   * @brief Automatic flush if dirty and sufficient time passed
   * @note Call periodically from main loop (every 1-5 seconds)
   */
  void loop();

  // Query persistence state
  bool isDirty() const { return dirty_; }
  unsigned long millisSinceLastFlush() const { 
    return millis() - lastFlush_; 
  }

private:
  void persist();
  void markDirty();

  Preferences prefs_;
  bool completed_ = false;
  uint8_t version_ = 0;
  bool migratedFromLegacy_ = false;
  bool dirty_ = false;
  unsigned long lastFlush_ = 0;
  
  static const unsigned long AUTO_FLUSH_DELAY_MS = 5000;  // 5 seconds
};

extern SetupState setupState;
