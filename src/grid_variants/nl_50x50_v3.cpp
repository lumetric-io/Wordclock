#include "grid_variants/nl_50x50_v3.h"

// Mirrors the NL_50x50_V2 layout; adjust when hardware wiring deviates.
const uint16_t LED_COUNT_GRID_NL_50x50_V3 = 128;
const uint16_t LED_COUNT_EXTRA_NL_50x50_V3 = 13;
const uint16_t LED_COUNT_TOTAL_NL_50x50_V3 = LED_COUNT_GRID_NL_50x50_V3 + LED_COUNT_EXTRA_NL_50x50_V3;

const char* const LETTER_GRID_NL_50x50_V3[] = {
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

const uint16_t EXTRA_MINUTES_NL_50x50_V3[] = {
  static_cast<uint16_t>(LED_COUNT_GRID_NL_50x50_V3 + 5),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_50x50_V3 + 7),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_50x50_V3 + 9),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_50x50_V3 + 11)
};

const WordPosition WORDS_NL_50x50_V3[] = {
  WPOS("HET",         1, 2, 3),
  WPOS("IS",          5, 6),
  WPOS("VIJF_M",      27, 28, 29, 30),
  WPOS("TIEN_M",      23, 22, 21, 20),
  WPOS("OVER",        50, 49, 48, 47),
  WPOS("VOOR",        56, 57, 58, 59),
  WPOS("KWART",       33, 34, 35, 36, 37),
  WPOS("HALF",        16, 35, 42, 61),
  WPOS("UUR",         120, 119, 118),
  WPOS("EEN",         105, 106, 107),
  WPOS("TWEE",        80, 101, 106, 127),
  WPOS("DRIE",        76, 75, 74, 73),
  WPOS("VIER",        41, 62, 67, 88),
  WPOS("VIJF",        126, 125, 124, 123),
  WPOS("ZES",         70, 85, 96),
  WPOS("ZEVEN",       70, 69, 68, 67, 66),
  WPOS("ACHT",        112, 113, 114, 115),
  WPOS("NEGEN",       107, 108, 109, 110, 111),
  WPOS("TIEN",        81, 82, 83, 84),
  WPOS("ELF",         69, 86, 95),
  WPOS("TWAALF",      102, 101, 100, 99, 98, 97),
};

const size_t WORDS_NL_50x50_V3_COUNT = sizeof(WORDS_NL_50x50_V3) / sizeof(WORDS_NL_50x50_V3[0]);
const size_t EXTRA_MINUTES_NL_50x50_V3_COUNT = sizeof(EXTRA_MINUTES_NL_50x50_V3) / sizeof(EXTRA_MINUTES_NL_50x50_V3[0]);
