#include <gtest/gtest.h>

#include <set>
#include <string>

#include "../mocks/mock_arduino.h"

// The real grid_layout, compiled with every variant enabled. No product_config.h
// is reachable from a native build, so grid_layout.cpp falls back to enabling
// all of them — which is exactly the multi-language configuration this suite
// needs, and the one no single product build can exercise on its own.
#include "../../src/phrase_rules.cpp"
#include "../../src/grid_variants/de_50x50_v1.cpp"
#include "../../src/grid_variants/nl_105x105_logo_v1.cpp"
#include "../../src/grid_variants/nl_20x20_v1.cpp"
#include "../../src/grid_variants/nl_50x50_v3.cpp"
#include "../../src/grid_variants/nl_55x50_logo_v1.cpp"
#include "../../src/grid_variants/nl_v4.cpp"
#include "../../src/grid_layout.cpp"

namespace {

// Every test starts from the state a freshly booted device is in: the first
// registered variant, its first dialect.
class LanguageTest : public ::testing::Test {
protected:
    void SetUp() override { resetToBuildDefault(); }
    void TearDown() override { resetToBuildDefault(); }

    static void resetToBuildDefault() {
        ASSERT_TRUE(setActiveGridVariant(GridVariant::NL_V4));
    }
};

std::set<std::string> languageSet() {
    std::set<std::string> codes;
    for (size_t i = 0; i < getLanguageCount(); ++i) {
        codes.insert(getLanguageCode(i));
    }
    return codes;
}

}  // namespace

// --------------------------------------------------------------------------
// Language listing
// --------------------------------------------------------------------------

TEST_F(LanguageTest, ListsEachLanguageOnce) {
    // Five Dutch variants and one German one collapse to two languages: the
    // customer picks a language, not a plate revision.
    EXPECT_EQ(2u, getLanguageCount());
    EXPECT_EQ(std::set<std::string>({"nl", "de"}), languageSet());
}

TEST_F(LanguageTest, LanguageCodeOutOfRangeIsNull) {
    EXPECT_EQ(nullptr, getLanguageCode(getLanguageCount()));
    EXPECT_EQ(nullptr, getLanguageCode(99));
}

TEST_F(LanguageTest, HasLanguageMatchesTheListing) {
    EXPECT_TRUE(hasLanguage("nl"));
    EXPECT_TRUE(hasLanguage("de"));
    EXPECT_FALSE(hasLanguage("fr"));
    EXPECT_FALSE(hasLanguage(nullptr));
    EXPECT_FALSE(hasLanguage(""));
}

// --------------------------------------------------------------------------
// Switching language
// --------------------------------------------------------------------------

TEST_F(LanguageTest, SwitchingLanguageSwapsThePlate) {
    ASSERT_STREQ("nl", getActiveLanguage());

    ASSERT_TRUE(setActiveLanguage("de"));
    EXPECT_STREQ("de", getActiveLanguage());
    EXPECT_EQ(GridVariant::DE_50x50_V1, getActiveGridVariant());
    EXPECT_NE(nullptr, find_word("MIN_20"));  // zwanzig — German-only slot

    ASSERT_TRUE(setActiveLanguage("nl"));
    EXPECT_STREQ("nl", getActiveLanguage());
    EXPECT_EQ(nullptr, find_word("MIN_20"));
}

TEST_F(LanguageTest, UnknownLanguageChangesNothing) {
    ASSERT_TRUE(setActiveLanguage("de"));
    EXPECT_FALSE(setActiveLanguage("fr"));
    EXPECT_STREQ("de", getActiveLanguage()) << "a rejected switch must not leave a half-applied plate";
    EXPECT_FALSE(setActiveLanguage(nullptr));
    EXPECT_STREQ("de", getActiveLanguage());
}

// --------------------------------------------------------------------------
// Dialects
// --------------------------------------------------------------------------

