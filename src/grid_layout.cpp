#include "grid_layout.h"

#include <Arduino.h>
#include <string.h>

#ifdef PRODUCT_CONFIG_HEADER
#include PRODUCT_CONFIG_HEADER
#elif defined(__has_include)
#if __has_include("product_config.h")
#include "product_config.h"
#endif
#endif

#if !defined(ENABLE_GRID_NL_V4) && !defined(ENABLE_GRID_NL_50X50_V3) && \
    !defined(ENABLE_GRID_NL_55X50_LOGO_V1) && !defined(ENABLE_GRID_NL_20X20_V1) && \
    !defined(ENABLE_GRID_NL_105X105_LOGO_V1) && !defined(ENABLE_GRID_DE_50X50_V1)
#define ENABLE_GRID_NL_V4 1
#define ENABLE_GRID_NL_50X50_V3 1
#define ENABLE_GRID_NL_55X50_LOGO_V1 1
#define ENABLE_GRID_NL_20X20_V1 1
#define ENABLE_GRID_NL_105X105_LOGO_V1 1
#define ENABLE_GRID_DE_50X50_V1 1
#endif

#ifndef ENABLE_GRID_NL_V4
#define ENABLE_GRID_NL_V4 0
#endif
#ifndef ENABLE_GRID_NL_50X50_V3
#define ENABLE_GRID_NL_50X50_V3 0
#endif
#ifndef ENABLE_GRID_NL_55X50_LOGO_V1
#define ENABLE_GRID_NL_55X50_LOGO_V1 0
#endif
#ifndef ENABLE_GRID_NL_20X20_V1
#define ENABLE_GRID_NL_20X20_V1 0
#endif
#ifndef ENABLE_GRID_NL_105X105_LOGO_V1
#define ENABLE_GRID_NL_105X105_LOGO_V1 0
#endif
#ifndef ENABLE_GRID_DE_50X50_V1
#define ENABLE_GRID_DE_50X50_V1 0
#endif

#if !(ENABLE_GRID_NL_V4 || ENABLE_GRID_NL_50X50_V3 || ENABLE_GRID_NL_55X50_LOGO_V1 || \
      ENABLE_GRID_NL_20X20_V1 || ENABLE_GRID_NL_105X105_LOGO_V1 || ENABLE_GRID_DE_50X50_V1)
#error "At least one grid variant must be enabled."
#endif

#if ENABLE_GRID_NL_V4
#include "grid_variants/nl_v4.h"
#endif
#if ENABLE_GRID_NL_50X50_V3
#include "grid_variants/nl_50x50_v3.h"
#endif
#if ENABLE_GRID_NL_55X50_LOGO_V1
#include "grid_variants/nl_55x50_logo_v1.h"
#endif
#if ENABLE_GRID_NL_20X20_V1
#include "grid_variants/nl_20x20_v1.h"
#endif
#if ENABLE_GRID_NL_105X105_LOGO_V1
#include "grid_variants/nl_105x105_logo_v1.h"
#endif
#if ENABLE_GRID_DE_50X50_V1
#include "grid_variants/de_50x50_v1.h"
#endif

