#include "grid_variants/nl_v4.h"

// Placeholder: NL_V4 currently reuses the NL_V1 grid until a dedicated layout is supplied.
const uint16_t LED_COUNT_GRID_NL_V4 = 137;
const uint16_t LED_COUNT_EXTRA_NL_V4 = 14;
const uint16_t LED_COUNT_TOTAL_NL_V4 = LED_COUNT_GRID_NL_V4 + LED_COUNT_EXTRA_NL_V4;

const char* const LETTER_GRID_NL_V4[] = {
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

const uint16_t EXTRA_MINUTES_NL_V4[] = {
  static_cast<uint16_t>(LED_COUNT_GRID_NL_V4 + 6),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_V4 + 8),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_V4 + 10),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_V4 + 12)
};

const WordPosition WORDS_NL_V4[] = {
  WPOS("HET",         1, 2, 3),
  WPOS("IS",          5, 6),
  WPOS("VIJF_M",      29, 30, 31, 32),
  WPOS("TIEN_M",      24, 23, 22, 21),
  WPOS("OVER",        53, 52, 51, 50),
  WPOS("VOOR",        60, 61, 62, 63),
  WPOS("KWART",       35, 36, 37, 38, 39),
  WPOS("HALF",        17, 37, 45, 65),
  WPOS("UUR",         129, 128, 127),
  WPOS("EEN",         113, 114, 115),
  WPOS("TWEE",        86, 108, 114, 136),
  WPOS("DRIE",        81, 80, 79, 78),
  WPOS("VIER",        44, 66, 72, 94),
  WPOS("VIJF",        135, 134, 133, 132),
  WPOS("ZES",         75, 91, 103),
  WPOS("ZEVEN",       75, 74, 73, 72, 71),
  WPOS("ACHT",        120, 121, 122, 123),
  WPOS("NEGEN",       115, 116, 117, 118, 119),
  WPOS("TIEN",        87, 88, 89, 90),
  WPOS("ELF",         74, 92, 102),
  WPOS("TWAALF",      109, 108, 107, 106, 105, 104),
};

const size_t WORDS_NL_V4_COUNT = sizeof(WORDS_NL_V4) / sizeof(WORDS_NL_V4[0]);
const size_t EXTRA_MINUTES_NL_V4_COUNT = sizeof(EXTRA_MINUTES_NL_V4) / sizeof(EXTRA_MINUTES_NL_V4[0]);
