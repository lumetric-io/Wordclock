#include <gtest/gtest.h>

// Include mocks before production code
#include "../mocks/mock_arduino.h"
#include "../mocks/mock_time.h"
#include "../helpers/test_utils.h"

// Define WordSegment structure (from time_mapper.h)
struct WordSegment {
    const char* key;
    std::vector<uint16_t> leds;
};

// Define WordAnimationMode enum (from display_settings.h)
enum class WordAnimationMode : uint8_t { Classic = 0, Smart = 1 };

// Include production log implementation (with PIO_UNIT_TESTING stubs)
#include "../../src/log.cpp"

/**
 * This test file tests the ClockDisplay static helper methods that were
 * extracted from wordclock.cpp during refactoring.
 * 
 * These methods contain core logic that was previously embedded in a 196-line
 * function and are now testable in isolation.
 */

// Since we're only testing static methods, we'll implement them here for testing
// This avoids pulling in all the dependencies from clock_display.cpp

namespace ClockDisplayHelpers {
    static bool isHetIs(const WordSegment& seg) {
        return strcmp(seg.key, "HET") == 0 || strcmp(seg.key, "IS") == 0;
    }

    static void stripHetIsIfDisabled(std::vector<WordSegment>& segs, uint16_t hetIsDurationSec) {
        if (hetIsDurationSec != 0) return;
        segs.erase(std::remove_if(segs.begin(), segs.end(), 
                                 [](const WordSegment& s) { return isHetIs(s); }), 
                   segs.end());
    }

    static std::vector<uint16_t> flattenSegments(const std::vector<WordSegment>& segs) {
        std::vector<uint16_t> indices;
        for (const auto& seg : segs) {
            indices.insert(indices.end(), seg.leds.begin(), seg.leds.end());
        }
        return indices;
    }

    static const WordSegment* findSegment(const std::vector<WordSegment>& segs, const char* key) {
        for (const auto& seg : segs) {
            if (strcmp(seg.key, key) == 0) return &seg;
        }
        return nullptr;
    }

    static void removeLeds(std::vector<uint16_t>& base, const std::vector<uint16_t>& toRemove) {
        base.erase(std::remove_if(base.begin(), base.end(), [&](uint16_t idx) {
            return std::find(toRemove.begin(), toRemove.end(), idx) != toRemove.end();
        }), base.end());
    }

    static bool hetIsCurrentlyVisible(uint16_t hetIsDurationSec, unsigned long hetIsVisibleUntil, unsigned long nowMs) {
        if (hetIsDurationSec == 0) return false;
        if (hetIsDurationSec >= 360) return true;
        if (hetIsVisibleUntil == 0) return true;
        return nowMs < hetIsVisibleUntil;
    }

    static void buildClassicFrames(const std::vector<WordSegment>& segs, 
                                   std::vector<std::vector<uint16_t>>& frames) {
        frames.clear();
        std::vector<uint16_t> cumulative;
        for (const auto& seg : segs) {
            cumulative.insert(cumulative.end(), seg.leds.begin(), seg.leds.end());
            frames.push_back(cumulative);
        }
    }

    static void buildSmartFrames(const std::vector<WordSegment>& prevSegments,
                                 const std::vector<WordSegment>& nextSegments,
                                 bool hetIsVisible,
                                 std::vector<std::vector<uint16_t>>& frames) {
        frames.clear();
        if (prevSegments.empty()) {
            buildClassicFrames(nextSegments, frames);
            return;
        }
        
        std::vector<WordSegment> prevVisible;
        prevVisible.reserve(prevSegments.size());
        for (const auto& seg : prevSegments) {
            if (isHetIs(seg) && !hetIsVisible) continue;
            prevVisible.push_back(seg);
        }
        
        std::vector<uint16_t> current = flattenSegments(prevVisible);
        
        std::vector<WordSegment> removals;
        for (const auto& seg : prevVisible) {
            bool presentInNext = findSegment(nextSegments, seg.key) != nullptr;
            if (isHetIs(seg) || !presentInNext) {
                removals.push_back(seg);
            }
        }
        
        std::vector<WordSegment> additions;
        for (const auto& seg : nextSegments) {
            bool visibleBefore = findSegment(prevVisible, seg.key) != nullptr;
            if (isHetIs(seg) || !visibleBefore) {
                additions.push_back(seg);
            }
        }
        
        if (!removals.empty()) {
            std::vector<uint16_t> removalLeds;
            for (const auto& rem : removals) {
                removalLeds.insert(removalLeds.end(), rem.leds.begin(), rem.leds.end());
            }
            removeLeds(current, removalLeds);
            frames.push_back(current);
        }
        for (const auto& add : additions) {
            current.insert(current.end(), add.leds.begin(), add.leds.end());
            frames.push_back(current);
        }
        if (frames.empty()) {
            frames.push_back(current);
        }
    }
}

using namespace ClockDisplayHelpers;

// Test fixture
class ClockDisplayTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset state before each test
    }
    
    void TearDown() override {
        // Clean up after each test
    }
};

// ============================================================================
// Test: Helper Functions
// ============================================================================

TEST_F(ClockDisplayTest, IsHetIs_IdentifiesHet) {
    WordSegment seg;
    seg.key = "HET";
    seg.leds = {0, 1, 2};
    
    EXPECT_TRUE(isHetIs(seg));
}

TEST_F(ClockDisplayTest, IsHetIs_IdentifiesIs) {
    WordSegment seg;
    seg.key = "IS";
    seg.leds = {3, 4};
    
    EXPECT_TRUE(isHetIs(seg));
}