namespace {

enum class MinuteLayout {
  AfterGrid,
  MixedIntoGrid
};

struct GridVariantData {
  GridVariant variant;
  const char* key;
  const char* label;
  const char* language;
  const char* version;
  uint16_t ledCountGrid;
  uint16_t ledCountExtra;
  uint16_t ledCountTotal;
  const char* const* letterGrid;
  const WordPosition* words;
  size_t wordCount;
  const uint16_t* minuteLeds;
  size_t minuteCount;
  MinuteLayout minuteLayout;
  size_t minuteGroupSize;
  const ClockDialect* dialects;  // never empty; [0] is the fallback
  size_t dialectCount;
  // Optional. A variant with no axes offers its dialects as one flat list;
  // one with axes offers a group per axis and resolves the tuple to a dialect.
  const DialectAxis* axes;
  size_t axisCount;
};

// Helper to compute array length at compile time
template <typename T, size_t N>
constexpr size_t countof(const T (&)[N]) { return N; }

uint16_t computeTotalLedCount(const GridVariantData* data) {
  if (!data) return 0;
  if (data->minuteLayout == MinuteLayout::AfterGrid) {
    return data->ledCountTotal;
  }
  uint16_t maxLed = data->ledCountGrid;
  for (size_t i = 0; i < data->minuteCount; ++i) {
    if (data->minuteLeds[i] > maxLed) {
      maxLed = data->minuteLeds[i];
    }
  }
  return maxLed;
}

uint16_t computeExtraLedCount(const GridVariantData* data) {
  if (!data) return 0;
  if (data->minuteLayout == MinuteLayout::AfterGrid) {
    return data->ledCountExtra;
  }
  uint16_t total = computeTotalLedCount(data);
  return total > data->ledCountGrid ? static_cast<uint16_t>(total - data->ledCountGrid) : 0;
}

static const GridVariantData GRID_VARIANTS[] = {
#if ENABLE_GRID_NL_V4
  { GridVariant::NL_V4, "NL_V4", "Nederlands 30x30 V4", "nl", "v4", LED_COUNT_GRID_NL_V4, LED_COUNT_EXTRA_NL_V4, LED_COUNT_TOTAL_NL_V4, LETTER_GRID_NL_V4, WORDS_NL_V4, WORDS_NL_V4_COUNT, EXTRA_MINUTES_NL_V4, EXTRA_MINUTES_NL_V4_COUNT, MinuteLayout::AfterGrid, 1, DIALECTS_NL, DIALECTS_NL_COUNT },
#endif
#if ENABLE_GRID_NL_50X50_V3
  { GridVariant::NL_50x50_V3, "NL_50x50_V3", "Nederlands 50x50 V3", "nl", "v3", LED_COUNT_GRID_NL_50x50_V3, LED_COUNT_EXTRA_NL_50x50_V3, LED_COUNT_TOTAL_NL_50x50_V3, LETTER_GRID_NL_50x50_V3, WORDS_NL_50x50_V3, WORDS_NL_50x50_V3_COUNT, EXTRA_MINUTES_NL_50x50_V3, EXTRA_MINUTES_NL_50x50_V3_COUNT, MinuteLayout::AfterGrid, 1, DIALECTS_NL, DIALECTS_NL_COUNT },
#endif
#if ENABLE_GRID_NL_55X50_LOGO_V1
  { GridVariant::NL_55x50_LOGO_V1, "NL_55x50_LOGO_V1", "Nederlands 55x50 Logo V1", "nl", "v1", LED_COUNT_GRID_NL_55x50_LOGO_V1, LED_COUNT_EXTRA_NL_55x50_LOGO_V1, LED_COUNT_TOTAL_NL_55x50_LOGO_V1, LETTER_GRID_NL_55x50_LOGO_V1, WORDS_NL_55x50_LOGO_V1, WORDS_NL_55x50_LOGO_V1_COUNT, EXTRA_MINUTES_NL_55x50_LOGO_V1, EXTRA_MINUTES_NL_55x50_LOGO_V1_COUNT, MinuteLayout::AfterGrid, 1, DIALECTS_NL, DIALECTS_NL_COUNT },
#endif
#if ENABLE_GRID_NL_20X20_V1
  { GridVariant::NL_20x20_V1, "NL_20x20_V1", "Nederlands 20x20 V1", "nl", "v1", LED_COUNT_GRID_NL_20x20_V1, LED_COUNT_EXTRA_NL_20x20_V1, LED_COUNT_TOTAL_NL_20x20_V1, LETTER_GRID_NL_20x20_V1, WORDS_NL_20x20_V1, WORDS_NL_20x20_V1_COUNT, EXTRA_MINUTES_NL_20x20_V1, EXTRA_MINUTES_NL_20x20_V1_COUNT, MinuteLayout::AfterGrid, 1, DIALECTS_NL, DIALECTS_NL_COUNT },
#endif
#if ENABLE_GRID_NL_105X105_LOGO_V1
  { GridVariant::NL_105x105_LOGO_V1, "NL_105x105_LOGO_V1", "Nederlands 105x105 Logo V1", "nl", "v1", LED_COUNT_GRID_NL_105x105_LOGO_V1, LED_COUNT_EXTRA_NL_105x105_LOGO_V1, LED_COUNT_TOTAL_NL_105x105_LOGO_V1, LETTER_GRID_NL_105x105_LOGO_V1, WORDS_NL_105x105_LOGO_V1, WORDS_NL_105x105_LOGO_V1_COUNT, EXTRA_MINUTES_NL_105x105_LOGO_V1, EXTRA_MINUTES_NL_105x105_LOGO_V1_COUNT, MinuteLayout::AfterGrid, 4, DIALECTS_NL, DIALECTS_NL_COUNT },
#endif
#if ENABLE_GRID_DE_50X50_V1
  { GridVariant::DE_50x50_V1, "DE_50x50_V1", "Deutsch 50x50 V1", "de", "v1", LED_COUNT_GRID_DE_50x50_V1, LED_COUNT_EXTRA_DE_50x50_V1, LED_COUNT_TOTAL_DE_50x50_V1, LETTER_GRID_DE_50x50_V1, WORDS_DE_50x50_V1, WORDS_DE_50x50_V1_COUNT, EXTRA_MINUTES_DE_50x50_V1, EXTRA_MINUTES_DE_50x50_V1_COUNT, MinuteLayout::AfterGrid, 1, DIALECTS_DE_50x50_V1, DIALECTS_DE_50x50_V1_COUNT, DIALECT_AXES_DE_50x50_V1, DIALECT_AXES_DE_50x50_V1_COUNT },
#endif
};

static const GridVariantData* activeVariant = &GRID_VARIANTS[0];
static MinuteLayout activeMinuteLayout = MinuteLayout::AfterGrid;
// Index into activeVariant->dialects. Reset on every variant switch: a dialect
// belongs to one plate, so carrying an index across variants could point at a
// phrase table the new plate has no words for.
static size_t activeDialectIndex = 0;

void applyActiveVariant(const GridVariantData* data) {
  activeVariant = data;
  activeDialectIndex = 0;
  LETTER_GRID = data->letterGrid;
  ACTIVE_WORDS = data->words;
  ACTIVE_WORD_COUNT = data->wordCount;
  EXTRA_MINUTE_LEDS = data->minuteLeds;
  EXTRA_MINUTE_LED_COUNT = data->minuteCount;
  EXTRA_MINUTE_LED_GROUP_SIZE = data->minuteGroupSize;
  activeMinuteLayout = data->minuteLayout;
}

const GridVariantData* findVariant(GridVariant variant) {
  for (size_t i = 0; i < countof(GRID_VARIANTS); ++i) {
    if (GRID_VARIANTS[i].variant == variant) {
      return &GRID_VARIANTS[i];
    }
  }
  return nullptr;
}

const GridVariantData* findVariantByKey(const char* key) {
  if (!key) return nullptr;
  for (size_t i = 0; i < countof(GRID_VARIANTS); ++i) {
    if (strcmp(GRID_VARIANTS[i].key, key) == 0) {
      return &GRID_VARIANTS[i];
    }
  }
  return nullptr;
}

// First variant carrying this ISO code. A product ships at most one plate per
// language, so "first" is also "only"; the language listing relies on this to
// deduplicate without a separate table.
const GridVariantData* findVariantByLanguage(const char* language) {
  if (!language) return nullptr;
  for (size_t i = 0; i < countof(GRID_VARIANTS); ++i) {
    if (strcmp(GRID_VARIANTS[i].language, language) == 0) {
      return &GRID_VARIANTS[i];
    }
  }
  return nullptr;
}

} // namespace

