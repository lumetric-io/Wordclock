#include <gtest/gtest.h>

// Include mocks before production code
#include "../mocks/mock_arduino.h"
#include "../mocks/mock_grid_layout.h"
#include "../mocks/mock_time.h"
#include "../helpers/test_utils.h"

// Include production log implementation (with PIO_UNIT_TESTING stubs)
#include "../../src/log.cpp"

// Include production code
#include "../../src/time_mapper.cpp"

// Test fixture
class TimeMapperTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup is already done via mock_grid_layout.h globals
    }
};

// Test: get_leds_for_word function
TEST_F(TimeMapperTest, GetLedsForWord_HET) {
    auto leds = get_leds_for_word("HET");
    ASSERT_EQ(3, leds.size());
    ASSERT_EQ(1, leds[0]);
    ASSERT_EQ(2, leds[1]);
    ASSERT_EQ(3, leds[2]);
}

TEST_F(TimeMapperTest, GetLedsForWord_IS) {
    auto leds = get_leds_for_word("IS");
    ASSERT_EQ(2, leds.size());
    ASSERT_EQ(5, leds[0]);
    ASSERT_EQ(6, leds[1]);
}

TEST_F(TimeMapperTest, GetLedsForWord_NonExistent) {
    auto leds = get_leds_for_word("NONEXISTENT");
    ASSERT_EQ(0, leds.size());
}

// Test: Exact hour (12:00)
TEST_F(TimeMapperTest, ExactHour_12_00) {
    struct tm time = createTestTime(12, 0);
    auto leds = get_led_indices_for_time(&time);
    
    // Should contain: HET, IS, TWAALF, UUR
    ASSERT_FALSE(leds.empty());
    
    // Verify HET is included (1, 2, 3)
    ASSERT_TRUE(vectorContains(leds, static_cast<uint16_t>(1)));
    ASSERT_TRUE(vectorContains(leds, static_cast<uint16_t>(2)));
    ASSERT_TRUE(vectorContains(leds, static_cast<uint16_t>(3)));
    
    // Verify IS is included (5, 6)
    ASSERT_TRUE(vectorContains(leds, static_cast<uint16_t>(5)));
    ASSERT_TRUE(vectorContains(leds, static_cast<uint16_t>(6)));
}

// Test: 5 minutes past (12:05)
TEST_F(TimeMapperTest, FiveMinutesPast_12_05) {
    struct tm time = createTestTime(12, 5);
    auto leds = get_led_indices_for_time(&time);
    
    // Should contain: HET, IS, VIJF, OVER, TWAALF
    ASSERT_FALSE(leds.empty());
    
    // Verify "VIJF_M" is included
    auto vijfLeds = get_leds_for_word("VIJF_M");
    ASSERT_TRUE(vectorContainsAll(leds, vijfLeds))
        << "VIJF_M LEDs not found in result";
    
    // Verify "OVER" is included
    auto overLeds = get_leds_for_word("OVER");
    ASSERT_TRUE(vectorContainsAll(leds, overLeds))
        << "OVER LEDs not found in result";
}

// Test: 10 minutes past (12:10)
TEST_F(TimeMapperTest, TenMinutesPast_12_10) {
    struct tm time = createTestTime(12, 10);
    auto leds = get_led_indices_for_time(&time);
    
    // Should contain: HET, IS, TIEN, OVER, TWAALF
    ASSERT_FALSE(leds.empty());
    
    // Verify "TIEN_M" is included
    auto tienLeds = get_leds_for_word("TIEN_M");
    ASSERT_TRUE(vectorContainsAll(leds, tienLeds))
        << "TIEN_M LEDs not found in result";
}

