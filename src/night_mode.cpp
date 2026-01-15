#include "night_mode.h"
#include "log.h"
#include "mqtt_client.h"

NightMode nightMode;

void NightMode::begin() {
  prefs_.begin(PREF_NAMESPACE, false);
  enabled_ = prefs_.getBool("enabled", false);
  uint8_t storedEffect = prefs_.getUChar("effect", static_cast<uint8_t>(NightModeEffect::Dim));
  if (storedEffect > static_cast<uint8_t>(NightModeEffect::Dim)) {
    storedEffect = static_cast<uint8_t>(NightModeEffect::Dim);
  }
  effect_ = static_cast<NightModeEffect>(storedEffect);
  dimPercent_ = prefs_.getUChar("dim_pct", 20);
  if (dimPercent_ > 100) dimPercent_ = 100;
  startMinutes_ = prefs_.getUShort("start", 22 * 60);
  if (startMinutes_ >= 24 * 60) startMinutes_ = 22 * 60;
  endMinutes_ = prefs_.getUShort("end", 6 * 60);
  if (endMinutes_ >= 24 * 60) endMinutes_ = 6 * 60;
  prefs_.end();

  overrideMode_ = NightModeOverride::Auto;
  active_ = false;
  scheduleActive_ = false;
  hasValidTime_ = false;
  dirty_ = false;
  lastFlush_ = millis();
}

void NightMode::updateFromTime(const struct tm& timeinfo) {
  uint16_t minutes = static_cast<uint16_t>((timeinfo.tm_hour * 60) + timeinfo.tm_min);
  bool newScheduleActive = computeScheduleActive(minutes);
  bool scheduleChanged = (newScheduleActive != scheduleActive_);
  scheduleActive_ = newScheduleActive;
  hasValidTime_ = true;
  if (scheduleChanged) {
    updateEffectiveState("schedule");
  } else if (overrideMode_ == NightModeOverride::Auto) {
    // In auto mode we still need to keep active state aligned with schedule/enabled/time
    updateEffectiveState(nullptr);
  }
}

void NightMode::markTimeInvalid() {
  if (!hasValidTime_) return;
  hasValidTime_ = false;
  if (overrideMode_ == NightModeOverride::Auto && active_) {
    updateEffectiveState("time-invalid");
  }
}

void NightMode::setEnabled(bool on) {
  if (enabled_ == on) return;
  enabled_ = on;
  markDirty();  // Instead of persistEnabled()
  logInfo(String("🌙 Night mode ") + (enabled_ ? "enabled" : "disabled"));
  updateEffectiveState("enabled");
}

void NightMode::setEffect(NightModeEffect mode) {
  if (effect_ == mode) return;
  effect_ = mode;
  markDirty();  // Instead of persistEffect()
  const char* label = (effect_ == NightModeEffect::Off) ? "off" : "dim";
  logInfo(String("🌙 Night mode effect -> ") + label);
  updateEffectiveState("effect");
}

void NightMode::setDimPercent(uint8_t pct) {
  if (pct > 100) pct = 100;
  if (dimPercent_ == pct) return;
  dimPercent_ = pct;
  markDirty();  // Instead of persistDimPercent()
  logInfo(String("🌙 Night mode dim -> ") + pct + "%");
  publishState();
}

void NightMode::setSchedule(uint16_t startMin, uint16_t endMin) {
  startMin %= (24 * 60);
  endMin %= (24 * 60);
  if (startMinutes_ == startMin && endMinutes_ == endMin) return;
  startMinutes_ = startMin;
  endMinutes_ = endMin;
  markDirty();  // Instead of persistSchedule()
  logInfo(String("🌙 Night schedule -> ") + formatMinutes(startMinutes_) + " - " + formatMinutes(endMinutes_));
  if (overrideMode_ == NightModeOverride::Auto && hasValidTime_) {
    updateEffectiveState("schedule-update");
  }
}