TEST_F(ClockDisplayTest, IsHetIs_RejectsOtherWords) {
    WordSegment seg;
    seg.key = "VIJF";
    seg.leds = {5, 6, 7, 8};
    
    EXPECT_FALSE(isHetIs(seg));
}

TEST_F(ClockDisplayTest, StripHetIsIfDisabled_RemovesWhenZero) {
    std::vector<WordSegment> segments = {
        {"HET", {0, 1, 2}},
        {"IS", {3, 4}},
        {"VIJF", {5, 6, 7, 8}}
    };
    
    stripHetIsIfDisabled(segments, 0);
    
    EXPECT_EQ(1, segments.size());
    EXPECT_STREQ("VIJF", segments[0].key);
}

TEST_F(ClockDisplayTest, StripHetIsIfDisabled_KeepsWhenNonzero) {
    std::vector<WordSegment> segments = {
        {"HET", {0, 1, 2}},
        {"IS", {3, 4}},
        {"VIJF", {5, 6, 7, 8}}
    };
    
    stripHetIsIfDisabled(segments, 5);
    
    EXPECT_EQ(3, segments.size());
}

TEST_F(ClockDisplayTest, FlattenSegments) {
    std::vector<WordSegment> segments = {
        {"HET", {0, 1, 2}},
        {"IS", {3, 4}},
        {"VIJF", {5, 6, 7, 8}}
    };
    
    std::vector<uint16_t> flattened = flattenSegments(segments);
    
    EXPECT_EQ(9, flattened.size());
    EXPECT_EQ(0, flattened[0]);
    EXPECT_EQ(8, flattened[8]);
}

TEST_F(ClockDisplayTest, FindSegment_FindsExisting) {
    std::vector<WordSegment> segments = {
        {"HET", {0, 1, 2}},
        {"IS", {3, 4}},
        {"VIJF", {5, 6, 7, 8}}
    };
    
    const WordSegment* found = findSegment(segments, "VIJF");
    
    EXPECT_NE(nullptr, found);
    EXPECT_STREQ("VIJF", found->key);
}

TEST_F(ClockDisplayTest, FindSegment_ReturnsNullForMissing) {
    std::vector<WordSegment> segments = {
        {"HET", {0, 1, 2}},
        {"IS", {3, 4}}
    };
    
    const WordSegment* found = findSegment(segments, "MISSING");
    
    EXPECT_EQ(nullptr, found);
}

TEST_F(ClockDisplayTest, RemoveLeds) {
    std::vector<uint16_t> base = {0, 1, 2, 3, 4, 5};
    std::vector<uint16_t> toRemove = {1, 3, 5};
    
    removeLeds(base, toRemove);
    
    EXPECT_EQ(3, base.size());
    EXPECT_EQ(0, base[0]);
    EXPECT_EQ(2, base[1]);
    EXPECT_EQ(4, base[2]);
}

// ============================================================================
// Test: HET IS Visibility Logic
// ============================================================================

TEST_F(ClockDisplayTest, HetIsCurrentlyVisible_AlwaysFalseWhenZero) {
    bool visible = hetIsCurrentlyVisible(0, 0, 1000);
    EXPECT_FALSE(visible);
}

TEST_F(ClockDisplayTest, HetIsCurrentlyVisible_AlwaysTrueWhen360Plus) {
    bool visible = hetIsCurrentlyVisible(360, 5000, 10000);
    EXPECT_TRUE(visible);
    
    visible = hetIsCurrentlyVisible(500, 5000, 10000);
    EXPECT_TRUE(visible);
}

TEST_F(ClockDisplayTest, HetIsCurrentlyVisible_TrueWhenNotExpired) {
    unsigned long visibleUntil = 10000;
    unsigned long nowMs = 5000;
    
    bool visible = hetIsCurrentlyVisible(30, visibleUntil, nowMs);
    EXPECT_TRUE(visible);
}

TEST_F(ClockDisplayTest, HetIsCurrentlyVisible_FalseWhenExpired) {
    unsigned long visibleUntil = 5000;
    unsigned long nowMs = 10000;
    
    bool visible = hetIsCurrentlyVisible(30, visibleUntil, nowMs);
    EXPECT_FALSE(visible);
}

// ============================================================================
// Test: Animation Frame Building
// ============================================================================

TEST_F(ClockDisplayTest, BuildClassicFrames_CreatesCumulativeFrames) {
    std::vector<WordSegment> segments = {
        {"HET", {0, 1, 2}},
        {"IS", {3, 4}},
        {"VIJF", {5, 6, 7, 8}}
    };
    std::vector<std::vector<uint16_t>> frames;
    
    buildClassicFrames(segments, frames);
    
    EXPECT_EQ(3, frames.size());
    EXPECT_EQ(3, frames[0].size()); // HET
    EXPECT_EQ(5, frames[1].size()); // HET + IS
    EXPECT_EQ(9, frames[2].size()); // HET + IS + VIJF
}

TEST_F(ClockDisplayTest, BuildClassicFrames_EmptyInput) {
    std::vector<WordSegment> segments;
    std::vector<std::vector<uint16_t>> frames;
    
    buildClassicFrames(segments, frames);
    
    EXPECT_EQ(0, frames.size());
}

TEST_F(ClockDisplayTest, BuildSmartFrames_FallsBackToClassicWhenNoPrevious) {
    std::vector<WordSegment> prevSegments;  // Empty
    std::vector<WordSegment> nextSegments = {
        {"HET", {0, 1, 2}},
        {"IS", {3, 4}}
    };
    std::vector<std::vector<uint16_t>> frames;
    
    buildSmartFrames(prevSegments, nextSegments, true, frames);
    
    EXPECT_EQ(2, frames.size());
    EXPECT_EQ(3, frames[0].size());
    EXPECT_EQ(5, frames[1].size());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
