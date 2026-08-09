#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

// Include mocks before production code
#include "../mocks/mock_arduino.h"
#include "../mocks/mock_grid_layout.h"
#include "../mocks/mock_time.h"

#include "../../src/log.cpp"
#include "../../src/time_mapper.cpp"

// The German variant under test. Its rule tables come along with it.
#include "../../src/grid_variants/de_50x50_v1.cpp"

// Runs the phrase engine against the German plate instead of the Dutch test
// grid. Both grids are swapped in wholesale, exactly as grid_layout does at
// boot, so this exercises the production tables and the production word list.
class GermanPhraseTest : public ::testing::Test {
protected:
    void SetUp() override {
        ACTIVE_WORDS = WORDS_DE_50x50_V1;
        ACTIVE_WORD_COUNT = WORDS_DE_50x50_V1_COUNT;
        EXTRA_MINUTE_LEDS = EXTRA_MINUTES_DE_50x50_V1;
        EXTRA_MINUTE_LED_COUNT = EXTRA_MINUTES_DE_50x50_V1_COUNT;
        ACTIVE_PHRASE_RULES = &DE_RULES_STANDARD;
    }

    void TearDown() override {
        ACTIVE_WORDS = WORDS_TEST;
        ACTIVE_WORD_COUNT = WORDS_TEST_COUNT;
        EXTRA_MINUTE_LEDS = EXTRA_MINUTES_TEST;
        EXTRA_MINUTE_LED_COUNT = EXTRA_MINUTES_TEST_COUNT;
        ACTIVE_PHRASE_RULES = &PHRASE_RULES_NL;
    }

    static std::vector<std::string> keysAt(int hour, int minute) {
        struct tm t {};
        t.tm_hour = hour;
        t.tm_min = minute;
        std::vector<std::string> keys;
        for (const auto& seg : get_word_segments_with_keys(&t)) {
            keys.push_back(seg.key);
        }
        return keys;
    }
};

// --------------------------------------------------------------------------
// Every slot a rule table names must exist on the plate
// --------------------------------------------------------------------------
// This is the check that actually protects a new variant: a table asking for
// MIN_20 or THREEQUARTER on a plate that lacks the word would silently show a
// phrase with a missing word instead of failing loudly.

static void expectAllSlotsExist(const PhraseRules& rules) {
    for (int step = 0; step < 12; ++step) {
        for (const char* slot : rules.steps[step].slots) {
            if (!slot) continue;
            EXPECT_NE(nullptr, find_word(slot))
                << rules.id << " step :" << (step * 5) << " wants missing slot " << slot;
        }
        if (rules.steps[step].withOClock) {
            EXPECT_NE(nullptr, find_word("OCLOCK"))
                << rules.id << " step :" << (step * 5) << " wants missing slot OCLOCK";
        }
    }
    for (int h = 0; h < 12; ++h) {
        EXPECT_NE(nullptr, find_word(phraseHourKey(h)))
            << rules.id << " misses hour word " << phraseHourKey(h);
    }
}

TEST_F(GermanPhraseTest, StandardRules_AllSlotsPresentOnPlate) {
    expectAllSlotsExist(DE_RULES_STANDARD);
}

TEST_F(GermanPhraseTest, SuedRules_AllSlotsPresentOnPlate) {
    ACTIVE_PHRASE_RULES = &DE_RULES_SUED;
    expectAllSlotsExist(DE_RULES_SUED);
}

TEST_F(GermanPhraseTest, PrefixAlwaysPresent) {
    for (int m = 0; m < 60; ++m) {
        auto keys = keysAt(9, m);
        ASSERT_GE(keys.size(), 3u);
        EXPECT_EQ("PREFIX_A", keys[0]);
        EXPECT_EQ("PREFIX_B", keys[1]);
    }
}

// --------------------------------------------------------------------------
// Nord / Hochdeutsch phrasing
// --------------------------------------------------------------------------

TEST_F(GermanPhraseTest, Standard_FullHour) {
    // "es ist drei Uhr" — hour word, then UHR
    EXPECT_EQ(std::vector<std::string>({"PREFIX_A", "PREFIX_B", "H_3", "OCLOCK"}),
              keysAt(15, 0));
}

TEST_F(GermanPhraseTest, Standard_TwentyPast) {
    // "es ist zwanzig nach drei" — not "zehn vor halb vier"
    EXPECT_EQ(std::vector<std::string>({"PREFIX_A", "PREFIX_B", "MIN_20", "PAST", "H_3"}),
              keysAt(15, 20));
}

TEST_F(GermanPhraseTest, Standard_QuarterPastKeepsCurrentHour) {
    EXPECT_EQ(std::vector<std::string>({"PREFIX_A", "PREFIX_B", "QUARTER", "PAST", "H_3"}),
              keysAt(15, 15));
}

