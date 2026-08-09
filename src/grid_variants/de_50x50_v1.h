#pragma once

#include <stddef.h>
#include <stdint.h>

#include "phrase_rules.h"
#include "wordposition.h"

extern const uint16_t LED_COUNT_GRID_DE_50x50_V1;
extern const uint16_t LED_COUNT_EXTRA_DE_50x50_V1;
extern const uint16_t LED_COUNT_TOTAL_DE_50x50_V1;

extern const char* const LETTER_GRID_DE_50x50_V1[];
extern const WordPosition WORDS_DE_50x50_V1[];
extern const size_t WORDS_DE_50x50_V1_COUNT;
extern const uint16_t EXTRA_MINUTES_DE_50x50_V1[];
extern const size_t EXTRA_MINUTES_DE_50x50_V1_COUNT;

// Two regional dialects share this one letter grid. Both are offered to the
// customer at setup; DIALECTS_DE_50x50_V1[0] is what a device falls back to
// when no choice is stored.
extern const PhraseRules DE_RULES_STANDARD;  // Nord / "Hochdeutsch"
extern const PhraseRules DE_RULES_SUED;      // Süd-Ost: viertel/dreiviertel

extern const ClockDialect DIALECTS_DE_50x50_V1[];
extern const size_t DIALECTS_DE_50x50_V1_COUNT;
