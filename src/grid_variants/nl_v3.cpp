#include "grid_variants/nl_v3.h"

// v3 is new lay-out in spiegelbeeld t.o.v. v1, elke bocht met 4 leds, behalve 1 (misproductie ;-))

// Placeholder: NL_V3 currently reuses the NL_V1 grid until a dedicated layout is supplied.
const uint16_t LED_COUNT_GRID_NL_V3 = 144;
const uint16_t LED_COUNT_EXTRA_NL_V3 = 15;
const uint16_t LED_COUNT_TOTAL_NL_V3 = LED_COUNT_GRID_NL_V3 + LED_COUNT_EXTRA_NL_V3;

const char* const LETTER_GRID_NL_V3[] = {
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

const uint16_t EXTRA_MINUTES_NL_V3[] = {
  static_cast<uint16_t>(LED_COUNT_GRID_NL_V3 + 13),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_V3 + 11),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_V3 + 9),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_V3 + 7)
};

const WordPosition WORDS_NL_V3[] = {
  WPOS("HET",         10, 9, 8),
  WPOS("IS",          6, 5),
  WPOS("VIJF_M",      40, 39, 38, 37),
  WPOS("TIEN_M",      16, 17, 18, 19),
  WPOS("OVER",        45, 46, 47, 48),
  WPOS("VOOR",        66, 65, 64, 63),
  WPOS("KWART",       34, 33, 32, 31, 30),
  WPOS("HALF",        23, 32, 53, 61),
  WPOS("UUR",         142, 143, 144),
  WPOS("EEN",         129, 128, 127),
  WPOS("TWEE",        98, 105, 128, 135),
  WPOS("DRIE",        74, 75, 76, 77),
  WPOS("VIER",        54, 60, 83, 90),
  WPOS("VIJF",        136, 137, 138, 139),
  WPOS("ZES",         80, 93, 110),
  WPOS("ZEVEN",       80, 81, 82, 83, 84),
  WPOS("ACHT",        122, 121, 120, 119),
  WPOS("NEGEN",       127, 126, 125, 124, 123),
  WPOS("TIEN",        97, 96, 95, 94),
  WPOS("ELF",         81, 92, 111),
  WPOS("TWAALF",      104, 105, 106, 107, 108, 109),
};

const size_t WORDS_NL_V3_COUNT = sizeof(WORDS_NL_V3) / sizeof(WORDS_NL_V3[0]);
const size_t EXTRA_MINUTES_NL_V3_COUNT = sizeof(EXTRA_MINUTES_NL_V3) / sizeof(EXTRA_MINUTES_NL_V3[0]);
