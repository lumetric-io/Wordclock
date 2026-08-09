#ifndef MOCK_GRID_LAYOUT_H
#define MOCK_GRID_LAYOUT_H

#include <vector>
#include <cstring>
#include "../../src/wordposition.h"

// Real phrase-rule data — the tables are plain data with no Arduino/ESP
// dependency, so tests exercise the production tables rather than a copy.
// Every test dir is a single translation unit, so pulling in the .cpp here is
// safe and saves each test from including it separately.
#include "../../src/phrase_rules.cpp"

// Simple test grid for unit testing (Dutch word clock)
const char* const LETTER_GRID_TEST[] = {
    "HETLISAVIJF",
    "TIENBTZVOOR",
    "OVERMEKWART",
    "HALFSPWOVER",
    "VOORTHALF*E",
    "EENCTWEEDRI",
    "VIERSVIJFZE",
    "ZEVENONEGEN",
    "ACHTTIENTIEN",
    "ELFTWAALFUUR",
    "***********",
    nullptr
};

// Test word definitions - minimal set for testing
const WordPosition WORDS_TEST[] = {
    WPOS("PREFIX_A", 1, 2, 3),                      // HET
    WPOS("PREFIX_B", 5, 6),                         // IS
    WPOS("MIN_5",    8, 9, 10, 11),                 // VIJF_M
    WPOS("MIN_10",   12, 13, 14, 15),               // TIEN_M
    WPOS("TO",       19, 20, 21, 22),               // VOOR
    WPOS("PAST",     23, 24, 25, 26),               // OVER
    WPOS("QUARTER",  29, 30, 31, 32, 33),           // KWART
    WPOS("HALF",     34, 35, 36, 37),               // HALF
    WPOS("H_1",      56, 57, 58),                   // EEN
    WPOS("H_2",      60, 61, 62, 63),               // TWEE
    WPOS("H_3",      64, 65, 66, 67),               // DRIE
    WPOS("H_4",      67, 68, 69, 70),               // VIER
    WPOS("H_5",      72, 73, 74, 75),               // VIJF
    WPOS("H_6",      76, 77, 78),                   // ZES
    WPOS("H_7",      78, 79, 80, 81, 82),           // ZEVEN
    WPOS("H_8",      89, 90, 91, 92),               // ACHT
    WPOS("H_9",      85, 86, 87, 88, 89),           // NEGEN
    WPOS("H_10",     93, 94, 95, 96),               // TIEN
    WPOS("H_11",     100, 101, 102),                // ELF
    WPOS("H_12",     103, 104, 105, 106, 107, 108), // TWAALF
    WPOS("OCLOCK",   109, 110, 111),                // UUR
};

const size_t WORDS_TEST_COUNT = sizeof(WORDS_TEST) / sizeof(WORDS_TEST[0]);

const uint16_t EXTRA_MINUTES_TEST[] = {111, 112, 113, 114};
const size_t EXTRA_MINUTES_TEST_COUNT = 4;

// Active layout data (global variables for testing)
const char* const* LETTER_GRID = LETTER_GRID_TEST;
const WordPosition* ACTIVE_WORDS = WORDS_TEST;
size_t ACTIVE_WORD_COUNT = WORDS_TEST_COUNT;
const uint16_t* EXTRA_MINUTE_LEDS = EXTRA_MINUTES_TEST;
size_t EXTRA_MINUTE_LED_COUNT = EXTRA_MINUTES_TEST_COUNT;
// Group size of 1 = each minute LED is its own symbol (matches the legacy
// per-LED behavior). Tests that exercise time mapping don't depend on the
// grouped variant, so 1 keeps the existing assertions valid.
size_t EXTRA_MINUTE_LED_GROUP_SIZE = 1;

// Stub for the LED-events system. time_mapper.cpp consults this when
// LED_STATUS_EVENT_USE_MINUTE_LEDS is enabled (defaults to 1) to suppress
// minute LEDs while a status event is animating. Tests don't drive the
// events system, so reporting "no event active" is correct.
inline bool ledEventIsActive() { return false; }

// Helper to find a word in the test grid
inline const WordPosition* find_word(const char* name) {
    if (!name) return nullptr;  // Handle nullptr input
    for (size_t i = 0; i < ACTIVE_WORD_COUNT; ++i) {
        if (strcmp(ACTIVE_WORDS[i].word, name) == 0) {
            return &ACTIVE_WORDS[i];
        }
    }
    return nullptr;
}

// The test grid is Dutch, so it runs the Dutch rule table by default. Tests
// that swap ACTIVE_WORDS for another variant swap this along with it.
const PhraseRules* ACTIVE_PHRASE_RULES = &PHRASE_RULES_NL;
inline const PhraseRules* getActivePhraseRules() { return ACTIVE_PHRASE_RULES; }

// Mock grid functions
inline uint16_t getActiveLedCountGrid() { return 111; }
inline uint16_t getActiveLedCountExtra() { return 4; }
inline uint16_t getActiveLedCountTotal() { return 115; }

#endif // MOCK_GRID_LAYOUT_H