TEST_F(LanguageTest, EveryVariantHasAtLeastOneDialect) {
    // getActivePhraseRules() dereferences the active dialect on every render,
    // so a variant registered with an empty list would crash the clock.
    size_t count = 0;
    const GridVariantInfo* infos = getGridVariantInfos(count);
    for (size_t i = 0; i < count; ++i) {
        ASSERT_TRUE(setActiveGridVariant(infos[i].variant)) << infos[i].key;
        EXPECT_GE(getDialectCount(), 1u) << infos[i].key << " has no dialects";
        EXPECT_NE(nullptr, getActiveDialect()) << infos[i].key;
        EXPECT_NE(nullptr, getActivePhraseRules()) << infos[i].key;
    }
}

TEST_F(LanguageTest, DutchHasOneDialect) {
    EXPECT_EQ(1u, getDialectCount());
    EXPECT_STREQ("nl", getActiveDialect()->id);
    EXPECT_STREQ("nl", getActivePhraseRules()->id);
}

TEST_F(LanguageTest, GermanOffersBothDialects) {
    ASSERT_TRUE(setActiveLanguage("de"));
    ASSERT_EQ(2u, getDialectCount());

    EXPECT_STREQ("de-nord", getActiveDialect()->id) << "index 0 is the fallback";
    EXPECT_STREQ("de", getActivePhraseRules()->id);

    ASSERT_TRUE(setActiveDialect("de-sued"));
    EXPECT_STREQ("de-sued", getActiveDialect()->id);
    EXPECT_STREQ("de-sued", getActivePhraseRules()->id);
}

TEST_F(LanguageTest, DialectSamplesDifferWithinAVariant) {
    // The sample sentence is the only thing a customer picks a dialect on. Two
    // identical samples would make the choice unanswerable.
    ASSERT_TRUE(setActiveLanguage("de"));
    std::set<std::string> samples;
    for (size_t i = 0; i < getDialectCount(); ++i) {
        const ClockDialect* d = getDialect(i);
        ASSERT_NE(nullptr, d);
        ASSERT_NE(nullptr, d->sample);
        EXPECT_GT(std::string(d->sample).size(), 0u) << d->id << " has an empty sample";
        samples.insert(d->sample);
    }
    EXPECT_EQ(getDialectCount(), samples.size()) << "two dialects show the same sample";
}

TEST_F(LanguageTest, DialectIdsAreUniqueWithinAVariant) {
    size_t count = 0;
    const GridVariantInfo* infos = getGridVariantInfos(count);
    for (size_t i = 0; i < count; ++i) {
        ASSERT_TRUE(setActiveGridVariant(infos[i].variant));
        std::set<std::string> ids;
        for (size_t d = 0; d < getDialectCount(); ++d) {
            ids.insert(getDialect(d)->id);
        }
        EXPECT_EQ(getDialectCount(), ids.size()) << infos[i].key << " has duplicate dialect ids";
    }
}

TEST_F(LanguageTest, DialectOutOfRangeIsNull) {
    EXPECT_EQ(nullptr, getDialect(getDialectCount()));
    EXPECT_EQ(nullptr, getDialect(99));
}

TEST_F(LanguageTest, ForeignDialectIsRejected) {
    // The Dutch plate has no words for "dreiviertel"; accepting the id would
    // render a phrase with holes in it.
    EXPECT_FALSE(setActiveDialect("de-sued"));
    EXPECT_STREQ("nl", getActiveDialect()->id);
    EXPECT_FALSE(setActiveDialect(nullptr));
    EXPECT_FALSE(setActiveDialect("nonsense"));
}

TEST_F(LanguageTest, SwitchingLanguageResetsTheDialect) {
    ASSERT_TRUE(setActiveLanguage("de"));
    ASSERT_TRUE(setActiveDialect("de-sued"));

    ASSERT_TRUE(setActiveLanguage("nl"));
    EXPECT_STREQ("nl", getActiveDialect()->id);

    // Back to German: the previous dialect must not be resurrected, because
    // the index it was stored under means something different per variant.
    ASSERT_TRUE(setActiveLanguage("de"));
    EXPECT_STREQ("de-nord", getActiveDialect()->id);
}

