#ifndef NIGHT_MODE_H
#define NIGHT_MODE_H

#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

enum class NightModeEffect : uint8_t {
  Off = 0,
  Dim = 1
};

enum class NightModeOverride : uint8_t {
  Auto = 0,
  ForceOn = 1,
  ForceOff = 2
};

class NightMode {
public:
  void begin();

  void updateFromTime(const struct tm& timeinfo);
  void markTimeInvalid();

  bool isEnabled() const { return enabled_; }
  void setEnabled(bool on);

  NightModeEffect getEffect() const { return effect_; }
  void setEffect(NightModeEffect mode);

  uint8_t getDimPercent() const { return dimPercent_; }
  void setDimPercent(uint8_t pct);

  uint16_t getStartMinutes() const { return startMinutes_; }
  uint16_t getEndMinutes() const { return endMinutes_; }
  void setSchedule(uint16_t startMin, uint16_t endMin);

  NightModeOverride getOverride() const { return overrideMode_; }
  void setOverride(NightModeOverride mode);

  bool isActive() const { return active_; }
  bool isScheduleActive() const { return scheduleActive_; }
  bool hasTime() const { return hasValidTime_; }

  uint8_t applyToBrightness(uint8_t baseBrightness) const;

  String formatMinutes(uint16_t minutes) const;
  static bool parseTimeString(const String& text, uint16_t& minutesOut);

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
  static constexpr const char* PREF_NAMESPACE = "wc_night";  // Renamed namespace

  bool computeScheduleActive(uint16_t minutes) const;
  void markDirty();
  void updateEffectiveState(const char* reason);
  void publishState();

  Preferences prefs_;
  bool enabled_ = false;
  NightModeEffect effect_ = NightModeEffect::Dim;
  uint8_t dimPercent_ = 20;
  uint16_t startMinutes_ = 22 * 60;
  uint16_t endMinutes_ = 6 * 60;
  NightModeOverride overrideMode_ = NightModeOverride::Auto;
  bool active_ = false;
  bool scheduleActive_ = false;
  bool hasValidTime_ = false;
  bool dirty_ = false;
  unsigned long lastFlush_ = 0;
  
  static const unsigned long AUTO_FLUSH_DELAY_MS = 5000;  // 5 seconds
};

extern NightMode nightMode;

#endif // NIGHT_MODE_H
