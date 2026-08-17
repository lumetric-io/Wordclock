#include "grid_variants/nl_55x50_logo_v1.h"

// Mirrors the NL_50x50_V3 layout for the logo hardware variant.
const uint16_t LED_COUNT_GRID_NL_55x50_LOGO_V1 = 128;
const uint16_t LED_COUNT_EXTRA_NL_55x50_LOGO_V1 = 14;
const uint16_t LED_COUNT_TOTAL_NL_55x50_LOGO_V1 =
  LED_COUNT_GRID_NL_55x50_LOGO_V1 + LED_COUNT_EXTRA_NL_55x50_LOGO_V1;

const char* const LETTER_GRID_NL_55x50_LOGO_V1[] = {
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

const uint16_t EXTRA_MINUTES_NL_55x50_LOGO_V1[] = {
  static_cast<uint16_t>(LED_COUNT_GRID_NL_55x50_LOGO_V1 + 5),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_55x50_LOGO_V1 + 7),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_55x50_LOGO_V1 + 9),
  static_cast<uint16_t>(LED_COUNT_GRID_NL_55x50_LOGO_V1 + 11)
};

const WordPosition WORDS_NL_55x50_LOGO_V1[] = {
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

const size_t WORDS_NL_55x50_LOGO_V1_COUNT =
  sizeof(WORDS_NL_55x50_LOGO_V1) / sizeof(WORDS_NL_55x50_LOGO_V1[0]);
const size_t EXTRA_MINUTES_NL_55x50_LOGO_V1_COUNT =
  sizeof(EXTRA_MINUTES_NL_55x50_LOGO_V1) / sizeof(EXTRA_MINUTES_NL_55x50_LOGO_V1[0]);
