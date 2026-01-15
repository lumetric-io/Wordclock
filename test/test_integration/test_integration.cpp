#include <gtest/gtest.h>
#include "../mocks/mock_arduino.h"
#include "../mocks/mock_grid_layout.h"
#include "../mocks/mock_time.h"
#include "../mocks/mock_preferences.h"
#include "../helpers/test_utils.h"

// Include production log
#include "../../src/log.cpp"

// Include production code for integration tests
#include "../../src/time_mapper.cpp"
#include "../../src/led_state.h"
#include "../../src/led_state.cpp"

// Mock night mode for integration
#ifndef NIGHT_MODE_H
#define NIGHT_MODE_H
class NightMode {
public:
    void begin() {}
    uint8_t applyToBrightness(uint8_t base) const { return base; }
    bool isActive() const { return false; }
};
NightMode nightMode;
#endif

#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H
static std::vector<uint16_t> lastShownLeds;
void initLeds() { lastShownLeds.clear(); }
void showLeds(const std::vector<uint16_t>& leds) { lastShownLeds = leds; }
const std::vector<uint16_t>& test_getLastShownLeds() { return lastShownLeds; }
#endif

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        Preferences::reset();
        setMockMillis(0);
        ledState.begin();
        initLeds();
    }
    
    void TearDown() override {
        Preferences::reset();
    }
};

// Time → LED Mapping Integration
TEST_F(IntegrationTest, TimeToDisplay_12_00) {
    struct tm time = createTestTime(12, 0);
    auto leds = get_led_indices_for_time(&time);
    
    showLeds(leds);
    auto shown = test_getLastShownLeds();
    
    ASSERT_FALSE(shown.empty());
    ASSERT_EQ(leds.size(), shown.size());
}

TEST_F(IntegrationTest, TimeToDisplay_AllFiveMinuteIntervals) {
    for (int hour = 0; hour < 24; hour++) {
        for (int minute = 0; minute < 60; minute += 5) {
            struct tm time = createTestTime(hour, minute);
            auto leds = get_led_indices_for_time(&time);
            
            showLeds(leds);
            auto shown = test_getLastShownLeds();
            
            ASSERT_FALSE(shown.empty()) 
                << "No LEDs shown for " << hour << ":" << minute;
            ASSERT_EQ(leds.size(), shown.size())
                << "LED count mismatch for " << hour << ":" << minute;
        }
    }
}

// LED State → Display Integration
TEST_F(IntegrationTest, LedStateChange_PreservedAcrossUpdates) {
    ledState.setRGB(255, 128, 64);
    ledState.setBrightness(200);
    
    uint8_t r, g, b, w;
    ledState.getRGBW(r, g, b, w);
    
    ASSERT_EQ(255, r);
    ASSERT_EQ(128, g);
    ASSERT_EQ(64, b);
    ASSERT_EQ(200, ledState.getBrightness());
    
    // Show some LEDs
    showLeds({1, 2, 3});
    
    // LED state should still be preserved
    ledState.getRGBW(r, g, b, w);
    ASSERT_EQ(255, r);
    ASSERT_EQ(128, g);
    ASSERT_EQ(64, b);
    ASSERT_EQ(200, ledState.getBrightness());
}

// Settings Persistence Integration
TEST_F(IntegrationTest, SettingsPersistence_SurvivesReboot) {
    // Set up state
    ledState.setRGB(100, 150, 200);
    ledState.setBrightness(180);
    ledState.flush();
    
    // Simulate reboot
    LedState newState;
    newState.begin();
    
    uint8_t r, g, b, w;
    newState.getRGBW(r, g, b, w);
    
    ASSERT_EQ(100, r);
    ASSERT_EQ(150, g);
    ASSERT_EQ(200, b);
    ASSERT_EQ(180, newState.getBrightness());
}

