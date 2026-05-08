#ifndef MOCK_GRID_LAYOUT_H
#define MOCK_GRID_LAYOUT_H

#include <vector>
#include <cstring>
#include "../../src/wordposition.h"

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
    WPOS("HET",    1, 2, 3),
    WPOS("IS",     5, 6),
    WPOS("VIJF_M", 8, 9, 10, 11),
    WPOS("TIEN_M", 12, 13, 14, 15),
    WPOS("VOOR",   19, 20, 21, 22),
    WPOS("OVER",   23, 24, 25, 26),
    WPOS("KWART",  29, 30, 31, 32, 33),
    WPOS("HALF",   34, 35, 36, 37),
    WPOS("EEN",    56, 57, 58),
    WPOS("TWEE",   60, 61, 62, 63),
    WPOS("DRIE",   64, 65, 66, 67),
    WPOS("VIER",   67, 68, 69, 70),
    WPOS("VIJF",   72, 73, 74, 75),
    WPOS("ZES",    76, 77, 78),
    WPOS("ZEVEN",  78, 79, 80, 81, 82),
    WPOS("ACHT",   89, 90, 91, 92),
    WPOS("NEGEN",  85, 86, 87, 88, 89),
    WPOS("TIEN",   93, 94, 95, 96),
    WPOS("ELF",    100, 101, 102),
    WPOS("TWAALF", 103, 104, 105, 106, 107, 108),
    WPOS("UUR",    109, 110, 111),
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

// Mock grid functions
inline uint16_t getActiveLedCountGrid() { return 111; }
inline uint16_t getActiveLedCountExtra() { return 4; }
inline uint16_t getActiveLedCountTotal() { return 115; }

#endif // MOCK_GRID_LAYOUT_H
