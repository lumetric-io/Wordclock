#ifndef CLOCK_DISPLAY_H
#define CLOCK_DISPLAY_H

#include <time.h>
#include <vector>
#include "time_mapper.h"
#include "display_settings.h"

/**
 * @brief Manages word clock display state and animation
 * 
 * Encapsulates the complex state machine for:
 * - Time synchronization and caching
 * - Animation state management
 * - HET IS visibility timing
 * - LED display updates
 * 
 * This class refactors the 196-line wordclock_loop() into manageable,
 * testable components with clear responsibilities.
 */
class ClockDisplay {
public:
    ClockDisplay();
    
    /**
     * @brief Update display (call every 50ms from main loop)
     * @return true if clock is active, false if disabled/incomplete
     */
    bool update();
    
    /**
     * @brief Force animation for specific time (testing)
     * @param time The time to animate
     */
    void forceAnimationForTime(const struct tm& time);
    
    /**
     * @brief Reset display state to initial conditions
     */
    void reset();
    
    // ========================================================================
    // Public Static Helper Methods (useful for testing and external use)
    // ========================================================================
    
    static bool isHetIs(const WordSegment& seg);
    static void stripHetIsIfDisabled(std::vector<WordSegment>& segs, uint16_t hetIsDurationSec);
    static std::vector<uint16_t> flattenSegments(const std::vector<WordSegment>& segs);
    static const WordSegment* findSegment(const std::vector<WordSegment>& segs, const char* key);
    static void removeLeds(std::vector<uint16_t>& base, const std::vector<uint16_t>& toRemove);
    static bool hetIsCurrentlyVisible(uint16_t hetIsDurationSec, unsigned long hetIsVisibleUntil, unsigned long nowMs);
    static void buildClassicFrames(const std::vector<WordSegment>& segs, std::vector<std::vector<uint16_t>>& frames);
    
private:
    // State management structures
    struct AnimationState {
        bool active = false;
        unsigned long lastStepAt = 0;
        int currentStep = 0;
        std::vector<std::vector<uint16_t>> frames;
    };
    
    struct TimeState {
        struct tm cached = {};
        bool valid = false;
        unsigned long lastFetchMs = 0;
        int lastRoundedMinute = -1;
    };
    
    struct HetIsState {
        unsigned long visibleUntil = 0;
        bool lastHidden = false;  // Track state changes for logging
    };
    
    struct NoTimeIndicatorState {
        unsigned long startMs = 0;
        std::vector<uint16_t> leds;
    };
    
    // Member variables
    AnimationState animation_;
    TimeState time_;
    HetIsState hetIs_;
    NoTimeIndicatorState noTimeIndicator_;
    
    std::vector<WordSegment> lastSegments_;
    std::vector<WordSegment> targetSegments_;
    
    bool forceAnimation_ = false;
    struct tm forcedTime_ = {};
    bool loggedInitialTimeFailure_ = false;
    
    // Helper struct for display time calculation
    struct DisplayTime {
        struct tm effective;
        int rounded;
        int extra;
    };
    
    // Extracted methods - Preconditions
    bool checkClockEnabled();
    
    // Extracted methods - Time management
    bool updateTimeCache(unsigned long nowMs);
    void handleNoTime(unsigned long nowMs);
    void showNoTimeIndicator(unsigned long nowMs);
    void resetNoTimeIndicator();
    void ensureNoTimeIndicatorLeds();
    
    // Extracted methods - Time calculation
    DisplayTime prepareDisplayTime();
    
    // Extracted methods - Animation
    void triggerAnimationIfNeeded(const DisplayTime& dt, unsigned long nowMs);
    void buildAnimationFrames(const DisplayTime& dt, unsigned long nowMs);
    void executeAnimationStep(unsigned long nowMs);
    
    // Extracted methods - Static display
    void displayStaticTime(const DisplayTime& dt);
    bool shouldHideHetIs(unsigned long nowMs);
    void updateHetIsVisibility(unsigned long nowMs);
};

// Global instance
extern ClockDisplay clockDisplay;

#endif // CLOCK_DISPLAY_H
