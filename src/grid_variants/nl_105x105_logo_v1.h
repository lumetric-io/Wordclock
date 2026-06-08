#pragma once

#include <stddef.h>
#include <stdint.h>

#include "wordposition.h"

extern const uint16_t LED_COUNT_GRID_NL_105x105_LOGO_V1;
extern const uint16_t LED_COUNT_EXTRA_NL_105x105_LOGO_V1;
extern const uint16_t LED_COUNT_TOTAL_NL_105x105_LOGO_V1;

extern const char* const LETTER_GRID_NL_105x105_LOGO_V1[];
extern const WordPosition WORDS_NL_105x105_LOGO_V1[];
extern const size_t WORDS_NL_105x105_LOGO_V1_COUNT;
extern const uint16_t EXTRA_MINUTES_NL_105x105_LOGO_V1[];
extern const size_t EXTRA_MINUTES_NL_105x105_LOGO_V1_COUNT;

namespace nl_105x105_logo_v1 {
constexpr uint16_t LOGO_LED_COUNT = 52;
}  // namespace nl_105x105_logo_v1
