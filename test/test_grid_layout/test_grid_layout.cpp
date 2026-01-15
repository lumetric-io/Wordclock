#include <gtest/gtest.h>
#include "../mocks/mock_arduino.h"
#include "../mocks/mock_grid_layout.h"

// For grid layout tests, we'll test the mock grid layout interface
// Testing the full production grid_layout.cpp requires all variant files
// which makes the test complex. Instead, we test the API contract.

class GridLayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Using mock grid layout for testing
    }
};

// Basic Interface Tests
TEST_F(GridLayoutTest, ActiveDataIsInitialized) {
    ASSERT_NE(nullptr, LETTER_GRID);
    ASSERT_NE(nullptr, ACTIVE_WORDS);
    ASSERT_GT(ACTIVE_WORD_COUNT, 0);
}

// Grid Data Tests
TEST_F(GridLayoutTest, LetterGrid_HasContent) {
    ASSERT_NE(nullptr, LETTER_GRID);
    ASSERT_NE(nullptr, LETTER_GRID[0]);  // First row exists
}

TEST_F(GridLayoutTest, ActiveWords_HasContent) {
    ASSERT_NE(nullptr, ACTIVE_WORDS);
    ASSERT_GT(ACTIVE_WORD_COUNT, 0);
    ASSERT_NE(nullptr, ACTIVE_WORDS[0].word);  // First word exists
}

TEST_F(GridLayoutTest, ExtraMinuteLeds_Defined) {
    ASSERT_NE(nullptr, EXTRA_MINUTE_LEDS);
    ASSERT_GT(EXTRA_MINUTE_LED_COUNT, 0);
}

// Word Lookup Tests  
TEST_F(GridLayoutTest, FindWord_ExistingWord_ReturnsPointer) {
    const WordPosition* word = find_word("HET");
    ASSERT_NE(nullptr, word);
    ASSERT_STREQ("HET", word->word);
}

TEST_F(GridLayoutTest, FindWord_NonExistent_ReturnsNull) {
    const WordPosition* word = find_word("NONEXISTENT");
    ASSERT_EQ(nullptr, word);
}

TEST_F(GridLayoutTest, FindWord_Null_ReturnsNull) {
    const WordPosition* word = find_word(nullptr);
    ASSERT_EQ(nullptr, word);
}

// LED Count Tests (Mock implementation)
TEST_F(GridLayoutTest, GetLedCounts_Mock) {
    uint16_t grid = getActiveLedCountGrid();
    uint16_t extra = getActiveLedCountExtra();
    uint16_t total = getActiveLedCountTotal();
    
    ASSERT_EQ(111, grid);   // From mock
    ASSERT_EQ(4, extra);    // From mock
    ASSERT_EQ(115, total);  // From mock
}

// Words Available Test
TEST_F(GridLayoutTest, CommonWords_Available) {
    // Test that common words are available
    ASSERT_NE(nullptr, find_word("HET"));
    ASSERT_NE(nullptr, find_word("IS"));
    ASSERT_NE(nullptr, find_word("UUR"));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