// Public state
const char* const* LETTER_GRID = GRID_VARIANTS[0].letterGrid;
const WordPosition* ACTIVE_WORDS = GRID_VARIANTS[0].words;
size_t ACTIVE_WORD_COUNT = GRID_VARIANTS[0].wordCount;
const uint16_t* EXTRA_MINUTE_LEDS = GRID_VARIANTS[0].minuteLeds;
size_t EXTRA_MINUTE_LED_COUNT = GRID_VARIANTS[0].minuteCount;
size_t EXTRA_MINUTE_LED_GROUP_SIZE = GRID_VARIANTS[0].minuteGroupSize;

GridVariant getActiveGridVariant() {
  return activeVariant->variant;
}

bool setActiveGridVariant(GridVariant variant) {
  const GridVariantData* data = findVariant(variant);
  if (!data) return false;
  applyActiveVariant(data);
  return true;
}

bool setActiveGridVariantById(uint8_t id) {
  GridVariant variant;
  switch (id) {
    case 3:  variant = GridVariant::NL_V4; break;
    case 6:  variant = GridVariant::NL_50x50_V3; break;
    case 7:  variant = GridVariant::NL_55x50_LOGO_V1; break;
    case 8:  variant = GridVariant::NL_20x20_V1; break;
    case 10: variant = GridVariant::NL_105x105_LOGO_V1; break;
    case 11: variant = GridVariant::DE_50x50_V1; break;
    default: return false;
  }
  return setActiveGridVariant(variant);
}

