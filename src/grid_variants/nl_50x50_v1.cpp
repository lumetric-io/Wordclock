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
  WPOS("HET",         1, 23, 25),
  WPOS("IS",          49, 71),
  WPOS("VIJF_M",      3, 21, 27, 45),
  WPOS("TIEN_M",      22, 26, 46, 50),
  WPOS("OVER",        4, 20, 28, 44),
  WPOS("VOOR",        43, 53, 67, 77),
  WPOS("KWART",       75, 93, 99, 117, 123),
  WPOS("HALF",        98, 99, 100, 101),
  WPOS("UUR",         106, 110, 130),
  WPOS("EEN",         9, 15, 33),
  WPOS("TWEE",        17, 16, 15, 14),
  WPOS("DRIE",        6, 18, 30, 42),
  WPOS("VIER",        116, 115, 114, 113),
  WPOS("VIJF",        34, 38, 58, 62),
  WPOS("ZES",         78, 79, 80),
  WPOS("ZEVEN",       78, 90, 102, 114, 126),
  WPOS("ACHT",        87, 105, 111, 129),
  WPOS("NEGEN",       33, 39, 57, 63, 81),
  WPOS("TIEN",        31, 41, 55, 65),
  WPOS("ELF",         90, 89, 88),
  WPOS("TWAALF",      8, 16, 32, 40, 56, 64),
};

const size_t WORDS_NL_50x50_V1_COUNT = sizeof(WORDS_NL_50x50_V1) / sizeof(WORDS_NL_50x50_V1[0]);
const size_t EXTRA_MINUTES_NL_50x50_V1_COUNT = sizeof(EXTRA_MINUTES_NL_50x50_V1) / sizeof(EXTRA_MINUTES_NL_50x50_V1[0]);
