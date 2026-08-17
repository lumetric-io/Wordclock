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

// Four readings share this one letter grid: two independent axes (quarters,
// twenties) with two options each. All four are offered to the customer;
// DIALECTS_DE_50x50_V1[0] is what a device falls back to when no choice is
// stored.
extern const PhraseRules DE_RULES_STANDARD;      // nach + zwanzig  ("Hochdeutsch")
extern const PhraseRules DE_RULES_SUED;          // viertel + halb  ("Süd-Ost")
extern const PhraseRules DE_RULES_NORD_HALB;     // nach + halb
extern const PhraseRules DE_RULES_SUED_ZWANZIG;  // viertel + zwanzig

extern const ClockDialect DIALECTS_DE_50x50_V1[];
extern const size_t DIALECTS_DE_50x50_V1_COUNT;

extern const DialectAxis DIALECT_AXES_DE_50x50_V1[];
extern const size_t DIALECT_AXES_DE_50x50_V1_COUNT;
