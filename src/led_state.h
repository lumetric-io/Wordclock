#ifndef LED_STATE_H
#define LED_STATE_H

#include <Preferences.h>

class LedState {
public:
    /**
     * @brief Initialize LED state from persistent storage
     * @note Call once during setup()
     */
    void begin() {
        prefs_.begin("wc_led", false);  // Note: renamed namespace for safety
        red_   = prefs_.getUChar("r", 0);
        green_ = prefs_.getUChar("g", 0);
        blue_  = prefs_.getUChar("b", 0);
        white_ = prefs_.getUChar("w", 255);
        brightness_ = prefs_.getUChar("br", 64);
        prefs_.end();
        
        dirty_ = false;
        lastFlush_ = millis();
    }

    /**
     * @brief Set RGB color (immediate in-memory, deferred persistence)
     * @param r Red component (0-255)
     * @param g Green component (0-255)
     * @param b Blue component (0-255)
     */
    void setRGB(uint8_t r, uint8_t g, uint8_t b) {
        // Early exit if no change
        if (red_ == r && green_ == g && blue_ == b) {
            return;
        }
        
        red_ = r; 
        green_ = g; 
        blue_ = b;
        white_ = (r == 255 && g == 255 && b == 255) ? 255 : 0;
        if (white_) { 
            red_ = green_ = blue_ = 0; 
        }
        
        markDirty();  // Flag for later persistence
    }

    /**
     * @brief Set brightness (immediate in-memory, deferred persistence)
     * @param b Brightness value (0-255)
     */
    void setBrightness(uint8_t b) {
        if (brightness_ == b) return;
        brightness_ = b;
        markDirty();
    }

    /**
     * @brief Force immediate write to persistent storage
     * @note Call before critical operations (OTA, deep sleep, restart)
     */
    void flush() {
        if (!dirty_) return;
        
        prefs_.begin("wc_led", false);
        prefs_.putUChar("r", red_);
        prefs_.putUChar("g", green_);
        prefs_.putUChar("b", blue_);
        prefs_.putUChar("w", white_);
        prefs_.putUChar("br", brightness_);
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

    // Getters (unchanged)
    uint8_t getBrightness() const { return brightness_; }
    void getRGBW(uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &w) const {
        r = red_; g = green_; b = blue_; w = white_;
    }
    
    // New: Query persistence state
    bool isDirty() const { return dirty_; }
    unsigned long millisSinceLastFlush() const { 
        return millis() - lastFlush_; 
    }

private:
    void markDirty() {
        if (!dirty_) {
            dirty_ = true;
            lastFlush_ = millis();  // Track when change occurred
        }
    }

    uint8_t red_ = 0, green_ = 0, blue_ = 0, white_ = 255;
    uint8_t brightness_ = 64;
    bool dirty_ = false;
    unsigned long lastFlush_ = 0;
    
    Preferences prefs_;
    
    static const unsigned long AUTO_FLUSH_DELAY_MS = 5000;  // 5 seconds
};

extern LedState ledState;

#endif // LED_STATE_H
