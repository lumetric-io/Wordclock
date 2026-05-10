#pragma once
#include <Preferences.h>

#include "log.h"

enum class WordAnimationMode : uint8_t { Classic = 0 };

class DisplaySettings {
public:
  void begin() {
    prefs_.begin("wc_display", false);  // Note: renamed namespace for safety
    hetIsDurationSec_ = prefs_.getUShort("his_sec", 360); // default ALWAYS (360s)
    if (hetIsDurationSec_ > 360) hetIsDurationSec_ = 360;
    sellMode_ = prefs_.getBool("sell_on", false);
    animateWords_ = prefs_.getBool("anim_on", false); // default OFF unless enabled via UI
    animationMode_ = WordAnimationMode::Classic; // Only Classic mode available

    autoUpdate_ = prefs_.getBool("auto_upd", true);

    // Legacy NVS hygiene: older firmwares persisted a runtime grid_id under
    // this namespace. The grid is now compile-time only (one variant per
    // product, see grid_layout.cpp::GRID_VARIANTS), so the key is dead and
    // removed if present. Safe to call when the key doesn't exist.
    if (prefs_.isKey("grid_id")) {
      prefs_.remove("grid_id");
    }
    prefs_.end();

    // Update channel: persisted user choice wins. On first boot (no
    // stored value yet) derive the default from FIRMWARE_VERSION rather
    // than hardcoding "stable" — otherwise a freshly-flashed early/develop
    // firmware would immediately auto-OTA back to stable on first WiFi
    // connect (isVersionNewer treats "26.5.10" as newer than
    // "26.5.10-rc.3" because the suffix differs). The user can still
    // change the channel via the admin UI; once stored, that choice
    // survives reboots and any future re-flash leaves it untouched.
    prefs_.begin("wc_display", true);
    hasStoredUpdateChannel_ = prefs_.isKey("upd_ch");
    const String buildChannel = detectBuildChannel();
    String ch = hasStoredUpdateChannel_
                  ? prefs_.getString("upd_ch", buildChannel)
                  : buildChannel;
    prefs_.end();
    ch.toLowerCase();
    if (ch != "stable" && ch != "early" && ch != "develop") ch = "stable";
    updateChannel_ = ch;
    if (!hasStoredUpdateChannel_) {
      logInfo(String("ℹ️ No stored update channel; defaulting to build channel '") +
              updateChannel_ + "'");
    }
    if (updateChannel_ == "develop" && autoUpdate_) {
      autoUpdate_ = false;
      prefs_.begin("wc_display", false);
      prefs_.putBool("auto_upd", autoUpdate_);
      prefs_.end();
      logInfo("🔁 Automatic updates disabled for develop channel");
    }
    initialized_ = true;

    dirty_ = false;
    lastFlush_ = millis();
  }

  uint16_t getHetIsDurationSec() const { return hetIsDurationSec_; }
  bool isSellMode() const { return sellMode_; }
  bool getAnimateWords() const { return animateWords_; }
  WordAnimationMode getAnimationMode() const { return animationMode_; }
  uint8_t getAnimationModeId() const { return static_cast<uint8_t>(animationMode_); }
  bool getAutoUpdate() const { return autoUpdate_; }
  String getUpdateChannel() const { return updateChannel_; }
  bool hasStoredChannel() const { return hasStoredUpdateChannel_; }

  void setHetIsDurationSec(uint16_t s) {
    if (s > 360) s = 360;
    if (hetIsDurationSec_ == s) return;
    hetIsDurationSec_ = s;
    markDirty();
  }

  void setSellMode(bool on) {
    if (sellMode_ == on) return;
    sellMode_ = on;
    markDirty();
  }

  void setAnimateWords(bool on) {
    if (animateWords_ == on) return;
    animateWords_ = on;
    markDirty();
  }

  void setAnimationMode(WordAnimationMode mode) {
    // Only Classic mode is supported
    animationMode_ = WordAnimationMode::Classic;
  }

  void setAnimationModeById(uint8_t id) {
    // Only Classic mode is supported
    animationMode_ = WordAnimationMode::Classic;
  }

  void setAutoUpdate(bool on) {
    if (autoUpdate_ == on) return;
    autoUpdate_ = on;
    markDirty();
  }

  void setUpdateChannel(const String& channel) {
    String ch = channel;
    ch.toLowerCase();
    if (ch != "stable" && ch != "early" && ch != "develop") ch = "stable"; // default/fallback
    if (ch == updateChannel_) return;
    updateChannel_ = ch;
    markDirty();
    logInfo(String("🔀 Update channel set to ") + updateChannel_);
    if (updateChannel_ == "develop" && autoUpdate_) {
      autoUpdate_ = false;
      markDirty();
      logInfo("🔁 Automatic updates disabled for develop channel");
    }
  }

  void resetUpdateChannel() {
    setUpdateChannel("stable");
    hasStoredUpdateChannel_ = false;
  }

  /**
   * @brief Force immediate write to persistent storage
   * @note Call before critical operations (OTA, deep sleep, restart)
   */
  void flush() {
    if (!dirty_) return;

    prefs_.begin("wc_display", false);
    // Batch write all settings
    prefs_.putUShort("his_sec", hetIsDurationSec_);
    prefs_.putBool("sell_on", sellMode_);
    prefs_.putBool("anim_on", animateWords_);
    prefs_.putUChar("anim_mode", static_cast<uint8_t>(animationMode_));
    prefs_.putBool("auto_upd", autoUpdate_);
    prefs_.putString("upd_ch", updateChannel_);
    prefs_.end();

    dirty_ = false;
    lastFlush_ = millis();
  }

  /**
   * @brief Automatic flush if dirty and sufficient time passed
   * @note Call periodically from main loop (every 1-5 seconds)
   */
  void loop() {
    if (dirty_ && (millis() - lastFlush_) >= AUTO_FLUSH_DELAY_MS) {
      flush();
    }
  }

  // Query persistence state
  bool isDirty() const { return dirty_; }
  unsigned long millisSinceLastFlush() const {
    return millis() - lastFlush_;
  }

private:
  // Map FIRMWARE_VERSION → channel name. The release pipeline
  // (tools/release.sh) enforces:
  //   develop builds → "*-dev.X" or "*-dev"
  //   early builds   → "*-rc.X"  or "*-early"
  //   stable builds  → no pre-release suffix
  // Used as the first-boot fallback for the persisted update channel.
  static String detectBuildChannel() {
    String v(FIRMWARE_VERSION);
    v.toLowerCase();
    if (v.indexOf("-dev") >= 0) return String("develop");
    if (v.indexOf("-rc") >= 0 || v.indexOf("-early") >= 0) return String("early");
    return String("stable");
  }

  void markDirty() {
    if (!dirty_) {
      dirty_ = true;
      lastFlush_ = millis();
    }
  }

  uint16_t hetIsDurationSec_ = 360; // default ALWAYS
  bool sellMode_ = false;
  bool animateWords_ = false; // default OFF
  WordAnimationMode animationMode_ = WordAnimationMode::Classic;
  bool autoUpdate_ = true;    // default ON to keep current behavior
  String updateChannel_ = "stable";
  bool hasStoredUpdateChannel_ = false;
  bool initialized_ = false;
  bool dirty_ = false;
  unsigned long lastFlush_ = 0;

  Preferences prefs_;

  static const unsigned long AUTO_FLUSH_DELAY_MS = 5000;  // 5 seconds
};

extern DisplaySettings displaySettings;
