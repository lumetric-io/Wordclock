#include "clock_display.h"
#include "led_controller.h"
#include "led_state.h"
#include "setup_state.h"
#include "night_mode.h"
#include "time_sync.h"
#include "grid_layout.h"
#include "log.h"
#include <algorithm>
#include <cstring>
#include <map>

// Global instance
ClockDisplay clockDisplay;

// External references
extern bool clockEnabled;
extern bool g_initialTimeSyncSucceeded;

// ============================================================================
// Constructor and Lifecycle
// ============================================================================

ClockDisplay::ClockDisplay() {
    reset();
}

void ClockDisplay::reset() {
    animation_ = AnimationState();
    time_ = TimeState();
    hetIs_ = HetIsState();
    noTimeIndicator_ = NoTimeIndicatorState();
    lastSegments_.clear();
    targetSegments_.clear();
    forceAnimation_ = false;
    loggedInitialTimeFailure_ = false;
}

// ============================================================================
// Main Update Loop
// ============================================================================

bool ClockDisplay::update() {
    unsigned long nowMs = millis();
    
    // Check preconditions
    if (!checkClockEnabled()) {
        return false;
    }
    
    // Update time cache
    if (!updateTimeCache(nowMs)) {
        handleNoTime(nowMs);
        return false;
    }
    
    // Prepare display time (with sell mode override)
    DisplayTime dt = prepareDisplayTime();
    
    // Check if we need to start animation
    triggerAnimationIfNeeded(dt, nowMs);
    
    // Execute animation or display static
    if (animation_.active) {
        executeAnimationStep(nowMs);
    } else {
        displayStaticTime(dt);
    }
    
    return true;
}

// ============================================================================
// Precondition Checks
// ============================================================================

bool ClockDisplay::checkClockEnabled() {
    if (!clockEnabled) {
        animation_.active = false;
        showLeds({});
        resetNoTimeIndicator();
        return false;
    }
    
    if (!setupState.isComplete()) {
        animation_.active = false;
        showLeds({});
        resetNoTimeIndicator();
        return false;
    }
    
    return true;
}

// ============================================================================
// Time Management
// ============================================================================

bool ClockDisplay::updateTimeCache(unsigned long nowMs) {
    // Refresh cached time at most once per second
    if (!time_.valid || (nowMs - time_.lastFetchMs) >= 1000UL) {
        struct tm t = {};
        if (getLocalTime(&t)) {
            time_.cached = t;
            time_.valid = true;
            time_.lastFetchMs = nowMs;
            g_initialTimeSyncSucceeded = true;
            loggedInitialTimeFailure_ = false;
            nightMode.updateFromTime(time_.cached);
            resetNoTimeIndicator();
            return true;
        } else if (!time_.valid) {
            // First failure - no cached time available
            return false;
        }
    }
    
    return time_.valid;
}

void ClockDisplay::handleNoTime(unsigned long nowMs) {
    if (!loggedInitialTimeFailure_) {
        logWarn("❗ Unable to fetch time; showing no-time indicator");
        loggedInitialTimeFailure_ = true;
    }
    nightMode.markTimeInvalid();
    showNoTimeIndicator(nowMs);
}

void ClockDisplay::ensureNoTimeIndicatorLeds() {
    if (!noTimeIndicator_.leds.empty()) return;
    size_t count = EXTRA_MINUTE_LED_COUNT >= 4 ? 4 : EXTRA_MINUTE_LED_COUNT;
    for (size_t i = 0; i < count; ++i) {
        noTimeIndicator_.leds.push_back(EXTRA_MINUTE_LEDS[i]);
    }
}

void ClockDisplay::showNoTimeIndicator(unsigned long nowMs) {
    ensureNoTimeIndicatorLeds();
    if (noTimeIndicator_.startMs == 0) {
        noTimeIndicator_.startMs = nowMs;
    }
    const unsigned long elapsed = nowMs - noTimeIndicator_.startMs;
    const unsigned long phase = elapsed % 5000UL; // 5 second cycle
    if (phase < 500UL) {
        showLeds(noTimeIndicator_.leds);
    } else {
        showLeds({});
    }
}

void ClockDisplay::resetNoTimeIndicator() {
    noTimeIndicator_.startMs = 0;
    noTimeIndicator_.leds.clear();
}

