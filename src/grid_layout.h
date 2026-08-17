#pragma once

#include <Arduino.h>
#include "phrase_rules.h"
#include "wordposition.h"

// Dimensions of the letter grid
const int GRID_WIDTH = 11;
const int GRID_HEIGHT = 11;

// Numeric values are NVS-stable. Devices in the field have grid_id stored under
// these IDs; reusing or shifting them would silently misinterpret the stored value.
// Add new variants with new unused IDs.
enum class GridVariant : uint8_t {
  NL_V4 = 3,
  NL_50x50_V3 = 6,
  NL_55x50_LOGO_V1 = 7,
  NL_20x20_V1 = 8,
  NL_105x105_LOGO_V1 = 10,
  DE_50x50_V1 = 11,
};

struct GridVariantInfo {
  GridVariant variant;
  const char* key;       // identifier like "NL_V1"
  const char* label;     // human-readable name for UI
  const char* language;  // ISO language code, e.g. "nl"
  const char* version;   // version string, e.g. "v1"
};

// Active layout data
extern const char* const* LETTER_GRID;
extern const WordPosition* ACTIVE_WORDS;
extern size_t ACTIVE_WORD_COUNT;
extern const uint16_t* EXTRA_MINUTE_LEDS;
extern size_t EXTRA_MINUTE_LED_COUNT;
extern size_t EXTRA_MINUTE_LED_GROUP_SIZE;

// Variant management helpers
GridVariant getActiveGridVariant();
bool setActiveGridVariant(GridVariant variant);
bool setActiveGridVariantById(uint8_t id);
bool setActiveGridVariantByKey(const char* key);
GridVariant gridVariantFromId(uint8_t id);
GridVariant gridVariantFromKey(const char* key);
uint8_t gridVariantToId(GridVariant variant);
const GridVariantInfo* getGridVariantInfos(size_t& count);
const GridVariantInfo* getGridVariantInfo(GridVariant variant);

// Lightweight lookup instead of unordered_map
const WordPosition* find_word(const char* name);

// Phrase rules of the active variant *and* the active dialect. Never null for
// a correctly registered variant.
const PhraseRules* getActivePhraseRules();

// ---------------------------------------------------------------------------
// Language and dialect
// ---------------------------------------------------------------------------
// A build contains one grid variant per language it supports (the front plate
// is physical, so a language is a plate). Selecting a language therefore means
// selecting a variant; the variants within one product are required to share
// their LED counts so the strip layout does not change underneath the driver.
//
// These functions only change what the firmware *renders*. Persisting the
// choice is language_settings' job — grid_layout stays free of NVS.

// Number of distinct languages compiled into this build (>= 1).
size_t getLanguageCount();

// ISO code of language `index`, or nullptr when out of range.
const char* getLanguageCode(size_t index);

// ISO code of the language currently rendered. Never null.
const char* getActiveLanguage();

// True if `code` is compiled into this build.
bool hasLanguage(const char* code);

// Switch to the variant for `code`. Resets the dialect to that variant's
// first. False (and no change) if the language is not in this build.
bool setActiveLanguage(const char* code);

// Dialects of the active variant (>= 1).
size_t getDialectCount();
const ClockDialect* getDialect(size_t index);
const ClockDialect* getActiveDialect();

// Switch dialect within the active variant. False if `id` is not one of them.
// Only the phrase table changes, so this needs no reboot.
bool setActiveDialect(const char* id);

// Dialect axes of the active variant. Zero for a variant that offers its
// dialects as one flat list (Dutch); German declares two.
size_t getDialectAxisCount();
const DialectAxis* getDialectAxis(size_t index);

// The active dialect's value on `axisId`, or nullptr if the variant has no
// such axis.
const char* getActiveDialectAxisValue(const char* axisId);

// Resolve one axis change into a dialect: takes the active dialect's tuple,
// replaces `axisId` with `value`, and returns the dialect matching the result.
// nullptr if the axis or value is unknown, or — which would be a data bug the
// tests are meant to catch — no dialect covers the combination.
const ClockDialect* findDialectByAxisChange(const char* axisId, const char* value);

// Active LED counts per variant
uint16_t getActiveLedCountGrid();
uint16_t getActiveLedCountExtra();
uint16_t getActiveLedCountTotal();

/** True if the given LED index is used by any word in the active grid (wordclock-mini: skip such LEDs for event blinking). */
bool isLedUsedByActiveWords(uint16_t ledIndex);
