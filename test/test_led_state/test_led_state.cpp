#include <gtest/gtest.h>
#include "../mocks/mock_arduino.h"
#include "../mocks/mock_preferences.h"

// Include production code
#include "../../src/led_state.h"
#include "../../src/led_state.cpp"

class LedStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        Preferences::reset();
        setMockMillis(0);
        ledState.begin();
    }
    
    void TearDown() override {
        Preferences::reset();
    }
};

// Initialization Tests
TEST_F(LedStateTest, InitializesWithDefaults) {
    uint8_t r, g, b, w;
    ledState.getRGBW(r, g, b, w);
    
    ASSERT_EQ(0, r);
    ASSERT_EQ(0, g);
    ASSERT_EQ(0, b);
    ASSERT_EQ(255, w);  // Default is pure white
    ASSERT_EQ(64, ledState.getBrightness());
}

// RGB Color Tests
TEST_F(LedStateTest, SetRGB_StoresValues) {
    ledState.setRGB(128, 64, 32);
    
    uint8_t r, g, b, w;
    ledState.getRGBW(r, g, b, w);
    
    ASSERT_EQ(128, r);
    ASSERT_EQ(64, g);
    ASSERT_EQ(32, b);
    ASSERT_EQ(0, w);  // RGB mode, no white
}

TEST_F(LedStateTest, SetRGB_PureWhite_UsesWhiteChannel) {
    ledState.setRGB(255, 255, 255);
    
    uint8_t r, g, b, w;
    ledState.getRGBW(r, g, b, w);
    
    // Pure white should use W channel and set RGB to 0
    ASSERT_EQ(0, r);
    ASSERT_EQ(0, g);
    ASSERT_EQ(0, b);
    ASSERT_EQ(255, w);
}

TEST_F(LedStateTest, SetRGB_NoChange_DoesNotMarkDirty) {
    ledState.setRGB(100, 100, 100);
    ledState.flush();
    
    ASSERT_FALSE(ledState.isDirty());
    
    // Set to same values
    ledState.setRGB(100, 100, 100);
    ASSERT_FALSE(ledState.isDirty());
}

TEST_F(LedStateTest, SetRGB_Change_MarksDirty) {
    ledState.setRGB(100, 100, 100);
    ledState.flush();
    
    ASSERT_FALSE(ledState.isDirty());
    
    // Change values
    ledState.setRGB(200, 100, 100);
    ASSERT_TRUE(ledState.isDirty());
}

// Brightness Tests
TEST_F(LedStateTest, SetBrightness_StoresValue) {
    ledState.setBrightness(128);
    ASSERT_EQ(128, ledState.getBrightness());
}

TEST_F(LedStateTest, SetBrightness_NoChange_DoesNotMarkDirty) {
    ledState.setBrightness(100);
    ledState.flush();
    
    ASSERT_FALSE(ledState.isDirty());
    
    ledState.setBrightness(100);
    ASSERT_FALSE(ledState.isDirty());
}

TEST_F(LedStateTest, SetBrightness_Change_MarksDirty) {
    ledState.setBrightness(100);
    ledState.flush();
    
    ASSERT_FALSE(ledState.isDirty());
    
    ledState.setBrightness(200);
    ASSERT_TRUE(ledState.isDirty());
}

TEST_F(LedStateTest, SetBrightness_BoundaryValues) {
    ledState.setBrightness(0);
    ASSERT_EQ(0, ledState.getBrightness());
    
    ledState.setBrightness(255);
    ASSERT_EQ(255, ledState.getBrightness());
}

// Persistence Tests
TEST_F(LedStateTest, Persistence_SavesRGB) {
    ledState.setRGB(100, 150, 200);
    ledState.flush();
    
    // Create new instance (simulates reboot)
    LedState newState;
    newState.begin();
    
    uint8_t r, g, b, w;
    newState.getRGBW(r, g, b, w);
    
    ASSERT_EQ(100, r);
    ASSERT_EQ(150, g);
    ASSERT_EQ(200, b);
}

TEST_F(LedStateTest, Persistence_SavesBrightness) {
    ledState.setBrightness(200);
    ledState.flush();
    
    LedState newState;
    newState.begin();
    
    ASSERT_EQ(200, newState.getBrightness());
}

TEST_F(LedStateTest, Persistence_SavesWhiteMode) {
    ledState.setRGB(255, 255, 255);
    ledState.flush();
    
    LedState newState;
    newState.begin();
    
    uint8_t r, g, b, w;
    newState.getRGBW(r, g, b, w);
    
    ASSERT_EQ(0, r);
    ASSERT_EQ(0, g);
    ASSERT_EQ(0, b);
    ASSERT_EQ(255, w);
}

// Dirty Flag Tests
TEST_F(LedStateTest, DirtyFlag_InitiallyClean) {
    ASSERT_FALSE(ledState.isDirty());
}

TEST_F(LedStateTest, DirtyFlag_SetAfterChange) {
    ledState.setRGB(100, 100, 100);
    ASSERT_TRUE(ledState.isDirty());
}

TEST_F(LedStateTest, DirtyFlag_ClearedAfterFlush) {
    ledState.setRGB(100, 100, 100);
    ASSERT_TRUE(ledState.isDirty());
    
    ledState.flush();
    ASSERT_FALSE(ledState.isDirty());
}

// Auto-flush Tests
TEST_F(LedStateTest, AutoFlush_NotBeforeDelay) {
    ledState.setRGB(100, 100, 100);
    ASSERT_TRUE(ledState.isDirty());
    
    // Advance time by less than auto-flush delay (5 seconds)
    setMockMillis(4000);
    ledState.loop();
    
    ASSERT_TRUE(ledState.isDirty()) << "Should not auto-flush yet";
}

TEST_F(LedStateTest, AutoFlush_AfterDelay) {
    ledState.setRGB(100, 100, 100);
    ASSERT_TRUE(ledState.isDirty());
    
    // Advance time past auto-flush delay
    setMockMillis(6000);
    ledState.loop();
    
    ASSERT_FALSE(ledState.isDirty()) << "Should have auto-flushed";
}

TEST_F(LedStateTest, MillisSinceLastFlush_TracksTime) {
    ledState.setRGB(100, 100, 100);
    
    setMockMillis(1000);
    ASSERT_EQ(1000, ledState.millisSinceLastFlush());
    
    setMockMillis(3000);
    ASSERT_EQ(3000, ledState.millisSinceLastFlush());
}

// Edge Cases
TEST_F(LedStateTest, MultipleChanges_OnlyOneDirtyFlag) {
    ledState.setRGB(100, 100, 100);
    ledState.setRGB(150, 150, 150);
    ledState.setBrightness(200);
    
    ASSERT_TRUE(ledState.isDirty());
    
    ledState.flush();
    ASSERT_FALSE(ledState.isDirty());
}

TEST_F(LedStateTest, FlushWhenClean_NoOp) {
    ASSERT_FALSE(ledState.isDirty());
    
    // Should not cause issues
    ledState.flush();
    
    ASSERT_FALSE(ledState.isDirty());
}

TEST_F(LedStateTest, LoopWhenClean_NoOp) {
    ASSERT_FALSE(ledState.isDirty());
    
    setMockMillis(10000);
    ledState.loop();
    
    ASSERT_FALSE(ledState.isDirty());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