TEST_F(GermanPhraseTest, Standard_HalfNamesNextHour) {
    // German "halb vier" is 15:30 — the hour word rolls over.
    EXPECT_EQ(std::vector<std::string>({"PREFIX_A", "PREFIX_B", "HALF", "H_4"}),
              keysAt(15, 30));
}

TEST_F(GermanPhraseTest, Standard_TwentyTo) {
    EXPECT_EQ(std::vector<std::string>({"PREFIX_A", "PREFIX_B", "MIN_20", "TO", "H_4"}),
              keysAt(15, 40));
}

TEST_F(GermanPhraseTest, Standard_QuarterTo) {
    EXPECT_EQ(std::vector<std::string>({"PREFIX_A", "PREFIX_B", "QUARTER", "TO", "H_4"}),
              keysAt(15, 45));
}

// --------------------------------------------------------------------------
// Süd-Ost phrasing — the four steps that differ
// --------------------------------------------------------------------------

TEST_F(GermanPhraseTest, Sued_QuarterNamesNextHour) {
    ACTIVE_PHRASE_RULES = &DE_RULES_SUED;
    // "viertel vier" = 15:15
    EXPECT_EQ(std::vector<std::string>({"PREFIX_A", "PREFIX_B", "QUARTER", "H_4"}),
              keysAt(15, 15));
}

TEST_F(GermanPhraseTest, Sued_TwentyPastGoesViaHalb) {
    ACTIVE_PHRASE_RULES = &DE_RULES_SUED;
    // "zehn vor halb vier"
    EXPECT_EQ(std::vector<std::string>({"PREFIX_A", "PREFIX_B", "MIN_10", "TO", "HALF", "H_4"}),
              keysAt(15, 20));
}

TEST_F(GermanPhraseTest, Sued_TwentyToGoesViaHalb) {
    ACTIVE_PHRASE_RULES = &DE_RULES_SUED;
    // "zehn nach halb vier"
    EXPECT_EQ(std::vector<std::string>({"PREFIX_A", "PREFIX_B", "MIN_10", "PAST", "HALF", "H_4"}),
              keysAt(15, 40));
}

TEST_F(GermanPhraseTest, Sued_ThreeQuarter) {
    ACTIVE_PHRASE_RULES = &DE_RULES_SUED;
    // "dreiviertel vier" = 15:45
    EXPECT_EQ(std::vector<std::string>({"PREFIX_A", "PREFIX_B", "THREEQUARTER", "H_4"}),
              keysAt(15, 45));
}

TEST_F(GermanPhraseTest, Dialects_AgreeOnEveryOtherStep) {
    for (int step = 0; step < 12; ++step) {
        if (step == 3 || step == 4 || step == 8 || step == 9) continue;  // :15 :20 :40 :45
        ACTIVE_PHRASE_RULES = &DE_RULES_STANDARD;
        auto standard = keysAt(15, step * 5);
        ACTIVE_PHRASE_RULES = &DE_RULES_SUED;
        EXPECT_EQ(standard, keysAt(15, step * 5)) << "dialects diverge at :" << (step * 5);
    }
}

// --------------------------------------------------------------------------
// EIN vs EINS
// --------------------------------------------------------------------------

TEST_F(GermanPhraseTest, OneOClockUsesAltHourWord) {
    // "es ist ein Uhr" — the shortened form, only when UHR is lit.
    auto keys = keysAt(13, 0);
    EXPECT_EQ(std::vector<std::string>({"PREFIX_A", "PREFIX_B", "H_1_ALT", "OCLOCK"}), keys);
}

TEST_F(GermanPhraseTest, FivePastOneUsesFullHourWord) {
    // "fünf nach eins" — no UHR, so the full form.
    EXPECT_EQ(std::vector<std::string>({"PREFIX_A", "PREFIX_B", "MIN_5", "PAST", "H_1"}),
              keysAt(13, 5));
}

TEST_F(GermanPhraseTest, AltHourWordIsDroppedWhenVariantHasNone) {
    // Only H_1 has an _ALT form on this plate; every other full hour falls
    // back to the plain hour word.
    for (int h = 0; h < 24; ++h) {
        if (h % 12 == 1) continue;
        auto keys = keysAt(h, 0);
        ASSERT_EQ(4u, keys.size());
        EXPECT_EQ(std::string(phraseHourKey(h % 12)), keys[2]);
        EXPECT_EQ("OCLOCK", keys[3]);
    }
}

// --------------------------------------------------------------------------
// Dutch behaviour is unchanged by the _ALT mechanism
// --------------------------------------------------------------------------

TEST_F(GermanPhraseTest, DutchNeverUsesAltHourWords) {
    TearDown();  // back to the Dutch test grid
    for (int h = 0; h < 24; ++h) {
        auto keys = keysAt(h, 0);
        for (const auto& k : keys) {
            EXPECT_EQ(std::string::npos, k.find("_ALT")) << "unexpected alt key " << k;
        }
    }
}