bool setActiveGridVariantByKey(const char* key) {
  const GridVariantData* data = findVariantByKey(key);
  if (!data) return false;
  applyActiveVariant(data);
  return true;
}

GridVariant gridVariantFromId(uint8_t id) {
  switch (id) {
    case 3:  return GridVariant::NL_V4;
    case 6:  return GridVariant::NL_50x50_V3;
    case 7:  return GridVariant::NL_55x50_LOGO_V1;
    case 8:  return GridVariant::NL_20x20_V1;
    case 10: return GridVariant::NL_105x105_LOGO_V1;
    case 11: return GridVariant::DE_50x50_V1;
    default: return GridVariant::NL_V4;
  }
}

GridVariant gridVariantFromKey(const char* key) {
  const GridVariantData* data = findVariantByKey(key);
  if (!data) {
    return GRID_VARIANTS[0].variant;
  }
  return data->variant;
}

uint8_t gridVariantToId(GridVariant variant) {
  return static_cast<uint8_t>(variant);
}

uint16_t getActiveLedCountGrid() {
  return activeVariant->ledCountGrid;
}

uint16_t getActiveLedCountExtra() {
  return computeExtraLedCount(activeVariant);
}

uint16_t getActiveLedCountTotal() {
  return computeTotalLedCount(activeVariant);
}

const GridVariantInfo* getGridVariantInfos(size_t& count) {
  static GridVariantInfo infos[countof(GRID_VARIANTS)];
  for (size_t i = 0; i < countof(GRID_VARIANTS); ++i) {
    infos[i].variant = GRID_VARIANTS[i].variant;
    infos[i].key = GRID_VARIANTS[i].key;
    infos[i].label = GRID_VARIANTS[i].label;
    infos[i].language = GRID_VARIANTS[i].language;
    infos[i].version = GRID_VARIANTS[i].version;
  }
  count = countof(GRID_VARIANTS);
  return infos;
}

const GridVariantInfo* getGridVariantInfo(GridVariant variant) {
  const GridVariantData* data = findVariant(variant);
  if (!data) return nullptr;
  static GridVariantInfo info;
  info.variant = data->variant;
  info.key = data->key;
  info.label = data->label;
  info.language = data->language;
  info.version = data->version;
  return &info;
}

const PhraseRules* getActivePhraseRules() {
  const ClockDialect* dialect = getActiveDialect();
  return dialect ? dialect->rules : nullptr;
}

// ---------------------------------------------------------------------------
// Language and dialect
// ---------------------------------------------------------------------------

size_t getLanguageCount() {
  size_t count = 0;
  for (size_t i = 0; i < countof(GRID_VARIANTS); ++i) {
    if (findVariantByLanguage(GRID_VARIANTS[i].language) == &GRID_VARIANTS[i]) {
      ++count;  // first variant carrying this language
    }
  }
  return count;
}

const char* getLanguageCode(size_t index) {
  size_t seen = 0;
  for (size_t i = 0; i < countof(GRID_VARIANTS); ++i) {
    if (findVariantByLanguage(GRID_VARIANTS[i].language) != &GRID_VARIANTS[i]) {
      continue;  // duplicate of an earlier variant's language
    }
    if (seen == index) return GRID_VARIANTS[i].language;
    ++seen;
  }
  return nullptr;
}

const char* getActiveLanguage() {
  return activeVariant->language;
}

bool hasLanguage(const char* code) {
  return findVariantByLanguage(code) != nullptr;
}

