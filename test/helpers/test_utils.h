#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <gtest/gtest.h>
#include <vector>
#include <algorithm>
#include <string>

// Helper to compare LED index vectors
inline bool ledVectorsEqual(const std::vector<uint16_t>& a, 
                            const std::vector<uint16_t>& b) {
    if (a.size() != b.size()) return false;
    
    // Sort both for comparison (order might not matter)
    std::vector<uint16_t> sortedA = a;
    std::vector<uint16_t> sortedB = b;
    std::sort(sortedA.begin(), sortedA.end());
    std::sort(sortedB.begin(), sortedB.end());
    
    return sortedA == sortedB;
}

// Custom assertion for LED vectors
#define ASSERT_LED_VECTOR_EQ(expected, actual) \
    ASSERT_TRUE(ledVectorsEqual(expected, actual)) \
        << "LED vectors differ\nExpected: " << ledVectorToString(expected) \
        << "\nActual: " << ledVectorToString(actual)

// Print LED vector for debugging
inline std::string ledVectorToString(const std::vector<uint16_t>& leds) {
    std::string result = "[";
    for (size_t i = 0; i < leds.size(); i++) {
        result += std::to_string(leds[i]);
        if (i < leds.size() - 1) result += ", ";
    }
    result += "]";
    return result;
}

// Helper to check if a vector contains a value
template<typename T>
inline bool vectorContains(const std::vector<T>& vec, const T& value) {
    return std::find(vec.begin(), vec.end(), value) != vec.end();
}

// Helper to check if vector A contains all elements from vector B
template<typename T>
inline bool vectorContainsAll(const std::vector<T>& haystack, const std::vector<T>& needles) {
    for (const auto& needle : needles) {
        if (!vectorContains(haystack, needle)) {
            return false;
        }
    }
    return true;
}

#endif // TEST_UTILS_H

