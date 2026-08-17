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
  WPOS("PREFIX_A", 1, 2, 3),                   // HET
  WPOS("PREFIX_B", 5, 6),                      // IS
  WPOS("MIN_5",    27, 28, 29, 30),            // VIJF_M
  WPOS("MIN_10",   23, 22, 21, 20),            // TIEN_M
  WPOS("PAST",     50, 49, 48, 47),            // OVER
  WPOS("TO",       56, 57, 58, 59),            // VOOR
  WPOS("QUARTER",  33, 34, 35, 36, 37),        // KWART
  WPOS("HALF",     16, 35, 42, 61),            // HALF
  WPOS("OCLOCK",   120, 119, 118),             // UUR
  WPOS("H_1",      105, 106, 107),             // EEN
  WPOS("H_2",      80, 101, 106, 127),         // TWEE
  WPOS("H_3",      76, 75, 74, 73),            // DRIE
  WPOS("H_4",      41, 62, 67, 88),            // VIER
  WPOS("H_5",      126, 125, 124, 123),        // VIJF
  WPOS("H_6",      70, 85, 96),                // ZES
  WPOS("H_7",      70, 69, 68, 67, 66),        // ZEVEN
  WPOS("H_8",      112, 113, 114, 115),        // ACHT
  WPOS("H_9",      107, 108, 109, 110, 111),   // NEGEN
  WPOS("H_10",     81, 82, 83, 84),            // TIEN
  WPOS("H_11",     69, 86, 95),                // ELF
  WPOS("H_12",     102, 101, 100, 99, 98, 97), // TWAALF
};

const size_t WORDS_NL_50x50_V3_COUNT = sizeof(WORDS_NL_50x50_V3) / sizeof(WORDS_NL_50x50_V3[0]);
const size_t EXTRA_MINUTES_NL_50x50_V3_COUNT = sizeof(EXTRA_MINUTES_NL_50x50_V3) / sizeof(EXTRA_MINUTES_NL_50x50_V3[0]);
