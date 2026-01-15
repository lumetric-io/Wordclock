#include <gtest/gtest.h>
#include "../mocks/mock_arduino.h"
#include "../mocks/mock_grid_layout.h"
#include "../mocks/mock_time.h"
#include "../mocks/mock_preferences.h"
#include "../mocks/mock_mqtt.h"
#include "../helpers/test_utils.h"

// Include production code
#include "../../src/log.cpp"
#include "../../src/time_mapper.cpp"
#include "../../src/night_mode.cpp"

class EdgeCasesTest : public ::testing::Test {
protected:
    void SetUp() override {
        Preferences::reset();
        setMockMillis(0);
        nightMode.begin();
    }
    
    void TearDown() override {
        Preferences::reset();
    }
};

// Time Edge Cases
TEST_F(EdgeCasesTest, Time_23_59_BeforeMidnight) {
    struct tm time = createTestTime(23, 59);
    auto leds = get_led_indices_for_time(&time);
    
    ASSERT_FALSE(leds.empty());
}

TEST_F(EdgeCasesTest, Time_00_00_Midnight) {
    struct tm time = createTestTime(0, 0);
    auto leds = get_led_indices_for_time(&time);
    
    ASSERT_FALSE(leds.empty());
}

TEST_F(EdgeCasesTest, Time_00_01_AfterMidnight) {
    struct tm time = createTestTime(0, 1);
    auto leds = get_led_indices_for_time(&time);
    
    ASSERT_FALSE(leds.empty());
}

TEST_F(EdgeCasesTest, Time_InvalidHour_25_DoesNotCrash) {
    struct tm time = createTestTime(25, 0);  // Invalid but should not crash
    ASSERT_NO_THROW({
        auto leds = get_led_indices_for_time(&time);
        (void)leds;
    });
}

TEST_F(EdgeCasesTest, Time_InvalidMinute_70_DoesNotCrash) {
    struct tm time = createTestTime(12, 70);  // Invalid but should not crash
    ASSERT_NO_THROW({
        auto leds = get_led_indices_for_time(&time);
        (void)leds;
    });
}


// Night Mode Edge Cases
TEST_F(EdgeCasesTest, NightMode_Schedule_SameStartEnd) {
    nightMode.setEnabled(true);
    nightMode.setSchedule(12 * 60, 12 * 60);  // Same time
    
    struct tm time = createTestTime(12, 0);
    nightMode.updateFromTime(time);
    
    ASSERT_FALSE(nightMode.isActive()) << "Zero-length schedule should not be active";
}

TEST_F(EdgeCasesTest, NightMode_Schedule_23_59_To_00_00) {
    nightMode.setEnabled(true);
    nightMode.setSchedule(23 * 60 + 59, 0);
    
    struct tm time = createTestTime(23, 59);
    nightMode.updateFromTime(time);
    ASSERT_TRUE(nightMode.isActive());
    
    time = createTestTime(0, 0);
    nightMode.updateFromTime(time);
    ASSERT_FALSE(nightMode.isActive());  // End time is exclusive
}

TEST_F(EdgeCasesTest, NightMode_DimPercent_0) {
    nightMode.setEnabled(true);
    nightMode.setDimPercent(0);
    
    struct tm time = createTestTime(23, 0);
    nightMode.updateFromTime(time);
    
    uint8_t result = nightMode.applyToBrightness(255);
    ASSERT_EQ(0, result);
}

TEST_F(EdgeCasesTest, NightMode_DimPercent_100) {
    nightMode.setEnabled(true);
    nightMode.setDimPercent(100);
    
    struct tm time = createTestTime(23, 0);
    nightMode.updateFromTime(time);
    
    uint8_t result = nightMode.applyToBrightness(200);
    ASSERT_EQ(200, result);  // 100% = no change
}

TEST_F(EdgeCasesTest, NightMode_DimPercent_Over100_Clamped) {
    nightMode.setDimPercent(150);  // Over 100
    ASSERT_EQ(100, nightMode.getDimPercent());  // Should be clamped
}

TEST_F(EdgeCasesTest, NightMode_NotEnabled_NeverActive) {
    nightMode.setEnabled(false);
    nightMode.setSchedule(0, 24 * 60);  // Full day
    
    struct tm time = createTestTime(12, 0);
    nightMode.updateFromTime(time);
    
    ASSERT_FALSE(nightMode.isActive());
}

// Word Lookup Edge Cases
TEST_F(EdgeCasesTest, WordLookup_Null_ReturnsEmpty) {
    auto leds = get_leds_for_word(nullptr);
    ASSERT_TRUE(leds.empty());
}

TEST_F(EdgeCasesTest, WordLookup_EmptyString_ReturnsEmpty) {
    auto leds = get_leds_for_word("");
    ASSERT_TRUE(leds.empty());
}

TEST_F(EdgeCasesTest, WordLookup_VeryLongString_DoesNotCrash) {
    std::string longWord(1000, 'X');
    ASSERT_NO_THROW({
        auto leds = get_leds_for_word(longWord.c_str());
        (void)leds;
    });
}


// All Hours Test
TEST_F(EdgeCasesTest, AllHours_ProduceValidLEDs) {
    for (int hour = 0; hour < 24; hour++) {
        struct tm time = createTestTime(hour, 0);
        auto leds = get_led_indices_for_time(&time);
        
        ASSERT_FALSE(leds.empty()) 
            << "No LEDs for hour " << hour;
    }
}

// All Minutes Test  
TEST_F(EdgeCasesTest, AllMinutes_ProduceValidLEDs) {
    for (int minute = 0; minute < 60; minute++) {
        struct tm time = createTestTime(12, minute);
        auto leds = get_led_indices_for_time(&time);
        
        ASSERT_FALSE(leds.empty()) 
            << "No LEDs for minute " << minute;
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

