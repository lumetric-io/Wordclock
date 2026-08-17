#include "grid_variants/nl_105x105_logo_v1.h"

// Mirrors the NL_55x50_LOGO_V1 layout for the 105x105 logo hardware variant.
const uint16_t LED_COUNT_GRID_NL_105x105_LOGO_V1 = 488;
const uint16_t LED_COUNT_EXTRA_NL_105x105_LOGO_V1 = 49;
const uint16_t LED_COUNT_TOTAL_NL_105x105_LOGO_V1 =
  LED_COUNT_GRID_NL_105x105_LOGO_V1 + LED_COUNT_EXTRA_NL_105x105_LOGO_V1;

const char* const LETTER_GRID_NL_105x105_LOGO_V1[] = {
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

const uint16_t EXTRA_MINUTES_NL_105x105_LOGO_V1[] = {
  // +1 minute symbol (4 LEDs)
  495, 496, 531, 532,
  // +2 minute symbol (4 LEDs)
  499, 500, 527, 528,
  // +3 minute symbol (4 LEDs)
  503, 504, 523, 524,
  // +4 minute symbol (4 LEDs)
  507, 508, 519, 520
};

const WordPosition WORDS_NL_105x105_LOGO_V1[] = {
  WPOS("PREFIX_A", 1, 2, 3, 4, 5, 6, 41, 42, 43, 44, 45, 46),                                                                               // HET
  WPOS("PREFIX_B", 9, 10, 11, 12, 35, 36, 37, 38),                                                                                          // IS
  WPOS("MIN_5",    99, 100, 101, 102, 103, 104, 105, 106, 137, 138, 139, 140, 141, 142, 143, 144),                                          // VIJF_M
  WPOS("MIN_10",   52, 53, 54, 55, 56, 57, 58, 59, 86, 87, 88, 89, 90, 91, 92, 93),                                                         // TIEN_M
  WPOS("PAST",     148, 149, 150, 151, 152, 153, 154, 155, 186, 187, 188, 189, 190, 191, 192, 193),                                         // OVER
  WPOS("TO",       203, 204, 205, 206, 207, 208, 209, 210, 229, 230, 231, 232, 233, 234, 235, 236),                                         // VOOR
  WPOS("QUARTER",  111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132),                     // KWART
  WPOS("HALF",     66, 67, 78, 79, 115, 116, 127, 128, 164, 165, 176, 177, 213, 214, 225, 226),                                             // HALF
  WPOS("OCLOCK",   458, 459, 460, 461, 462, 463, 466, 467, 468, 469, 470, 471),                                                             // UUR
  WPOS("H_1",      393, 394, 395, 396, 397, 398, 433, 434, 435, 436, 437, 438),                                                             // EEN
  WPOS("H_2",      297, 298, 337, 338, 346, 347, 386, 387, 395, 396, 435, 436, 444, 445, 484, 485),                                         // TWEE
  WPOS("H_3",      246, 247, 248, 249, 250, 251, 252, 253, 284, 285, 286, 287, 288, 289, 290, 291),                                         // DRIE
  WPOS("H_4",      166, 167, 174, 175, 215, 216, 223, 224, 264, 265, 272, 273, 313, 314, 321, 322),                                         // VIER
  WPOS("H_5",      446, 447, 448, 449, 450, 451, 452, 453, 476, 477, 478, 479, 480, 481, 482, 483),                                         // VIJF
  WPOS("H_6",      258, 259, 278, 279, 307, 308, 327, 328, 356, 357, 376, 377),                                                             // ZES
  WPOS("H_7",      258, 259, 260, 261, 262, 263, 264, 265, 266, 267, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279),                     // ZEVEN
  WPOS("H_8",      407, 408, 409, 410, 411, 412, 413, 414, 417, 418, 419, 420, 421, 422, 423, 424),                                         // ACHT
  WPOS("H_9",      397, 398, 399, 400, 401, 402, 403, 404, 405, 406, 425, 426, 427, 428, 429, 430, 431, 432, 433, 434),                     // NEGEN
  WPOS("H_10",     299, 300, 301, 302, 303, 304, 305, 306, 329, 330, 331, 332, 333, 334, 335, 336),                                         // TIEN
  WPOS("H_11",     260, 261, 276, 277, 309, 310, 325, 326, 358, 359, 374, 375),                                                             // ELF
  WPOS("H_12",     344, 345, 346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 378, 379, 380, 381, 382, 383, 384, 385, 386, 387, 388, 389), // TWAALF
};

const size_t WORDS_NL_105x105_LOGO_V1_COUNT =
  sizeof(WORDS_NL_105x105_LOGO_V1) / sizeof(WORDS_NL_105x105_LOGO_V1[0]);
const size_t EXTRA_MINUTES_NL_105x105_LOGO_V1_COUNT =
  sizeof(EXTRA_MINUTES_NL_105x105_LOGO_V1) / sizeof(EXTRA_MINUTES_NL_105x105_LOGO_V1[0]);
