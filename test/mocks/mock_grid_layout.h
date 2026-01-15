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
// Note: indices use 1-based for word detection, 0 = terminator
const WordPosition WORDS_TEST[] = {
    {"HET", {1, 2, 3, 0}},
    {"IS", {5, 6, 0}},
    {"VIJF_M", {8, 9, 10, 11, 0}},
    {"TIEN_M", {12, 13, 14, 15, 0}},
    {"VOOR", {19, 20, 21, 22, 0}},
    {"OVER", {23, 24, 25, 26, 0}},
    {"KWART", {29, 30, 31, 32, 33, 0}},
    {"HALF", {34, 35, 36, 37, 0}},
    {"EEN", {56, 57, 58, 0}},
    {"TWEE", {60, 61, 62, 63, 0}},
    {"DRIE", {64, 65, 66, 67, 0}},
    {"VIER", {67, 68, 69, 70, 0}},
    {"VIJF", {72, 73, 74, 75, 0}},
    {"ZES", {76, 77, 78, 0}},
    {"ZEVEN", {78, 79, 80, 81, 82, 0}},
    {"ACHT", {89, 90, 91, 92, 0}},
    {"NEGEN", {85, 86, 87, 88, 89, 0}},
    {"TIEN", {93, 94, 95, 96, 0}},
    {"ELF", {100, 101, 102, 0}},
    {"TWAALF", {103, 104, 105, 106, 107, 108, 0}},
    {"UUR", {109, 110, 111, 0}},
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

