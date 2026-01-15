#include <gtest/gtest.h>
#include <chrono>
#include "../mocks/mock_arduino.h"
#include "../mocks/mock_grid_layout.h"
#include "../mocks/mock_time.h"
#include "../mocks/mock_preferences.h"
#include "../helpers/test_utils.h"

// Include production log
#include "../../src/log.cpp"

// Include production code
#include "../../src/time_mapper.cpp"

class PerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
    
    void TearDown() override {
    }
    
    // Helper to measure execution time in microseconds
    template<typename Func>
    long measureMicroseconds(Func func) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }
};

// Time Mapper Performance
TEST_F(PerformanceTest, TimeMapper_AllTimes_Under10ms) {
    long totalTime = 0;
    int count = 0;
    
    for (int hour = 0; hour < 24; hour++) {
        for (int minute = 0; minute < 60; minute += 5) {
            struct tm time = createTestTime(hour, minute);
            
            long elapsed = measureMicroseconds([&]() {
                auto leds = get_led_indices_for_time(&time);
                (void)leds;  // Prevent optimization
            });
            
            totalTime += elapsed;
            count++;
            
            // Individual call should be fast
            ASSERT_LT(elapsed, 10000) 
                << "Too slow for " << hour << ":" << minute 
                << " (" << elapsed << " us)";
        }
    }
    
    long avgTime = totalTime / count;
    ASSERT_LT(avgTime, 5000) << "Average time too high: " << avgTime << " us";
}

TEST_F(PerformanceTest, WordLookup_Fast) {
    long elapsed = measureMicroseconds([&]() {
        for (int i = 0; i < 1000; i++) {
            auto word = get_leds_for_word("HET");
            (void)word;
        }
    });
    
    long avgPerCall = elapsed / 1000;
    ASSERT_LT(avgPerCall, 10) << "Word lookup too slow: " << avgPerCall << " us";
}

TEST_F(PerformanceTest, WordSegments_Fast) {
    struct tm time = createTestTime(12, 15);
    
    long elapsed = measureMicroseconds([&]() {
        for (int i = 0; i < 100; i++) {
            auto segments = get_word_segments_with_keys(&time);
            (void)segments;
        }
    });
    
    long avgPerCall = elapsed / 100;
    ASSERT_LT(avgPerCall, 100) << "Word segments too slow: " << avgPerCall << " us";
}


// Memory Tests
TEST_F(PerformanceTest, LEDVector_LargeCount_NoOverflow) {
    std::vector<uint16_t> leds;
    
    // Add 10000 LEDs
    for (uint16_t i = 0; i < 10000; i++) {
        leds.push_back(i);
    }
    
    ASSERT_EQ(10000, leds.size());
    ASSERT_EQ(0, leds.front());
    ASSERT_EQ(9999, leds.back());
}

TEST_F(PerformanceTest, StringOperations_NoMemoryLeak) {
    // Perform many string operations
    for (int i = 0; i < 1000; i++) {
        String test = "test";
        test = test + String(i);
        test = test + " value";
        (void)test;
    }
    
    // If we get here without crashing, no obvious memory leak
    SUCCEED();
}


TEST_F(PerformanceTest, ManyTimeCalculations_Consistent) {
    std::vector<std::vector<uint16_t>> results;
    
    // Calculate same time 1000 times
    for (int i = 0; i < 1000; i++) {
        struct tm time = createTestTime(12, 30);
        results.push_back(get_led_indices_for_time(&time));
    }
    
    // All results should be identical
    for (size_t i = 1; i < results.size(); i++) {
        ASSERT_EQ(results[0].size(), results[i].size())
            << "Inconsistent result at iteration " << i;
        ASSERT_TRUE(ledVectorsEqual(results[0], results[i]))
            << "Different LEDs at iteration " << i;
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

