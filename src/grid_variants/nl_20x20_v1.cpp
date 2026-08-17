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
  WPOS("MIN_5",   20, 19, 18, 17),         // VIJF_M
  WPOS("MIN_10",  0, 1, 2, 3),             // TIEN_M
  WPOS("PAST",    24, 25, 26, 27),         // OVER
  WPOS("TO",      15, 14, 13, 12),         // VOOR
  WPOS("QUARTER", 4, 5, 6, 7, 8),          // KWART
  WPOS("HALF",    29, 30, 31, 32),         // HALF
  WPOS("OCLOCK",  102, 103, 104),          // UUR
  WPOS("H_1",     48, 49, 50),             // EEN
  WPOS("H_2",     39, 38, 37, 36),         // TWEE
  WPOS("H_3",     52, 53, 54, 55),         // DRIE
  WPOS("H_4",     68, 67, 66, 65),         // VIER
  WPOS("H_5",     64, 63, 62, 61),         // VIJF
  WPOS("H_6",     56, 60, 80),             // ZES
  WPOS("H_7",     44, 43, 42, 41, 40),     // ZEVEN
  WPOS("H_8",     72, 73, 74, 75),         // ACHT
  WPOS("H_9",     96, 97, 98, 99, 100),    // NEGEN
  WPOS("H_10",    76, 77, 78, 79),         // TIEN
  WPOS("H_11",    86, 85, 84),             // ELF
  WPOS("H_12",    92, 91, 90, 89, 88, 87), // TWAALF
};

const size_t WORDS_NL_20x20_V1_COUNT = sizeof(WORDS_NL_20x20_V1) / sizeof(WORDS_NL_20x20_V1[0]);

const uint16_t EXTRA_MINUTES_NL_20x20_V1[] = {};
const size_t EXTRA_MINUTES_NL_20x20_V1_COUNT = sizeof(EXTRA_MINUTES_NL_20x20_V1) / sizeof(EXTRA_MINUTES_NL_20x20_V1[0]);
