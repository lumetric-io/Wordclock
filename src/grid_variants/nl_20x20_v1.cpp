#include "grid_variants/nl_20x20_v1.h"

// Dutch 20x20 V1 layout - no HET/IS words
const uint16_t LED_COUNT_GRID_NL_20x20_V1 = 106;
const uint16_t LED_COUNT_EXTRA_NL_20x20_V1 = 0;
const uint16_t LED_COUNT_TOTAL_NL_20x20_V1 = LED_COUNT_GRID_NL_20x20_V1 + LED_COUNT_EXTRA_NL_20x20_V1;

const char* const LETTER_GRID_NL_20x20_V1[] = {
  "T I E N K W A R T",
  "V I J F W V O O R",
  "O V E R T H A L F",
  "Z E V E N T W E E",  
  "E E N Y D R I E Z",
  "V I E R V I J F E",
  "A C H T T I E N S",
  "T W A A L F E L F",
  "N E G E N X U U R",
};

const WordPosition WORDS_NL_20x20_V1[] = {
  { "VIJF_M",      { 21, 20, 19, 18 } },
  { "TIEN_M",      { 1, 2, 3, 4 } },
  { "OVER",        { 25, 26, 27, 28 } },
  { "VOOR",        { 16, 15, 14, 13 } },
  { "KWART",       { 5, 6, 7, 8, 9 } },
  { "HALF",        { 30, 31, 32, 33 } },
  { "UUR",         { 103, 104, 105 } },
  { "EEN",         { 49, 50, 51 } },
  { "TWEE",        { 40, 39, 38, 37 } },
  { "DRIE",        { 53, 54, 55, 56 } },
  { "VIER",        { 69, 68, 67, 66 } },
  { "VIJF",        { 65, 64, 63, 62 } },
  { "ZES",         { 57, 61, 81 } },
  { "ZEVEN",       { 45, 44, 43, 42, 41 } },
  { "ACHT",        { 73, 74, 75, 76 } },
  { "NEGEN",       { 97, 98, 99, 100, 101 } },
  { "TIEN",        { 77, 78, 79, 80 } },
  { "ELF",         { 87, 86, 85 } },
  { "TWAALF",      { 93, 92, 91, 90, 89, 88 } }
};

const size_t WORDS_NL_20x20_V1_COUNT = sizeof(WORDS_NL_20x20_V1) / sizeof(WORDS_NL_20x20_V1[0]);

const uint16_t EXTRA_MINUTES_NL_20x20_V1[] = {};
const size_t EXTRA_MINUTES_NL_20x20_V1_COUNT = sizeof(EXTRA_MINUTES_NL_20x20_V1) / sizeof(EXTRA_MINUTES_NL_20x20_V1[0]);

// LEDs to blink when no time is available (e.g. NTP sync failed)
const uint16_t NO_TIME_INDICATOR_LEDS_NL_20x20_V1[] = {
  102
};
const size_t NO_TIME_INDICATOR_LEDS_NL_20x20_V1_COUNT = sizeof(NO_TIME_INDICATOR_LEDS_NL_20x20_V1) / sizeof(NO_TIME_INDICATOR_LEDS_NL_20x20_V1[0]);
