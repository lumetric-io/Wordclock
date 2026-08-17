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
  WPOS("PREFIX_A", 1, 2, 3),                      // HET
  WPOS("PREFIX_B", 5, 6),                         // IS
  WPOS("MIN_5",    29, 30, 31, 32),               // VIJF_M
  WPOS("MIN_10",   24, 23, 22, 21),               // TIEN_M
  WPOS("PAST",     53, 52, 51, 50),               // OVER
  WPOS("TO",       60, 61, 62, 63),               // VOOR
  WPOS("QUARTER",  35, 36, 37, 38, 39),           // KWART
  WPOS("HALF",     17, 37, 45, 65),               // HALF
  WPOS("OCLOCK",   129, 128, 127),                // UUR
  WPOS("H_1",      113, 114, 115),                // EEN
  WPOS("H_2",      86, 108, 114, 136),            // TWEE
  WPOS("H_3",      81, 80, 79, 78),               // DRIE
  WPOS("H_4",      44, 66, 72, 94),               // VIER
  WPOS("H_5",      135, 134, 133, 132),           // VIJF
  WPOS("H_6",      75, 91, 103),                  // ZES
  WPOS("H_7",      75, 74, 73, 72, 71),           // ZEVEN
  WPOS("H_8",      120, 121, 122, 123),           // ACHT
  WPOS("H_9",      115, 116, 117, 118, 119),      // NEGEN
  WPOS("H_10",     87, 88, 89, 90),               // TIEN
  WPOS("H_11",     74, 92, 102),                  // ELF
  WPOS("H_12",     109, 108, 107, 106, 105, 104), // TWAALF
};

const size_t WORDS_NL_V4_COUNT = sizeof(WORDS_NL_V4) / sizeof(WORDS_NL_V4[0]);
const size_t EXTRA_MINUTES_NL_V4_COUNT = sizeof(EXTRA_MINUTES_NL_V4) / sizeof(EXTRA_MINUTES_NL_V4[0]);