bool setActiveLanguage(const char* code) {
  const GridVariantData* data = findVariantByLanguage(code);
  if (!data) return false;
  applyActiveVariant(data);
  return true;
}

size_t getDialectCount() {
  return activeVariant->dialectCount;
}

const ClockDialect* getDialect(size_t index) {
  if (index >= activeVariant->dialectCount) return nullptr;
  return &activeVariant->dialects[index];
}

const ClockDialect* getActiveDialect() {
  // Defensive: a variant registered with an empty dialect list would otherwise
  // read past the array. The registry never does this, but getActivePhraseRules
  // dereferences the result on every render.
  if (!activeVariant || activeVariant->dialectCount == 0) return nullptr;
  const size_t index =
      activeDialectIndex < activeVariant->dialectCount ? activeDialectIndex : 0;
  return &activeVariant->dialects[index];
}

bool setActiveDialect(const char* id) {
  if (!id) return false;
  for (size_t i = 0; i < activeVariant->dialectCount; ++i) {
    if (strcmp(activeVariant->dialects[i].id, id) == 0) {
      activeDialectIndex = i;
      return true;
    }
  }
  return false;
}

size_t getDialectAxisCount() {
  return activeVariant ? activeVariant->axisCount : 0;
}

const DialectAxis* getDialectAxis(size_t index) {
  if (!activeVariant || index >= activeVariant->axisCount) return nullptr;
  return &activeVariant->axes[index];
}

// Index of `axisId` within the active variant's axis list. Also the index into
// every dialect's axisValues array, which is what makes the parallel arrays
// line up.
static bool findAxisIndex(const char* axisId, size_t* out) {
  if (!axisId || !activeVariant) return false;
  for (size_t i = 0; i < activeVariant->axisCount; ++i) {
    if (strcmp(activeVariant->axes[i].id, axisId) == 0) {
      *out = i;
      return true;
    }
  }
  return false;
}

static bool axisHasValue(const DialectAxis& axis, const char* value) {
  if (!value) return false;
  for (size_t i = 0; i < axis.optionCount; ++i) {
    if (strcmp(axis.options[i].value, value) == 0) return true;
  }
  return false;
}

const char* getActiveDialectAxisValue(const char* axisId) {
  size_t index = 0;
  if (!findAxisIndex(axisId, &index)) return nullptr;
  const ClockDialect* active = getActiveDialect();
  if (!active || !active->axisValues) return nullptr;
  return active->axisValues[index];
}

const ClockDialect* findDialectByAxisChange(const char* axisId, const char* value) {
  size_t changed = 0;
  if (!findAxisIndex(axisId, &changed)) return nullptr;
  if (!axisHasValue(activeVariant->axes[changed], value)) return nullptr;

  const ClockDialect* active = getActiveDialect();
  if (!active || !active->axisValues) return nullptr;

  // Keep every other axis where it is; only the named one moves. This is what
  // makes the two questions independent from the customer's point of view:
  // answering one never silently re-answers the other.
  for (size_t d = 0; d < activeVariant->dialectCount; ++d) {
    const ClockDialect& candidate = activeVariant->dialects[d];
    if (!candidate.axisValues) continue;
    bool match = true;
    for (size_t a = 0; a < activeVariant->axisCount; ++a) {
      const char* want = (a == changed) ? value : active->axisValues[a];
      if (strcmp(candidate.axisValues[a], want) != 0) {
        match = false;
        break;
      }
    }
    if (match) return &candidate;
  }
  return nullptr;
}

const WordPosition* find_word(const char* name) {
  if (!name) return nullptr;
  for (size_t i = 0; i < ACTIVE_WORD_COUNT; ++i) {
    if (strcmp(ACTIVE_WORDS[i].word, name) == 0) {
      return &ACTIVE_WORDS[i];
    }
  }
  return nullptr;
}

bool isLedUsedByActiveWords(uint16_t ledIndex) {
  const int idx = static_cast<int>(ledIndex);
  for (size_t w = 0; w < ACTIVE_WORD_COUNT; ++w) {
    for (int i = 0; i < ACTIVE_WORDS[w].count; ++i) {
      if (ACTIVE_WORDS[w].indices[i] == idx) {
        return true;
      }
    }
  }
  return false;
}