ClockDisplay::DisplayTime ClockDisplay::prepareDisplayTime() {
    ClockDisplay::DisplayTime dt;
    
    // Use cached time
    dt.effective = time_.cached;
    
    // Apply sell-mode override (forces 10:47)
    if (displaySettings.isSellMode()) {
        dt.effective.tm_hour = 10;
        dt.effective.tm_min = 47;
    }
    
    dt.rounded = (dt.effective.tm_min / 5) * 5;
    dt.extra = dt.effective.tm_min % 5;
    
    return dt;
}

// ============================================================================
// Animation Management
// ============================================================================

void ClockDisplay::triggerAnimationIfNeeded(const DisplayTime& dt, unsigned long nowMs) {
    // Start animation when the rounded bucket changes or when forced externally
    bool shouldAnimate = (dt.rounded != time_.lastRoundedMinute || forceAnimation_);
    
    if (shouldAnimate) {
        time_.lastRoundedMinute = dt.rounded;
        
        struct tm animTime = forceAnimation_ ? forcedTime_ : dt.effective;
        buildAnimationFrames(DisplayTime{animTime, dt.rounded, dt.extra}, nowMs);
        
        forceAnimation_ = false;
    }
}

void ClockDisplay::buildAnimationFrames(const DisplayTime& dt, unsigned long nowMs) {
    struct tm effectiveTime = dt.effective;
    targetSegments_ = get_word_segments_with_keys(&effectiveTime);
    
    uint16_t hisSec = displaySettings.getHetIsDurationSec();
    stripHetIsIfDisabled(targetSegments_, hisSec);
    
    bool animate = displaySettings.getAnimateWords();
    
    if (animate) {
        buildClassicFrames(targetSegments_, animation_.frames);
        
        // Add extra minute LEDs to final frame
        if (!animation_.frames.empty() && dt.extra > 0) {
#if SUPPORT_MINUTE_LEDS
            auto& finalFrame = animation_.frames.back();
            for (int i = 0; i < dt.extra && i < 4 && i < static_cast<int>(EXTRA_MINUTE_LED_COUNT); ++i) {
                finalFrame.push_back(EXTRA_MINUTE_LEDS[i]);
            }
#endif
        }
        
        if (!animation_.frames.empty()) {
            animation_.active = true;
            animation_.currentStep = 0;
            animation_.lastStepAt = millis();
            hetIs_.visibleUntil = 0;  // Reset; will be set when animation completes
        } else {
            animation_.active = false;
        }
    } else {
        animation_.active = false;
    }
    
    // If not animating, set HET IS visibility immediately
    if (!animation_.active) {
        updateHetIsVisibility(nowMs);
        lastSegments_ = targetSegments_;
    }
}

void ClockDisplay::executeAnimationStep(unsigned long nowMs) {
    unsigned long deltaMs = (animation_.currentStep == 0) ? 0 : (nowMs - animation_.lastStepAt);
    
    // Fixed animation speed: 500ms per frame
    const uint16_t frameDelayMs = 500;
    
    if (animation_.currentStep == 0 || deltaMs >= frameDelayMs) {
        if (animation_.currentStep < (int)animation_.frames.size()) {
            const auto& frame = animation_.frames[animation_.currentStep];
            
            // Logging
            size_t prevSize = (animation_.currentStep == 0) ? 0 
                            : animation_.frames[animation_.currentStep - 1].size();
            int stepIndex = animation_.currentStep; // capture before increment
            animation_.currentStep++;
            
            String msg = "Anim step ";
            msg += (stepIndex + 1);
            msg += "/";
            msg += animation_.frames.size();
            msg += " dt=";
            msg += deltaMs;
            msg += "ms (Δ";
            msg += (int)frame.size() - (int)prevSize;
            msg += " leds)";
            // Warn if actual delay is > 20% longer than configured delay
            uint16_t thresholdMs = frameDelayMs + (frameDelayMs / 5); // frameDelayMs * 1.2
            if (deltaMs > thresholdMs) {
                msg += " ⚠️ slow";
                logWarn(msg);
            } else {
                logDebug(msg);
            }
            
            // Instant display (no fade effects)
            showLeds(frame);
            
            animation_.lastStepAt = nowMs;
        }
        
        if (animation_.currentStep >= (int)animation_.frames.size()) {
            animation_.active = false;
            updateHetIsVisibility(nowMs);
            lastSegments_ = targetSegments_;
        }
    } else if (animation_.currentStep > 0 && animation_.currentStep <= (int)animation_.frames.size()) {
        // Re-display current frame (called between animation steps)
        showLeds(animation_.frames[animation_.currentStep - 1]);
    }
}

// ============================================================================
// Static Display
// ============================================================================

