#include "grid_variants/nl_20x20_v1.h"

// Dutch 20x20 V1 layout - no HET/IS words
const uint16_t LED_COUNT_GRID_NL_20x20_V1 = 105;
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
  { "VIJF_M",      { 20, 19, 18, 17 } },
  { "TIEN_M",      { 0, 1, 2, 3 } },
  { "OVER",        { 24, 25, 26, 27 } },
  { "VOOR",        { 15, 14, 13, 12 } },
  { "KWART",       { 4, 5, 6, 7, 8 } },
  { "HALF",        { 29, 30, 31, 32 } },
  { "UUR",         { 102, 103, 104 } },
  { "EEN",         { 48, 49, 50 } },
  { "TWEE",        { 39, 38, 37, 36 } },
  { "DRIE",        { 52, 53, 54, 55 } },
  { "VIER",        { 68, 67, 66, 65 } },
  { "VIJF",        { 64, 63, 62, 61 } },
  { "ZES",         { 56, 60, 80 } },
  { "ZEVEN",       { 44, 43, 42, 41, 40 } },
  { "ACHT",        { 72, 73, 74, 75 } },
  { "NEGEN",       { 96, 97, 98, 99, 100 } },
  { "TIEN",        { 76, 77, 78, 79 } },
  { "ELF",         { 86, 85, 84 } },
  { "TWAALF",      { 92, 91, 90, 89, 88, 87 } }
};

const size_t WORDS_NL_20x20_V1_COUNT = sizeof(WORDS_NL_20x20_V1) / sizeof(WORDS_NL_20x20_V1[0]);

const uint16_t EXTRA_MINUTES_NL_20x20_V1[] = {};
const size_t EXTRA_MINUTES_NL_20x20_V1_COUNT = sizeof(EXTRA_MINUTES_NL_20x20_V1) / sizeof(EXTRA_MINUTES_NL_20x20_V1[0]);
