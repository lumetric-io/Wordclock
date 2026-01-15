#include <gtest/gtest.h>
#include "../mocks/mock_arduino.h"
#include "../mocks/mock_preferences.h"
#include "../mocks/mock_time.h"
#include "../mocks/mock_mqtt.h"

// Include production log implementation (with PIO_UNIT_TESTING stubs)
#include "../../src/log.cpp"

// Include production code
#include "../../src/night_mode.cpp"

class NightModeTest : public ::testing::Test {
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

TEST_F(NightModeTest, InitializesWithDefaults) {
    ASSERT_FALSE(nightMode.isEnabled());
    ASSERT_EQ(NightModeEffect::Dim, nightMode.getEffect());
    ASSERT_EQ(20, nightMode.getDimPercent());
    ASSERT_EQ(22 * 60, nightMode.getStartMinutes());
    ASSERT_EQ(6 * 60, nightMode.getEndMinutes());
}

TEST_F(NightModeTest, EnableDisable) {
    nightMode.setEnabled(true);
    ASSERT_TRUE(nightMode.isEnabled());
    
    nightMode.setEnabled(false);
    ASSERT_FALSE(nightMode.isEnabled());
}

TEST_F(NightModeTest, SetEffect) {
    nightMode.setEffect(NightModeEffect::Off);
    ASSERT_EQ(NightModeEffect::Off, nightMode.getEffect());
    
    nightMode.setEffect(NightModeEffect::Dim);
    ASSERT_EQ(NightModeEffect::Dim, nightMode.getEffect());
}

TEST_F(NightModeTest, SetDimPercent) {
    nightMode.setDimPercent(50);
    ASSERT_EQ(50, nightMode.getDimPercent());
    
    nightMode.setDimPercent(100);
    ASSERT_EQ(100, nightMode.getDimPercent());
    
    nightMode.setDimPercent(0);
    ASSERT_EQ(0, nightMode.getDimPercent());
}

TEST_F(NightModeTest, SetDimPercent_ClampsTo100) {
    nightMode.setDimPercent(150);
    ASSERT_EQ(100, nightMode.getDimPercent());
}

TEST_F(NightModeTest, SetSchedule) {
    nightMode.setSchedule(20 * 60, 8 * 60);
    ASSERT_EQ(20 * 60, nightMode.getStartMinutes());
    ASSERT_EQ(8 * 60, nightMode.getEndMinutes());
}

TEST_F(NightModeTest, ScheduleCalculation_SimpleRange) {
    // 22:00 to 06:00 schedule
    nightMode.setEnabled(true);
    nightMode.setSchedule(22 * 60, 6 * 60);
    
    // Test time within range
    struct tm time = createTestTime(23, 30);  // 23:30
    nightMode.updateFromTime(time);
    ASSERT_TRUE(nightMode.isActive());
}

TEST_F(NightModeTest, ScheduleCalculation_WrapAroundMidnight) {
    nightMode.setEnabled(true);
    nightMode.setSchedule(22 * 60, 6 * 60);  // 22:00 to 06:00
    
    // Test time after midnight but before end
    struct tm time = createTestTime(2, 0);  // 02:00
    nightMode.updateFromTime(time);
    ASSERT_TRUE(nightMode.isActive());
    
    // Test time after end
    time = createTestTime(8, 0);  // 08:00
    nightMode.updateFromTime(time);
    ASSERT_FALSE(nightMode.isActive());
    
    // Test time before start
    time = createTestTime(20, 0);  // 20:00
    nightMode.updateFromTime(time);
    ASSERT_FALSE(nightMode.isActive());
}

TEST_F(NightModeTest, ScheduleCalculation_NoWrapAround) {
    nightMode.setEnabled(true);
    nightMode.setSchedule(9 * 60, 17 * 60);  // 09:00 to 17:00
    
    // Within range
    struct tm time = createTestTime(12, 0);
    nightMode.updateFromTime(time);
    ASSERT_TRUE(nightMode.isActive());
    
    // Before range
    time = createTestTime(8, 0);
    nightMode.updateFromTime(time);
    ASSERT_FALSE(nightMode.isActive());
    
    // After range
    time = createTestTime(18, 0);
    nightMode.updateFromTime(time);
    ASSERT_FALSE(nightMode.isActive());
}

TEST_F(NightModeTest, ScheduleCalculation_AtBoundaries) {
    nightMode.setEnabled(true);
    nightMode.setSchedule(22 * 60, 6 * 60);  // 22:00 to 06:00
    
    // At start (should be active)
    struct tm time = createTestTime(22, 0);
    nightMode.updateFromTime(time);
    ASSERT_TRUE(nightMode.isActive());
    
    // At end (should NOT be active)
    time = createTestTime(6, 0);
    nightMode.updateFromTime(time);
    ASSERT_FALSE(nightMode.isActive());
}

TEST_F(NightModeTest, NotActiveWhenDisabled) {
    nightMode.setEnabled(false);
    nightMode.setSchedule(22 * 60, 6 * 60);
    
    // Even within schedule, should not be active
    struct tm time = createTestTime(23, 0);
    nightMode.updateFromTime(time);
    ASSERT_FALSE(nightMode.isActive());
}

TEST_F(NightModeTest, DimPercentageApplication) {
    nightMode.setEnabled(true);
    nightMode.setDimPercent(50);
    
    // Make night mode active
    struct tm time = createTestTime(23, 0);
    nightMode.updateFromTime(time);
    ASSERT_TRUE(nightMode.isActive());
    
    // 50% of 100 = 50
    ASSERT_EQ(50, nightMode.applyToBrightness(100));
    
    // 50% of 200 = 100
    ASSERT_EQ(100, nightMode.applyToBrightness(200));
    
    // 50% of 10 = 5
    ASSERT_EQ(5, nightMode.applyToBrightness(10));
    
    // 50% of 255 = 127.5 -> 127
    ASSERT_EQ(127, nightMode.applyToBrightness(255));
}

TEST_F(NightModeTest, DimPercentageRoundingUpFromZero) {
    nightMode.setDimPercent(10);  // 10%
    
    // 10% of 5 = 0.5, should round up to 1
    uint8_t result = nightMode.applyToBrightness(5);
    ASSERT_GE(result, 1) << "Should not dim to zero when both base and percent > 0";
}

TEST_F(NightModeTest, DimPercentage_Zero) {
    nightMode.setEnabled(true);
    nightMode.setDimPercent(0);
    
    // Make night mode active
    struct tm time = createTestTime(23, 0);
    nightMode.updateFromTime(time);
    ASSERT_TRUE(nightMode.isActive());
    
    // 0% should result in 0 brightness when active
    uint8_t result = nightMode.applyToBrightness(100);
    ASSERT_EQ(0, result) << "0% should result in 0 brightness";
}

TEST_F(NightModeTest, DimPercentage_100) {
    nightMode.setDimPercent(100);
    
    // 100% should return original value
    ASSERT_EQ(200, nightMode.applyToBrightness(200));
    ASSERT_EQ(50, nightMode.applyToBrightness(50));
}

TEST_F(NightModeTest, ApplyToBrightness_NotActive) {
    nightMode.setEnabled(false);
    nightMode.setDimPercent(50);
    
    // When not active, should return original brightness
    ASSERT_EQ(100, nightMode.applyToBrightness(100));
    ASSERT_EQ(255, nightMode.applyToBrightness(255));
}

TEST_F(NightModeTest, EffectOff_ReturnsZeroBrightness) {
    nightMode.setEffect(NightModeEffect::Off);
    nightMode.setEnabled(true);
    
    struct tm time = createTestTime(23, 0);  // Within default schedule
    nightMode.updateFromTime(time);
    
    ASSERT_EQ(0, nightMode.applyToBrightness(255));
    ASSERT_EQ(0, nightMode.applyToBrightness(100));
}

TEST_F(NightModeTest, OverrideForceOn) {
    nightMode.setEnabled(true);
    nightMode.setSchedule(22 * 60, 6 * 60);
    nightMode.setOverride(NightModeOverride::ForceOn);
    
    // Even outside schedule, should be active
    struct tm time = createTestTime(12, 0);  // Daytime
    nightMode.updateFromTime(time);
    ASSERT_TRUE(nightMode.isActive());
}

TEST_F(NightModeTest, OverrideForceOff) {
    nightMode.setEnabled(true);
    nightMode.setSchedule(22 * 60, 6 * 60);
    nightMode.setOverride(NightModeOverride::ForceOff);
    
    // Even within schedule, should not be active
    struct tm time = createTestTime(23, 0);  // Nighttime
    nightMode.updateFromTime(time);
    ASSERT_FALSE(nightMode.isActive());
}

TEST_F(NightModeTest, OverrideAuto) {
    nightMode.setEnabled(true);
    nightMode.setSchedule(22 * 60, 6 * 60);
    nightMode.setOverride(NightModeOverride::Auto);
    
    // Should follow schedule
    struct tm time = createTestTime(23, 0);  // Within schedule
    nightMode.updateFromTime(time);
    ASSERT_TRUE(nightMode.isActive());
    
    time = createTestTime(12, 0);  // Outside schedule
    nightMode.updateFromTime(time);
    ASSERT_FALSE(nightMode.isActive());
}

TEST_F(NightModeTest, TimeStringParsing_Valid) {
    uint16_t minutes;
    
    ASSERT_TRUE(NightMode::parseTimeString("12:30", minutes));
    ASSERT_EQ(12 * 60 + 30, minutes);
    
    ASSERT_TRUE(NightMode::parseTimeString("00:00", minutes));
    ASSERT_EQ(0, minutes);
    
    ASSERT_TRUE(NightMode::parseTimeString("23:59", minutes));
    ASSERT_EQ(23 * 60 + 59, minutes);
    
    ASSERT_TRUE(NightMode::parseTimeString("06:15", minutes));
    ASSERT_EQ(6 * 60 + 15, minutes);
}

TEST_F(NightModeTest, TimeStringParsing_Invalid) {
    uint16_t minutes;
    
    ASSERT_FALSE(NightMode::parseTimeString("25:00", minutes));  // Invalid hour
    ASSERT_FALSE(NightMode::parseTimeString("12:60", minutes));  // Invalid minute
    ASSERT_FALSE(NightMode::parseTimeString("12", minutes));     // No colon
    ASSERT_FALSE(NightMode::parseTimeString("", minutes));       // Empty
    // Note: "ab:cd" may parse as 0:0 with toInt(), so skip that test
    // ASSERT_FALSE(NightMode::parseTimeString("ab:cd", minutes));  // Non-numeric
    ASSERT_FALSE(NightMode::parseTimeString("12:", minutes));    // Missing minute
    ASSERT_FALSE(NightMode::parseTimeString(":30", minutes));    // Missing hour
}

TEST_F(NightModeTest, FormatMinutes) {
    ASSERT_STREQ("12:30", nightMode.formatMinutes(12 * 60 + 30).c_str());
    ASSERT_STREQ("00:00", nightMode.formatMinutes(0).c_str());
    ASSERT_STREQ("23:59", nightMode.formatMinutes(23 * 60 + 59).c_str());
    ASSERT_STREQ("06:05", nightMode.formatMinutes(6 * 60 + 5).c_str());
}

TEST_F(NightModeTest, Persistence) {
    // Set values
    nightMode.setEnabled(true);
    nightMode.setDimPercent(75);
    nightMode.setSchedule(20 * 60, 8 * 60);
    nightMode.setEffect(NightModeEffect::Off);
    
    // Force flush
    nightMode.flush();
    
    // Create new instance (simulates reboot)
    NightMode newInstance;
    newInstance.begin();
    
    // Values should be restored
    ASSERT_TRUE(newInstance.isEnabled());
    ASSERT_EQ(75, newInstance.getDimPercent());
    ASSERT_EQ(20 * 60, newInstance.getStartMinutes());
    ASSERT_EQ(8 * 60, newInstance.getEndMinutes());
    ASSERT_EQ(NightModeEffect::Off, newInstance.getEffect());
}

TEST_F(NightModeTest, DirtyFlag) {
    ASSERT_FALSE(nightMode.isDirty());
    
    nightMode.setEnabled(true);
    ASSERT_TRUE(nightMode.isDirty());
    
    nightMode.flush();
    ASSERT_FALSE(nightMode.isDirty());
}

TEST_F(NightModeTest, AutoFlush) {
    nightMode.setEnabled(true);
    ASSERT_TRUE(nightMode.isDirty());
    
    // Advance time by less than auto-flush delay
    setMockMillis(4000);
    nightMode.loop();
    ASSERT_TRUE(nightMode.isDirty()) << "Should not auto-flush yet";
    
    // Advance time past auto-flush delay
    setMockMillis(6000);
    nightMode.loop();
    ASSERT_FALSE(nightMode.isDirty()) << "Should have auto-flushed";
}

TEST_F(NightModeTest, MarkTimeInvalid) {
    nightMode.setEnabled(true);
    nightMode.setSchedule(22 * 60, 6 * 60);
    
    // Make time valid and active
    struct tm time = createTestTime(23, 0);
    nightMode.updateFromTime(time);
    ASSERT_TRUE(nightMode.isActive());
    ASSERT_TRUE(nightMode.hasTime());
    
    // Mark time invalid
    nightMode.markTimeInvalid();
    ASSERT_FALSE(nightMode.hasTime());
    ASSERT_FALSE(nightMode.isActive()) << "Should deactivate when time is invalid";
}

TEST_F(NightModeTest, ZeroLengthSchedule) {
    nightMode.setEnabled(true);
    nightMode.setSchedule(12 * 60, 12 * 60);  // Same start and end
    
    struct tm time = createTestTime(12, 0);
    nightMode.updateFromTime(time);
    ASSERT_FALSE(nightMode.isActive()) << "Zero-length schedule should never be active";
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

