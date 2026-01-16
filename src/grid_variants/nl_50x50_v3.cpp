#include "grid_variants/nl_50x50_v3.h"

// NL_50x50 grid layout (v3); adjust when hardware wiring deviates.
const uint16_t LED_COUNT_GRID_NL_50x50_V3 = 128;
const uint16_t LED_COUNT_EXTRA_NL_50x50_V3 = 14;
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
  { "HET",         { 1, 2, 3 } },
  { "IS",          { 5, 6 } },
  { "VIJF_M",      { 27, 28, 29, 30 } },
  { "TIEN_M",      { 23, 22, 21, 20 } },
  { "OVER",        { 50, 49, 48, 47 } },
  { "VOOR",        { 56, 57, 58, 59 } },
  { "KWART",       { 33, 34, 35, 36, 37 } },
  { "HALF",        { 16, 35, 42, 61 } },
  { "UUR",         { 120, 119, 118 } },
  { "EEN",         { 105, 106, 107 } },
  { "TWEE",        { 80, 101, 106, 127 } },
  { "DRIE",        { 76, 75, 74, 73 } },
  { "VIER",        { 41, 62, 67, 88 } },
  { "VIJF",        { 126, 125, 124, 123 } },
  { "ZES",         { 70, 85, 96 } },
  { "ZEVEN",       { 70, 69, 68, 67, 66 } },
  { "ACHT",        { 112, 113, 114, 115 } },
  { "NEGEN",       { 107, 108, 109, 110, 111 } },
  { "TIEN",        { 81, 82, 83, 84 } },
  { "ELF",         { 69, 86, 95 } },
  { "TWAALF",      { 102, 101, 100, 99, 98, 97 } }
};

const size_t WORDS_NL_50x50_V3_COUNT = sizeof(WORDS_NL_50x50_V3) / sizeof(WORDS_NL_50x50_V3[0]);
const size_t EXTRA_MINUTES_NL_50x50_V3_COUNT = sizeof(EXTRA_MINUTES_NL_50x50_V3) / sizeof(EXTRA_MINUTES_NL_50x50_V3[0]);