// --------------------------------------------------------------------------
// Every dialect must be spellable on the plate it belongs to
// --------------------------------------------------------------------------
// test_phrase_rules checks this for the two German tables by hand. This is the
// version that cannot be forgotten: it walks whatever is registered, so a new
// variant or a third dialect is covered the moment it is added.

TEST_F(LanguageTest, EveryDialectOfEveryVariantSpellsItselfOnItsPlate) {
    size_t count = 0;
    const GridVariantInfo* infos = getGridVariantInfos(count);
    ASSERT_GT(count, 0u);

    for (size_t v = 0; v < count; ++v) {
        ASSERT_TRUE(setActiveGridVariant(infos[v].variant));
        const std::string plate = infos[v].key;

        for (size_t d = 0; d < getDialectCount(); ++d) {
            const ClockDialect* dialect = getDialect(d);
            ASSERT_TRUE(setActiveDialect(dialect->id)) << plate << " / " << dialect->id;
            const PhraseRules* rules = getActivePhraseRules();
            ASSERT_NE(nullptr, rules);

            const std::string where = plate + " / " + dialect->id;
            for (int step = 0; step < 12; ++step) {
                for (const char* slot : rules->steps[step].slots) {
                    if (!slot) continue;
                    EXPECT_NE(nullptr, find_word(slot))
                        << where << " step :" << (step * 5) << " wants missing slot " << slot;
                }
                if (rules->steps[step].withOClock) {
                    EXPECT_NE(nullptr, find_word("OCLOCK"))
                        << where << " step :" << (step * 5) << " wants missing slot OCLOCK";
                }
            }
            for (int h = 0; h < 12; ++h) {
                EXPECT_NE(nullptr, find_word(phraseHourKey(h)))
                    << where << " misses hour word " << phraseHourKey(h);
            }
            // The prefix is optional — the mini plate has no room for "HET IS"
            // and time_mapper simply skips words a plate lacks. What is not
            // allowed is half a prefix: the two halves animate separately, so
            // a plate with only one would light "HET" and never "IS".
            EXPECT_EQ(find_word("PREFIX_A") == nullptr, find_word("PREFIX_B") == nullptr)
                << where << " defines only one half of the prefix";
        }
    }
}

// --------------------------------------------------------------------------
// Variants that ship together must agree on the strip
// --------------------------------------------------------------------------

TEST_F(LanguageTest, GermanAndDutch50x50ShareTheirLedCounts) {
    // These two are the first pair to ship in one product. Switching language
    // must not change how long the strip is: the LED driver is sized once at
    // boot, and the customer's hardware does not change when they pick a
    // language. Every future language pair inherits this requirement.
    ASSERT_TRUE(setActiveGridVariant(GridVariant::NL_50x50_V3));
    const uint16_t nlGrid = getActiveLedCountGrid();
    const uint16_t nlExtra = getActiveLedCountExtra();
    const uint16_t nlTotal = getActiveLedCountTotal();

    ASSERT_TRUE(setActiveGridVariant(GridVariant::DE_50x50_V1));
    EXPECT_EQ(nlGrid, getActiveLedCountGrid());
    EXPECT_EQ(nlExtra, getActiveLedCountExtra());
    EXPECT_EQ(nlTotal, getActiveLedCountTotal());
}

TEST_F(LanguageTest, MinuteLedLayoutMatchesAcross50x50Languages) {
    ASSERT_TRUE(setActiveGridVariant(GridVariant::NL_50x50_V3));
    const size_t nlCount = EXTRA_MINUTE_LED_COUNT;
    const size_t nlGroup = EXTRA_MINUTE_LED_GROUP_SIZE;

    ASSERT_TRUE(setActiveGridVariant(GridVariant::DE_50x50_V1));
    EXPECT_EQ(nlCount, EXTRA_MINUTE_LED_COUNT);
    EXPECT_EQ(nlGroup, EXTRA_MINUTE_LED_GROUP_SIZE);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
