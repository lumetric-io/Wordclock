#include "grid_variants/nl_20x20_v0.h"

// Dutch 20x20 V0 layout - earlier hardware revision of wordclock-mini where
// LED index 0 is dark and the strip is shifted +1 relative to nl_20x20_v1.
const uint16_t LED_COUNT_GRID_NL_20x20_V0 = 106;
const uint16_t LED_COUNT_EXTRA_NL_20x20_V0 = 0;
const uint16_t LED_COUNT_TOTAL_NL_20x20_V0 = LED_COUNT_GRID_NL_20x20_V0 + LED_COUNT_EXTRA_NL_20x20_V0;

const char* const LETTER_GRID_NL_20x20_V0[] = {
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

const WordPosition WORDS_NL_20x20_V0[] = {
  WPOS("VIJF_M",      21, 20, 19, 18),
  WPOS("TIEN_M",      1, 2, 3, 4),
  WPOS("OVER",        25, 26, 27, 28),
  WPOS("VOOR",        16, 15, 14, 13),
  WPOS("KWART",       5, 6, 7, 8, 9),
  WPOS("HALF",        30, 31, 32, 33),
  WPOS("UUR",         103, 104, 105),
  WPOS("EEN",         49, 50, 51),
  WPOS("TWEE",        40, 39, 38, 37),
  WPOS("DRIE",        53, 54, 55, 56),
  WPOS("VIER",        69, 68, 67, 66),
  WPOS("VIJF",        65, 64, 63, 62),
  WPOS("ZES",         57, 61, 81),
  WPOS("ZEVEN",       45, 44, 43, 42, 41),
  WPOS("ACHT",        73, 74, 75, 76),
  WPOS("NEGEN",       97, 98, 99, 100, 101),
  WPOS("TIEN",        77, 78, 79, 80),
  WPOS("ELF",         87, 86, 85),
  WPOS("TWAALF",      93, 92, 91, 90, 89, 88),
};

const size_t WORDS_NL_20x20_V0_COUNT = sizeof(WORDS_NL_20x20_V0) / sizeof(WORDS_NL_20x20_V0[0]);

const uint16_t EXTRA_MINUTES_NL_20x20_V0[] = {};
const size_t EXTRA_MINUTES_NL_20x20_V0_COUNT = sizeof(EXTRA_MINUTES_NL_20x20_V0) / sizeof(EXTRA_MINUTES_NL_20x20_V0[0]);