// Time Changes Integration
TEST_F(IntegrationTest, MinuteChange_UpdatesDisplay) {
    struct tm time1 = createTestTime(12, 0);
    auto leds1 = get_led_indices_for_time(&time1);
    showLeds(leds1);
    
    struct tm time2 = createTestTime(12, 5);
    auto leds2 = get_led_indices_for_time(&time2);
    showLeds(leds2);
    
    auto shown = test_getLastShownLeds();
    
    // Should show different LEDs
    ASSERT_FALSE(ledVectorsEqual(leds1, leds2))
        << "12:00 and 12:05 should show different words";
}

// Word Segments Integration
TEST_F(IntegrationTest, WordSegments_MatchFullLedIndices) {
    struct tm time = createTestTime(12, 15);
    
    // Get full LED list
    auto allLeds = get_led_indices_for_time(&time);
    
    // Get word segments
    auto segments = get_word_segments_with_keys(&time);
    
    // Collect all LEDs from segments
    std::vector<uint16_t> segmentLeds;
    for (const auto& seg : segments) {
        segmentLeds.insert(segmentLeds.end(), seg.leds.begin(), seg.leds.end());
    }
    
    // Should have same LEDs (segments don't include extra minute LEDs)
    // So segment count should be <= all LEDs
    ASSERT_LE(segmentLeds.size(), allLeds.size());
}

// Edge Cases Integration
TEST_F(IntegrationTest, MidnightTransition_HandledCorrectly) {
    struct tm time1 = createTestTime(23, 55);
    auto leds1 = get_led_indices_for_time(&time1);
    
    struct tm time2 = createTestTime(0, 0);
    auto leds2 = get_led_indices_for_time(&time2);
    
    showLeds(leds1);
    auto shown1 = test_getLastShownLeds();
    
    showLeds(leds2);
    auto shown2 = test_getLastShownLeds();
    
    ASSERT_FALSE(shown1.empty());
    ASSERT_FALSE(shown2.empty());
}

// Multiple State Changes
TEST_F(IntegrationTest, MultipleStateChanges_AllPersisted) {
    // Change LED state multiple times
    ledState.setRGB(255, 0, 0);
    setMockMillis(1000);
    
    ledState.setBrightness(100);
    setMockMillis(2000);
    
    ledState.setRGB(0, 255, 0);
    setMockMillis(3000);
    
    ledState.setBrightness(200);
    setMockMillis(10000);
    
    // Auto-flush should happen
    ledState.loop();
    
    ASSERT_FALSE(ledState.isDirty());
    
    // Verify final state
    uint8_t r, g, b, w;
    ledState.getRGBW(r, g, b, w);
    ASSERT_EQ(0, r);
    ASSERT_EQ(255, g);
    ASSERT_EQ(0, b);
    ASSERT_EQ(200, ledState.getBrightness());
}

// Empty LED Display
TEST_F(IntegrationTest, EmptyLedVector_HandledSafely) {
    std::vector<uint16_t> empty;
    ASSERT_NO_THROW(showLeds(empty));
    
    auto shown = test_getLastShownLeds();
    ASSERT_EQ(0, shown.size());
}

// Large LED Count
TEST_F(IntegrationTest, LargeLedCount_HandledCorrectly) {
    std::vector<uint16_t> largeLeds;
    for (uint16_t i = 0; i < 1000; i++) {
        largeLeds.push_back(i);
    }
    
    ASSERT_NO_THROW(showLeds(largeLeds));
    
    auto shown = test_getLastShownLeds();
    ASSERT_EQ(1000, shown.size());
}

// Rapid Time Updates
TEST_F(IntegrationTest, RapidTimeUpdates_Stable) {
    for (int i = 0; i < 100; i++) {
        struct tm time = createTestTime(12, i % 60);
        auto leds = get_led_indices_for_time(&time);
        showLeds(leds);
        
        auto shown = test_getLastShownLeds();
        ASSERT_FALSE(shown.empty());
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

