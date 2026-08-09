#pragma once

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Language-neutral phrase rules
// ---------------------------------------------------------------------------
// Grid variants no longer carry language-specific word names ("HET", "OVER",
// "KWART"); they use the slot keys below. A PhraseRules table then says, per
// five-minute step, which slots light up and whether the hour word rolls over
// to the next hour. time_mapper.cpp walks that table instead of switching on
// hardcoded Dutch words.
//
// Slot keys a variant may define:
//
//   PREFIX_A       "HET" / "ES"        — split from PREFIX_B so the two can
//   PREFIX_B       "IS"  / "IST"         animate separately (see hetIsDuration)
//   MIN_5          "VIJF" / "FÜNF"     — minute words, distinct LEDs from the
//   MIN_10         "TIEN" / "ZEHN"       hour words of the same name
//   MIN_20         "ZWANZIG"           — German only
//   QUARTER        "KWART" / "VIERTEL"
//   THREEQUARTER   "DREIVIERTEL"       — German (southern) only
//   HALF           "HALF" / "HALB"
//   PAST           "OVER" / "NACH"
//   TO             "VOOR" / "VOR"
//   OCLOCK         "UUR" / "UHR"
//   H_1 .. H_12    hour words
//   H_<n>_ALT      optional alternate hour word used only on a step that also
//                  lights OCLOCK. German needs it: "es ist EIN Uhr" but "fünf
//                  nach EINS". Variants without _ALT keys are unaffected.
//
// A step emits, in this order:
//     PREFIX_A, PREFIX_B, slots…, hour word, [OCLOCK]
// which matches how both languages phrase every step.

struct PhraseStep {
  const char* slots[3];  // unused entries are nullptr
  uint8_t hourOffset;    // 0 = current hour, 1 = next hour
  bool withOClock;
};

struct PhraseRules {
  const char* id;        // "nl", "de" — logging and tests only
  PhraseStep steps[12];  // index = minute / 5
};

// ---------------------------------------------------------------------------
// Dialects
// ---------------------------------------------------------------------------
// One letter grid can be read in more than one way. German is the reason: the
// same plate spells both "viertel nach zehn" (Nord) and "viertel elf" (Süd),
// and which one is right depends on where the customer lives, not on the
// hardware. A dialect is therefore a customer choice, and both tables stay
// live data.
//
// Every variant has at least one dialect, so the engine never needs a special
// case for "no dialect": Dutch simply has exactly one.
//
// `id` is persisted in NVS and must stay stable across firmware versions.
// `sample` is the sentence shown next to the choice in the setup UI — it has
// to differ visibly between a variant's dialects, otherwise there is nothing
// for the customer to pick on.
struct ClockDialect {
  const char* id;      // NVS-stable: "nl", "de-nord", "de-sued"
  const char* label;   // UI label
  const char* sample;  // example phrase that distinguishes this dialect
  const PhraseRules* rules;
};

// Shared by every Dutch variant. German tables live in their own variant file,
// so a Dutch-only build does not link them.
extern const PhraseRules PHRASE_RULES_NL;

// Dutch has one reading of its plate. Registered as a one-entry dialect list
// so grid_layout can treat every variant identically.
extern const ClockDialect DIALECTS_NL[];
extern const size_t DIALECTS_NL_COUNT;

// hour12: 0 = twelve o'clock, 1 = one, … 11 = eleven.
const char* phraseHourKey(int hour12);
const char* phraseHourAltKey(int hour12);
