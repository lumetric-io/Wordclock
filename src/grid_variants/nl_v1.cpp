#include "grid_variants/nl_v1.h"

// Original grid - 4 leds on side to make the turns


const uint16_t LED_COUNT_GRID_NL_V1 = 146;
const uint16_t LED_COUNT_EXTRA_NL_V1 = 15;
const uint16_t LED_COUNT_TOTAL_NL_V1 = LED_COUNT_GRID_NL_V1 + LED_COUNT_EXTRA_NL_V1;

const char* const LETTER_GRID_NL_V1[] = {
  "HETBISWYBRC",
  "RTIENMMUHLC",
  "VIJFCWKWART",
  "OVERXTTXLVB",
  "QKEVOORTFIG",
  "DRIEKBZEVEN",
  "VTTIENELNRC",
  "TWAALFSFRSF",
  "EENEGENACHT",
  "XEVIJFJXUUR",
  "..-.-.-.-.."
};

const uint16_t EXTRA_MINUTES_NL_V1[] = {
  static_cast<uint16_t>(LED_COUNT_GRID_NL_V1 + 7),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_V1 + 9),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_V1 + 11),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_V1 + 13)
};

const WordPosition WORDS_NL_V1[] = {
  WPOS("HET",         1, 2, 3),
  WPOS("IS",          5, 6),
  WPOS("VIJF_M",      31, 32, 33, 34),
  WPOS("TIEN_M",      25, 24, 23, 22),
  WPOS("OVER",        56, 55, 54, 53),
  WPOS("VOOR",        64, 65, 66, 67),
  WPOS("KWART",       37, 38, 39, 40, 41),
  WPOS("HALF",        18, 39, 48, 69),
  WPOS("UUR",         138, 137, 136),
  WPOS("EEN",         121, 122, 123),
  WPOS("TWEE",        92, 115, 122, 145),
  WPOS("DRIE",        86, 85, 84, 83),
  WPOS("VIER",        47, 70, 77, 100),
  WPOS("VIJF",        144, 143, 142, 141),
  WPOS("ZES",         80, 97, 110),
  WPOS("ZEVEN",       80, 79, 78, 77, 76),
  WPOS("ACHT",        128, 129, 130, 131),
  WPOS("NEGEN",       123, 124, 125, 126, 127),
  WPOS("TIEN",        93, 94, 95, 96),
  WPOS("ELF",         79, 98, 109),
  WPOS("TWAALF",      116, 115, 114, 113, 112, 111),
};

const size_t WORDS_NL_V1_COUNT = sizeof(WORDS_NL_V1) / sizeof(WORDS_NL_V1[0]);
const size_t EXTRA_MINUTES_NL_V1_COUNT = sizeof(EXTRA_MINUTES_NL_V1) / sizeof(EXTRA_MINUTES_NL_V1[0]);