// Test: Quarter past (12:15)
TEST_F(TimeMapperTest, QuarterPast_12_15) {
    struct tm time = createTestTime(12, 15);
    auto leds = get_led_indices_for_time(&time);
    
    // Should contain: HET, IS, KWART, OVER, TWAALF
    ASSERT_FALSE(leds.empty());
    
    // Verify "KWART" is included
    auto kwartLeds = get_leds_for_word("KWART");
    ASSERT_TRUE(vectorContainsAll(leds, kwartLeds))
        << "KWART LEDs not found in result";
}

// Test: 20 minutes past (12:20)
TEST_F(TimeMapperTest, TwentyPast_12_20) {
    struct tm time = createTestTime(12, 20);
    auto leds = get_led_indices_for_time(&time);
    
    // Should contain: HET, IS, TIEN, VOOR, HALF, EEN (next hour!)
    ASSERT_FALSE(leds.empty());
    
    // Verify "TIEN_M" is included
    auto tienLeds = get_leds_for_word("TIEN_M");
    ASSERT_TRUE(vectorContainsAll(leds, tienLeds));
    
    // Verify "VOOR" is included
    auto voorLeds = get_leds_for_word("VOOR");
    ASSERT_TRUE(vectorContainsAll(leds, voorLeds));
    
    // Verify "HALF" is included
    auto halfLeds = get_leds_for_word("HALF");
    ASSERT_TRUE(vectorContainsAll(leds, halfLeds));
}

// Test: Half past (12:30)
TEST_F(TimeMapperTest, HalfPast_12_30) {
    struct tm time = createTestTime(12, 30);
    auto leds = get_led_indices_for_time(&time);
    
    // Should contain: HET, IS, HALF, EEN (next hour!)
    ASSERT_FALSE(leds.empty());
    
    // Verify "HALF" is included
    auto halfLeds = get_leds_for_word("HALF");
    ASSERT_TRUE(vectorContainsAll(leds, halfLeds))
        << "HALF LEDs not found in result";
    
    // After 20 minutes, it shows the next hour (12:30 = half een)
    auto eenLeds = get_leds_for_word("EEN");
    ASSERT_TRUE(vectorContainsAll(leds, eenLeds))
        << "EEN LEDs not found in result (should show next hour)";
}

// Test: Quarter to (12:45)
TEST_F(TimeMapperTest, QuarterTo_12_45) {
    struct tm time = createTestTime(12, 45);
    auto leds = get_led_indices_for_time(&time);
    
    // Should contain: HET, IS, KWART, VOOR, EEN
    ASSERT_FALSE(leds.empty());
    
    // Verify "KWART" is included
    auto kwartLeds = get_leds_for_word("KWART");
    ASSERT_TRUE(vectorContainsAll(leds, kwartLeds));
    
    // Verify "VOOR" is included
    auto voorLeds = get_leds_for_word("VOOR");
    ASSERT_TRUE(vectorContainsAll(leds, voorLeds));
}

// Test: Extra minute LEDs (12:32 = 30 + 2 extra)
TEST_F(TimeMapperTest, ExtraMinuteLEDs_12_32) {
    struct tm time = createTestTime(12, 32);
    auto leds = get_led_indices_for_time(&time);
    
    // Should include 2 extra minute LEDs
    int extraCount = 0;
    for (size_t i = 0; i < EXTRA_MINUTE_LED_COUNT && i < 2; i++) {
        if (vectorContains(leds, EXTRA_MINUTE_LEDS[i])) {
            extraCount++;
        }
    }
    
    ASSERT_EQ(2, extraCount) << "Expected 2 extra minute LEDs for :32";
}

// Test: Extra minute LEDs (12:03 = 00 + 3 extra)
TEST_F(TimeMapperTest, ExtraMinuteLEDs_12_03) {
    struct tm time = createTestTime(12, 3);
    auto leds = get_led_indices_for_time(&time);
    
    // Should include 3 extra minute LEDs
    int extraCount = 0;
    for (size_t i = 0; i < EXTRA_MINUTE_LED_COUNT && i < 3; i++) {
        if (vectorContains(leds, EXTRA_MINUTE_LEDS[i])) {
            extraCount++;
        }
    }
    
    ASSERT_EQ(3, extraCount) << "Expected 3 extra minute LEDs for :03";
}

