#include "grid_variants/nl_50x50_v1.h"

// NL_50x50_V1 grid layout
const uint16_t LED_COUNT_GRID_NL_50x50_V1 = 132;
const uint16_t LED_COUNT_EXTRA_NL_50x50_V1 = 0;
const uint16_t LED_COUNT_TOTAL_NL_50x50_V1 = LED_COUNT_GRID_NL_50x50_V1 + LED_COUNT_EXTRA_NL_50x50_V1;

const char* const LETTER_GRID_NL_50x50_V1[] = {
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

const uint16_t EXTRA_MINUTES_NL_50x50_V1[] = { 35, 59, 83, 107 };

const WordPosition WORDS_NL_50x50_V1[] = {
  { "HET",         { 1, 23, 25 } },
  { "IS",          { 49, 71 } },
  { "VIJF_M",      { 3, 21, 27, 45 } },
  { "TIEN_M",      { 22, 26, 46, 50 } },
  { "OVER",        { 4, 20, 28, 44 } },
  { "VOOR",        { 43, 53, 67, 77 } },
  { "KWART",       { 75, 93, 99, 117, 123 } },
  { "HALF",        { 98, 99, 100, 101 } },
  { "UUR",         { 106, 110, 130 } },
  { "EEN",         { 9, 15, 33 } },
  { "TWEE",        { 17, 16, 15, 14 } },
  { "DRIE",        { 6, 18, 30, 42 } },
  { "VIER",        { 116, 115, 114, 113 } },
  { "VIJF",        { 34, 38, 58, 62 } },
  { "ZES",         { 78, 79, 80 } },
  { "ZEVEN",       { 78, 90, 102, 114, 126 } },
  { "ACHT",        { 87, 105, 111, 129 } },
  { "NEGEN",       { 33, 39, 57, 63, 81 } },
  { "TIEN",        { 31, 41, 55, 65 } },
  { "ELF",         { 90, 89, 88 } },
  { "TWAALF",      { 8, 16, 32, 40, 56, 64 } }
};

const size_t WORDS_NL_50x50_V1_COUNT = sizeof(WORDS_NL_50x50_V1) / sizeof(WORDS_NL_50x50_V1[0]);
const size_t EXTRA_MINUTES_NL_50x50_V1_COUNT = sizeof(EXTRA_MINUTES_NL_50x50_V1) / sizeof(EXTRA_MINUTES_NL_50x50_V1[0]);
