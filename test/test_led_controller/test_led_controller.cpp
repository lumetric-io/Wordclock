#include <gtest/gtest.h>

// Include mocks
#include "../mocks/mock_arduino.h"
#include "../mocks/mock_grid_layout.h"
#include "../mocks/mock_preferences.h"
#include "../helpers/test_utils.h"

// Mock dependencies that led_controller needs
#ifndef LED_STATE_H
#define LED_STATE_H
class LedState {
public:
    void begin() {}
    uint8_t getBrightness() const { return brightness_; }
    void setBrightness(uint8_t b) { brightness_ = b; }
    void getRGBW(uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& w) const {
        r = 255; g = 255; b = 255; w = 255;
    }
private:
    uint8_t brightness_ = 255;
};

LedState ledState;
#endif

// Mock night_mode
#ifndef NIGHT_MODE_H
#define NIGHT_MODE_H
class NightMode {
public:
    uint8_t applyToBrightness(uint8_t base) const { return base; }
};

NightMode nightMode;
#endif

// Note: Using production DATA_PIN value (4) from src/config.h

// Include production code
#include "../../src/led_controller.cpp"

class LedControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        initLeds();
        test_clearLastShownLeds();
    }
};

TEST_F(LedControllerTest, InitializesCorrectly) {
    // Init should succeed without error
    initLeds();
    auto shown = test_getLastShownLeds();
    ASSERT_EQ(0, shown.size()) << "LEDs should be cleared after init";
}

TEST_F(LedControllerTest, ShowsSingleLED) {
    std::vector<uint16_t> leds = {5};
    showLeds(leds);
    
    auto shown = test_getLastShownLeds();
    ASSERT_EQ(1, shown.size());
    ASSERT_EQ(5, shown[0]);
}

TEST_F(LedControllerTest, ShowsMultipleLEDs) {
    std::vector<uint16_t> leds = {1, 2, 3, 10, 20};
    showLeds(leds);
    
    auto shown = test_getLastShownLeds();
    ASSERT_EQ(5, shown.size());
    ASSERT_LED_VECTOR_EQ(leds, shown);
}

TEST_F(LedControllerTest, ClearsLEDsWhenEmpty) {
    // First show some LEDs
    showLeds({1, 2, 3});
    
    auto shown = test_getLastShownLeds();
    ASSERT_EQ(3, shown.size());
    
    // Then clear
    showLeds({});
    
    shown = test_getLastShownLeds();
    ASSERT_EQ(0, shown.size());
}

TEST_F(LedControllerTest, HandlesLargeNumberOfLEDs) {
    std::vector<uint16_t> leds;
    for (uint16_t i = 0; i < 100; i++) {
        leds.push_back(i);
    }
    
    showLeds(leds);
    
    auto shown = test_getLastShownLeds();
    ASSERT_EQ(100, shown.size());
    ASSERT_LED_VECTOR_EQ(leds, shown);
}

TEST_F(LedControllerTest, UpdatesLEDsCorrectly) {
    // Show first set
    showLeds({1, 2, 3});
    auto shown = test_getLastShownLeds();
    ASSERT_EQ(3, shown.size());
    
    // Show different set
    showLeds({10, 20, 30, 40});
    shown = test_getLastShownLeds();
    ASSERT_EQ(4, shown.size());
    ASSERT_TRUE(vectorContains(shown, static_cast<uint16_t>(10)));
    ASSERT_TRUE(vectorContains(shown, static_cast<uint16_t>(20)));
    ASSERT_TRUE(vectorContains(shown, static_cast<uint16_t>(30)));
    ASSERT_TRUE(vectorContains(shown, static_cast<uint16_t>(40)));
    
    // Old LEDs should not be present
    ASSERT_FALSE(vectorContains(shown, static_cast<uint16_t>(1)));
    ASSERT_FALSE(vectorContains(shown, static_cast<uint16_t>(2)));
    ASSERT_FALSE(vectorContains(shown, static_cast<uint16_t>(3)));
}

TEST_F(LedControllerTest, PreservesLEDOrder) {
    std::vector<uint16_t> leds = {5, 1, 10, 3, 7};
    showLeds(leds);
    
    auto shown = test_getLastShownLeds();
    ASSERT_EQ(5, shown.size());
    
    // Check exact order is preserved
    for (size_t i = 0; i < leds.size(); i++) {
        ASSERT_EQ(leds[i], shown[i]) << "LED order mismatch at index " << i;
    }
}

TEST_F(LedControllerTest, HandlesDuplicateLEDs) {
    std::vector<uint16_t> leds = {5, 5, 10, 10, 10};
    showLeds(leds);
    
    auto shown = test_getLastShownLeds();
    ASSERT_EQ(5, shown.size()) << "Should preserve duplicates";
}

TEST_F(LedControllerTest, ClearPreservesState) {
    // Show LEDs
    showLeds({1, 2, 3});
    
    // Clear
    test_clearLastShownLeds();
    
    auto shown = test_getLastShownLeds();
    ASSERT_EQ(0, shown.size());
    
    // Should be able to show again
    showLeds({4, 5, 6});
    shown = test_getLastShownLeds();
    ASSERT_EQ(3, shown.size());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