// Test: Midnight (00:00)
TEST_F(TimeMapperTest, Midnight_00_00) {
    struct tm time = createTestTime(0, 0);
    auto leds = get_led_indices_for_time(&time);
    
    // Should contain: HET, IS, TWAALF, UUR
    ASSERT_FALSE(leds.empty());
    
    // Verify "TWAALF" is included (0 % 12 = 0 = TWAALF)
    auto twaalfLeds = get_leds_for_word("TWAALF");
    ASSERT_TRUE(vectorContainsAll(leds, twaalfLeds))
        << "TWAALF LEDs not found for midnight";
}

// Test: Midnight wraparound (23:55)
TEST_F(TimeMapperTest, MidnightWraparound_23_55) {
    struct tm time = createTestTime(23, 55);
    auto leds = get_led_indices_for_time(&time);
    
    // 23:55 = "vijf voor twaalf" (5 to 12)
    ASSERT_FALSE(leds.empty());
    
    // Verify "VIJF_M" is included
    auto vijfLeds = get_leds_for_word("VIJF_M");
    ASSERT_TRUE(vectorContainsAll(leds, vijfLeds));
    
    // Verify "VOOR" is included
    auto voorLeds = get_leds_for_word("VOOR");
    ASSERT_TRUE(vectorContainsAll(leds, voorLeds));
    
    // Verify "TWAALF" is included
    auto twaalfLeds = get_leds_for_word("TWAALF");
    ASSERT_TRUE(vectorContainsAll(leds, twaalfLeds));
}

// Test: All 5-minute intervals produce results
TEST_F(TimeMapperTest, AllFiveMinuteIntervals) {
    for (int hour = 0; hour < 24; hour++) {
        for (int minute = 0; minute < 60; minute += 5) {
            struct tm time = createTestTime(hour, minute);
            auto leds = get_led_indices_for_time(&time);
            
            ASSERT_FALSE(leds.empty()) 
                << "No LEDs for " << hour << ":" << minute;
            
            // Should always contain at least HET and IS
            ASSERT_GE(leds.size(), 5) 
                << "Too few LEDs for " << hour << ":" << minute;
        }
    }
}

// Test: Word segments with keys (12:15)
TEST_F(TimeMapperTest, WordSegmentsWithKeys_12_15) {
    struct tm time = createTestTime(12, 15);
    auto segments = get_word_segments_with_keys(&time);
    
    // Should have multiple segments
    ASSERT_GT(segments.size(), 0);
    
    // First two should be HET and IS
    ASSERT_STREQ("HET", segments[0].key);
    ASSERT_STREQ("IS", segments[1].key);
    
    // Should include KWART
    bool hasKwart = false;
    for (const auto& seg : segments) {
        if (strcmp(seg.key, "KWART") == 0) {
            hasKwart = true;
            break;
        }
    }
    ASSERT_TRUE(hasKwart) << "KWART segment not found for 12:15";
}

// Test: Word segments without keys (12:00)
TEST_F(TimeMapperTest, WordSegmentsForTime_12_00) {
    struct tm time = createTestTime(12, 0);
    auto segments = get_word_segments_for_time(&time);
    
    // Should have segments for: HET, IS, TWAALF, UUR
    ASSERT_EQ(4, segments.size());
}

// Test: Merge LEDs helper
TEST_F(TimeMapperTest, MergeLeds) {
    std::vector<uint16_t> a = {1, 2, 3};
    std::vector<uint16_t> b = {4, 5};
    std::vector<uint16_t> c = {6};
    
    auto result = merge_leds({a, b, c});
    
    ASSERT_EQ(6, result.size());
    ASSERT_EQ(1, result[0]);
    ASSERT_EQ(6, result[5]);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