void ClockDisplay::displayStaticTime(const DisplayTime& dt) {
    struct tm effectiveTime = dt.effective;
    std::vector<WordSegment> baseSegs = get_word_segments_with_keys(&effectiveTime);
    uint16_t hisSec = displaySettings.getHetIsDurationSec();
    stripHetIsIfDisabled(baseSegs, hisSec);
    
    std::vector<uint16_t> indices;
    bool hideHetIs = shouldHideHetIs(millis());
    
    // Track state changes for logging
    if (hideHetIs && !hetIs_.lastHidden) {
        // 'HET IS' hidden after configured duration (optional debug log)
    }
    hetIs_.lastHidden = hideHetIs;
    
    for (const auto& seg : baseSegs) {
        if (hideHetIs && isHetIs(seg)) continue;
        indices.insert(indices.end(), seg.leds.begin(), seg.leds.end());
    }
    
    // Add extra minute LEDs
#if SUPPORT_MINUTE_LEDS
    for (int i = 0; i < dt.extra && i < 4 && i < static_cast<int>(EXTRA_MINUTE_LED_COUNT); ++i) {
        indices.push_back(EXTRA_MINUTE_LEDS[i]);
    }
#endif
    
    showLeds(indices);
    
    lastSegments_ = baseSegs;
}

bool ClockDisplay::shouldHideHetIs(unsigned long nowMs) {
    uint16_t hisSec = displaySettings.getHetIsDurationSec();
    
    if (hisSec == 0) return true;              // Never show
    if (hisSec >= 360) return false;           // Always show
    
    return (hetIs_.visibleUntil != 0) && (nowMs >= hetIs_.visibleUntil);
}

void ClockDisplay::updateHetIsVisibility(unsigned long nowMs) {
    uint16_t hisSec = displaySettings.getHetIsDurationSec();
    
    if (hisSec >= 360) {
        hetIs_.visibleUntil = 0;  // Always on
    } else if (hisSec == 0) {
        hetIs_.visibleUntil = 1;  // Hidden immediately
    } else {
        hetIs_.visibleUntil = nowMs + (unsigned long)hisSec * 1000UL;
    }
}

// ============================================================================
// Public API
// ============================================================================

void ClockDisplay::forceAnimationForTime(const struct tm& time) {
    forcedTime_ = time;
    forceAnimation_ = true;
}

// ============================================================================
// Helper Methods (static)
// ============================================================================

bool ClockDisplay::isHetIs(const WordSegment& seg) {
    return strcmp(seg.key, "HET") == 0 || strcmp(seg.key, "IS") == 0;
}

void ClockDisplay::stripHetIsIfDisabled(std::vector<WordSegment>& segs, uint16_t hetIsDurationSec) {
    if (hetIsDurationSec != 0) return;
    segs.erase(std::remove_if(segs.begin(), segs.end(), 
                             [](const WordSegment& s) { return isHetIs(s); }), 
               segs.end());
}

std::vector<uint16_t> ClockDisplay::flattenSegments(const std::vector<WordSegment>& segs) {
    std::vector<uint16_t> indices;
    for (const auto& seg : segs) {
        indices.insert(indices.end(), seg.leds.begin(), seg.leds.end());
    }
    return indices;
}

const WordSegment* ClockDisplay::findSegment(const std::vector<WordSegment>& segs, const char* key) {
    for (const auto& seg : segs) {
        if (strcmp(seg.key, key) == 0) return &seg;
    }
    return nullptr;
}

void ClockDisplay::removeLeds(std::vector<uint16_t>& base, const std::vector<uint16_t>& toRemove) {
    base.erase(std::remove_if(base.begin(), base.end(), [&](uint16_t idx) {
        return std::find(toRemove.begin(), toRemove.end(), idx) != toRemove.end();
    }), base.end());
}

bool ClockDisplay::hetIsCurrentlyVisible(uint16_t hetIsDurationSec, unsigned long hetIsVisibleUntil, unsigned long nowMs) {
    if (hetIsDurationSec == 0) return false;
    if (hetIsDurationSec >= 360) return true;
    if (hetIsVisibleUntil == 0) return true;
    return nowMs < hetIsVisibleUntil;
}

void ClockDisplay::buildClassicFrames(const std::vector<WordSegment>& segs, 
                                     std::vector<std::vector<uint16_t>>& frames) {
    frames.clear();
    std::vector<uint16_t> cumulative;
    for (const auto& seg : segs) {
        cumulative.insert(cumulative.end(), seg.leds.begin(), seg.leds.end());
        frames.push_back(cumulative);
    }
}