// --------------------------------------------------------------------------
// The plate itself
// --------------------------------------------------------------------------

TEST_F(GermanPhraseTest, NoWordExceedsTheStrip) {
    for (size_t w = 0; w < WORDS_DE_50x50_V1_COUNT; ++w) {
        const WordPosition& word = WORDS_DE_50x50_V1[w];
        for (int i = 0; i < word.count; ++i) {
            EXPECT_GE(word.indices[i], 0) << word.word;
            EXPECT_LT(word.indices[i], LED_COUNT_TOTAL_DE_50x50_V1) << word.word;
        }
    }
}

TEST_F(GermanPhraseTest, WordKeysAreUnique) {
    std::set<std::string> keys;
    for (size_t w = 0; w < WORDS_DE_50x50_V1_COUNT; ++w) {
        EXPECT_TRUE(keys.insert(WORDS_DE_50x50_V1[w].word).second)
            << "duplicate key " << WORDS_DE_50x50_V1[w].word;
    }
}

// Splits a grid row into code points. FÜNF and ZWÖLF carry two-byte letters,
// so a byte index is not a column index.
static std::vector<std::string> codePoints(const char* row) {
    std::vector<std::string> out;
    for (const char* p = row; *p; ++p) {
        if ((static_cast<unsigned char>(*p) & 0xC0) == 0x80 && !out.empty()) {
            out.back() += *p;  // continuation byte
        } else {
            out.push_back(std::string(1, *p));
        }
    }
    return out;
}

// The 50x50 panel runs boustrophedon over 13 LEDs per row: 11 letters plus a
// frame LED at each end.
static std::string letterAt(int ledIndex) {
    const int row = ledIndex / 13;
    const int off = ledIndex % 13;
    const int col = (row % 2 == 0) ? off - 1 : 11 - off;
    if (row > 9 || col < 0 || col > 10) return "?";
    auto cells = codePoints(LETTER_GRID_DE_50x50_V1[row]);
    if (col >= static_cast<int>(cells.size())) return "?";
    return cells[col];
}

// The check that keeps the plate honest: every word must actually spell itself
// at its LED indices. A single transposed index would otherwise show a garbled
// word on the wall and pass every other test in this file.
TEST_F(GermanPhraseTest, EveryWordSpellsItselfOnThePlate) {
    const struct { const char* key; const char* word; } EXPECTED[] = {
        {"PREFIX_A", "ES"},        {"PREFIX_B", "IST"},
        {"MIN_5", "FÜNF"},         {"MIN_10", "ZEHN"},
        {"MIN_20", "ZWANZIG"},     {"QUARTER", "VIERTEL"},
        {"THREEQUARTER", "DREIVIERTEL"}, {"HALF", "HALB"},
        {"TO", "VOR"},             {"PAST", "NACH"},
        {"H_1", "EINS"},           {"H_1_ALT", "EIN"},
        {"H_2", "ZWEI"},           {"H_3", "DREI"},
        {"H_4", "VIER"},           {"H_5", "FÜNF"},
        {"H_6", "SECHS"},          {"H_7", "SIEBEN"},
        {"H_8", "ACHT"},           {"H_9", "NEUN"},
        {"H_10", "ZEHN"},          {"H_11", "ELF"},
        {"H_12", "ZWÖLF"},         {"OCLOCK", "UHR"},
    };

    for (const auto& e : EXPECTED) {
        const WordPosition* w = find_word(e.key);
        ASSERT_NE(nullptr, w) << e.key << " missing from the plate";
        std::string spelled;
        for (int i = 0; i < w->count; ++i) spelled += letterAt(w->indices[i]);
        EXPECT_EQ(std::string(e.word), spelled) << e.key << " does not spell " << e.word;
    }

    // And nothing is left over: every key on the plate is covered above.
    EXPECT_EQ(sizeof(EXPECTED) / sizeof(EXPECTED[0]), WORDS_DE_50x50_V1_COUNT);
}

TEST_F(GermanPhraseTest, LetterGridIsElevenCellsWide) {
    for (int row = 0; row < 10; ++row) {
        EXPECT_EQ(11u, codePoints(LETTER_GRID_DE_50x50_V1[row]).size())
            << "row " << row << " is not 11 cells wide";
    }
}

TEST_F(GermanPhraseTest, MinuteDotsSitOutsideTheLetterGrid) {
    for (size_t i = 0; i < EXTRA_MINUTES_DE_50x50_V1_COUNT; ++i) {
        EXPECT_GE(EXTRA_MINUTES_DE_50x50_V1[i], LED_COUNT_GRID_DE_50x50_V1);
        EXPECT_LT(EXTRA_MINUTES_DE_50x50_V1[i], LED_COUNT_TOTAL_DE_50x50_V1);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