void NightMode::setOverride(NightModeOverride mode) {
  if (overrideMode_ == mode) return;
  overrideMode_ = mode;
  const char* label = "auto";
  if (mode == NightModeOverride::ForceOn) label = "force-on";
  else if (mode == NightModeOverride::ForceOff) label = "force-off";
  logInfo(String("🌙 Night override -> ") + label);
  updateEffectiveState("override");
}

uint8_t NightMode::applyToBrightness(uint8_t baseBrightness) const {
  if (!active_) return baseBrightness;
  if (effect_ == NightModeEffect::Off) {
    return 0;
  }
  uint16_t scaled = static_cast<uint16_t>(baseBrightness) * dimPercent_;
  uint8_t result = static_cast<uint8_t>(scaled / 100);
  if (dimPercent_ > 0 && baseBrightness > 0 && result == 0) {
    result = 1; // avoid rounding to zero when dimming but base is small
  }
  return result;
}

String NightMode::formatMinutes(uint16_t minutes) const {
  minutes %= (24 * 60);
  uint8_t hour = minutes / 60;
  uint8_t minute = minutes % 60;
  char buf[6];
  snprintf(buf, sizeof(buf), "%02u:%02u", hour, minute);
  return String(buf);
}

bool NightMode::parseTimeString(const String& text, uint16_t& minutesOut) {
  String trimmed = text;
  trimmed.trim();
  if (trimmed.length() < 4) return false;
  int colon = trimmed.indexOf(':');
  if (colon <= 0) return false;
  String hStr = trimmed.substring(0, colon);
  String mStr = trimmed.substring(colon + 1);
  if (hStr.length() == 0 || mStr.length() == 0) return false;
  int hour = hStr.toInt();
  int minute = mStr.toInt();
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
  minutesOut = static_cast<uint16_t>(hour * 60 + minute);
  return true;
}

bool NightMode::computeScheduleActive(uint16_t minutes) const {
  if (!enabled_) return false;
  if (startMinutes_ == endMinutes_) {
    return false; // zero-length window
  }
  if (startMinutes_ < endMinutes_) {
    return minutes >= startMinutes_ && minutes < endMinutes_;
  }
  // Wrap around midnight
  return minutes >= startMinutes_ || minutes < endMinutes_;
}

void NightMode::flush() {
  if (!dirty_) return;
  
  prefs_.begin(PREF_NAMESPACE, false);
  prefs_.putBool("enabled", enabled_);
  prefs_.putUChar("effect", static_cast<uint8_t>(effect_));
  prefs_.putUChar("dim_pct", dimPercent_);
  prefs_.putUShort("start", startMinutes_);
  prefs_.putUShort("end", endMinutes_);
  prefs_.end();
  
  dirty_ = false;
  lastFlush_ = millis();
}

void NightMode::loop() {
  if (dirty_ && (millis() - lastFlush_) >= AUTO_FLUSH_DELAY_MS) {
    flush();
  }
}

void NightMode::markDirty() {
  if (!dirty_) {
    dirty_ = true;
    lastFlush_ = millis();
  }
}

void NightMode::updateEffectiveState(const char* reason) {
  bool newActive;
  if (overrideMode_ == NightModeOverride::ForceOn) {
    newActive = true;
  } else if (overrideMode_ == NightModeOverride::ForceOff) {
    newActive = false;
  } else {
    newActive = (enabled_ && hasValidTime_ && scheduleActive_);
  }
  if (newActive == active_) {
    if (reason == nullptr) {
      return;
    }
    // Still publish to keep MQTT consumers updated for dim percent change etc.
    publishState();
    return;
  }
  active_ = newActive;
  const char* label = reason ? reason : "state-change";
  logInfo(String("🌙 Night mode ") + (active_ ? "ACTIVE" : "INACTIVE") + " (" + label + ")");
  publishState();
}

void NightMode::publishState() {
  mqtt_publish_state(true);
}
