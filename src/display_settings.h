#pragma once
#include <Preferences.h>

#include "grid_layout.h"
#include "log.h"

constexpr GridVariant FIRMWARE_DEFAULT_GRID_VARIANT = GridVariant::NL_V4;
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

    // Pick the boot default from the variants actually compiled into this
    // firmware. FIRMWARE_DEFAULT_GRID_VARIANT is the preferred choice when
    // available; otherwise fall back to the first compiled variant. Without
    // this, products that don't include NL_V4 would persist an unreachable
    // ID on first boot and report "No info found for variant ID 3" forever.
    size_t variantCount = 0;
    const GridVariantInfo* variantInfos = getGridVariantInfos(variantCount);
    GridVariant defaultVariant = FIRMWARE_DEFAULT_GRID_VARIANT;
    if (variantCount > 0) {
      bool defaultCompiled = false;
      for (size_t i = 0; i < variantCount; ++i) {
        if (variantInfos[i].variant == defaultVariant) { defaultCompiled = true; break; }
      }
      if (!defaultCompiled) defaultVariant = variantInfos[0].variant;
    }
    const uint8_t defaultVariantId = gridVariantToId(defaultVariant);

    const bool hasGridKey = prefs_.isKey("grid_id");
    // Track whether a grid variant was already stored so we can detect migrations
    if (!initialized_) {
      hasStoredVariant_ = hasGridKey;
    }
    uint8_t storedVariant = prefs_.getUChar("grid_id", defaultVariantId);
    if (!hasGridKey) {
      prefs_.putUChar("grid_id", defaultVariantId);
      storedVariant = defaultVariantId;
    }
    prefs_.end();

    gridVariant_ = gridVariantFromId(storedVariant);
    if (!setActiveGridVariant(gridVariant_)) {
      // Persisted ID isn't compiled into this firmware (e.g. NVS inherited
      // from a different product). Self-heal: use the chosen default and
      // overwrite NVS so the warning doesn't recur.
      logWarn(String("⚠️ Persisted grid ID ") + storedVariant +
              " not compiled in; falling back to ID " + defaultVariantId);
      gridVariant_ = defaultVariant;
      setActiveGridVariant(gridVariant_);
      prefs_.begin("wc_display", false);
      prefs_.putUChar("grid_id", defaultVariantId);
      prefs_.end();
    }

    // Update channel (stable by default)
    prefs_.begin("wc_display", true);
    hasStoredUpdateChannel_ = prefs_.isKey("upd_ch");
    String ch = hasStoredUpdateChannel_ ? prefs_.getString("upd_ch", "stable") : String("stable");
    prefs_.end();
    ch.toLowerCase();
    if (ch != "stable" && ch != "early" && ch != "develop") ch = "stable";
    updateChannel_ = ch;
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
  GridVariant getGridVariant() const { return gridVariant_; }
  uint8_t getGridVariantId() const { return gridVariantToId(gridVariant_); }
  bool hasPersistedGridVariant() const { return hasStoredVariant_; }

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

  void setGridVariant(GridVariant variant) {
    if (!setActiveGridVariant(variant)) {
      return;
    }
    if (gridVariant_ == variant) return;
    gridVariant_ = variant;
    markDirty();
  }

  void setGridVariantById(uint8_t id) {
    size_t count = 0;
    const GridVariantInfo* infos = getGridVariantInfos(count);
    for (size_t i = 0; i < count; ++i) {
      if (gridVariantToId(infos[i].variant) == id) {
        setGridVariant(infos[i].variant);
        return;
      }
    }
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
    prefs_.putUChar("grid_id", gridVariantToId(gridVariant_));
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
  GridVariant gridVariant_ = FIRMWARE_DEFAULT_GRID_VARIANT;
  bool hasStoredVariant_ = false;
  bool hasStoredUpdateChannel_ = false;
  bool initialized_ = false;
  bool dirty_ = false;
  unsigned long lastFlush_ = 0;
  
  Preferences prefs_;
  
  static const unsigned long AUTO_FLUSH_DELAY_MS = 5000;  // 5 seconds
};

extern DisplaySettings displaySettings;
