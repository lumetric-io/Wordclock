#include "phrase_rules.h"

namespace {

const char* const HOUR_KEYS[12] = {
  "H_12", "H_1", "H_2", "H_3", "H_4",  "H_5",
  "H_6",  "H_7", "H_8", "H_9", "H_10", "H_11"
};

const char* const HOUR_ALT_KEYS[12] = {
  "H_12_ALT", "H_1_ALT", "H_2_ALT", "H_3_ALT", "H_4_ALT",  "H_5_ALT",
  "H_6_ALT",  "H_7_ALT", "H_8_ALT", "H_9_ALT", "H_10_ALT", "H_11_ALT"
};

} // namespace

const char* phraseHourKey(int hour12) {
  if (hour12 < 0 || hour12 > 11) return HOUR_KEYS[0];
  return HOUR_KEYS[hour12];
}

const char* phraseHourAltKey(int hour12) {
  if (hour12 < 0 || hour12 > 11) return HOUR_ALT_KEYS[0];
  return HOUR_ALT_KEYS[hour12];
}

// Dutch. Transcribed 1:1 from the switch that used to live in time_mapper.cpp;
// a golden test over all 1440 minutes guards the equivalence.
const PhraseRules PHRASE_RULES_NL = {
  "nl",
  {
    /* :00 */ { { nullptr,  nullptr, nullptr }, 0, true  },  // twaalf uur
    /* :05 */ { { "MIN_5",  "PAST",  nullptr }, 0, false },  // vijf over
    /* :10 */ { { "MIN_10", "PAST",  nullptr }, 0, false },  // tien over
    /* :15 */ { { "QUARTER","PAST",  nullptr }, 0, false },  // kwart over
    /* :20 */ { { "MIN_10", "TO",    "HALF"  }, 1, false },  // tien voor half
    /* :25 */ { { "MIN_5",  "TO",    "HALF"  }, 1, false },  // vijf voor half
    /* :30 */ { { "HALF",   nullptr, nullptr }, 1, false },  // half
    /* :35 */ { { "MIN_5",  "PAST",  "HALF"  }, 1, false },  // vijf over half
    /* :40 */ { { "MIN_10", "PAST",  "HALF"  }, 1, false },  // tien over half
    /* :45 */ { { "QUARTER","TO",    nullptr }, 1, false },  // kwart voor
    /* :50 */ { { "MIN_10", "TO",    nullptr }, 1, false },  // tien voor
    /* :55 */ { { "MIN_5",  "TO",    nullptr }, 1, false },  // vijf voor
  }
};

// Dutch has one reading. The sample is what the setup UI shows next to the
// choice; with a single dialect it is informational rather than a decision.
const ClockDialect DIALECTS_NL[] = {
  { "nl", "Nederlands", "kwart over tien", &PHRASE_RULES_NL },
};
const size_t DIALECTS_NL_COUNT = sizeof(DIALECTS_NL) / sizeof(DIALECTS_NL[0]);
