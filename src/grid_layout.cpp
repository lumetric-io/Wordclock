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

#if !defined(ENABLE_GRID_NL_V1) && !defined(ENABLE_GRID_NL_V2) && !defined(ENABLE_GRID_NL_V3) && \
    !defined(ENABLE_GRID_NL_V4) && !defined(ENABLE_GRID_NL_50X50_V1) && !defined(ENABLE_GRID_NL_50X50_V2) && \
    !defined(ENABLE_GRID_NL_50X50_V3) && !defined(ENABLE_GRID_NL_60X50_LOGO_V1) && \
    !defined(ENABLE_GRID_NL_20X20_V1)
#define ENABLE_GRID_NL_V1 1
#define ENABLE_GRID_NL_V2 1
#define ENABLE_GRID_NL_V3 1
#define ENABLE_GRID_NL_V4 1
#define ENABLE_GRID_NL_50X50_V1 1
#define ENABLE_GRID_NL_50X50_V2 1
#define ENABLE_GRID_NL_50X50_V3 1
#define ENABLE_GRID_NL_60X50_LOGO_V1 1
#define ENABLE_GRID_NL_20X20_V1 1
#endif

#ifndef ENABLE_GRID_NL_V1
#define ENABLE_GRID_NL_V1 0
#endif
#ifndef ENABLE_GRID_NL_V2
#define ENABLE_GRID_NL_V2 0
#endif
#ifndef ENABLE_GRID_NL_V3
#define ENABLE_GRID_NL_V3 0
#endif
#ifndef ENABLE_GRID_NL_V4
#define ENABLE_GRID_NL_V4 0
#endif
#ifndef ENABLE_GRID_NL_50X50_V1
#define ENABLE_GRID_NL_50X50_V1 0
#endif
#ifndef ENABLE_GRID_NL_50X50_V2
#define ENABLE_GRID_NL_50X50_V2 0
#endif
#ifndef ENABLE_GRID_NL_50X50_V3
#define ENABLE_GRID_NL_50X50_V3 0
#endif
#ifndef ENABLE_GRID_NL_60X50_LOGO_V1
#define ENABLE_GRID_NL_60X50_LOGO_V1 0
#endif
#ifndef ENABLE_GRID_NL_20X20_V1
#define ENABLE_GRID_NL_20X20_V1 0
#endif

#if !(ENABLE_GRID_NL_V1 || ENABLE_GRID_NL_V2 || ENABLE_GRID_NL_V3 || ENABLE_GRID_NL_V4 || \
      ENABLE_GRID_NL_50X50_V1 || ENABLE_GRID_NL_50X50_V2 || ENABLE_GRID_NL_50X50_V3 || \
      ENABLE_GRID_NL_60X50_LOGO_V1 || ENABLE_GRID_NL_20X20_V1)
#error "At least one grid variant must be enabled."
#endif

#if ENABLE_GRID_NL_V1
#include "grid_variants/nl_v1.h"
#endif
#if ENABLE_GRID_NL_V2
#include "grid_variants/nl_v2.h"
#endif
#if ENABLE_GRID_NL_V3
#include "grid_variants/nl_v3.h"
#endif
#if ENABLE_GRID_NL_V4
#include "grid_variants/nl_v4.h"
#endif
#if ENABLE_GRID_NL_50X50_V1
#include "grid_variants/nl_50x50_v1.h"
#endif
#if ENABLE_GRID_NL_50X50_V2
#include "grid_variants/nl_50x50_v2.h"
#endif
#if ENABLE_GRID_NL_50X50_V3
#include "grid_variants/nl_50x50_v3.h"
#endif
#if ENABLE_GRID_NL_60X50_LOGO_V1
#include "grid_variants/nl_60x50_logo_v1.h"
#endif
#if ENABLE_GRID_NL_20X20_V1
#include "grid_variants/nl_20x20_v1.h"
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
#if ENABLE_GRID_NL_V1
  { GridVariant::NL_V1, "NL_V1", "Nederlands V1", "nl", "v1", LED_COUNT_GRID_NL_V1, LED_COUNT_EXTRA_NL_V1, LED_COUNT_TOTAL_NL_V1, LETTER_GRID_NL_V1, WORDS_NL_V1, WORDS_NL_V1_COUNT, EXTRA_MINUTES_NL_V1, EXTRA_MINUTES_NL_V1_COUNT, MinuteLayout::AfterGrid },
#endif
#if ENABLE_GRID_NL_V2
  { GridVariant::NL_V2, "NL_V2", "Nederlands V2", "nl", "v2", LED_COUNT_GRID_NL_V2, LED_COUNT_EXTRA_NL_V2, LED_COUNT_TOTAL_NL_V2, LETTER_GRID_NL_V2, WORDS_NL_V2, WORDS_NL_V2_COUNT, EXTRA_MINUTES_NL_V2, EXTRA_MINUTES_NL_V2_COUNT, MinuteLayout::AfterGrid },
#endif
#if ENABLE_GRID_NL_V3
  { GridVariant::NL_V3, "NL_V3", "Nederlands V3", "nl", "v3", LED_COUNT_GRID_NL_V3, LED_COUNT_EXTRA_NL_V3, LED_COUNT_TOTAL_NL_V3, LETTER_GRID_NL_V3, WORDS_NL_V3, WORDS_NL_V3_COUNT, EXTRA_MINUTES_NL_V3, EXTRA_MINUTES_NL_V3_COUNT, MinuteLayout::AfterGrid },
#endif
#if ENABLE_GRID_NL_V4
  { GridVariant::NL_V4, "NL_V4", "Nederlands V4", "nl", "v4", LED_COUNT_GRID_NL_V4, LED_COUNT_EXTRA_NL_V4, LED_COUNT_TOTAL_NL_V4, LETTER_GRID_NL_V4, WORDS_NL_V4, WORDS_NL_V4_COUNT, EXTRA_MINUTES_NL_V4, EXTRA_MINUTES_NL_V4_COUNT, MinuteLayout::AfterGrid },
#endif
#if ENABLE_GRID_NL_50X50_V1
  { GridVariant::NL_50x50_V1, "NL_50x50_V1", "Nederlands 50x50 V1", "nl", "v1", LED_COUNT_GRID_NL_50x50_V1, LED_COUNT_EXTRA_NL_50x50_V1, LED_COUNT_TOTAL_NL_50x50_V1, LETTER_GRID_NL_50x50_V1, WORDS_NL_50x50_V1, WORDS_NL_50x50_V1_COUNT, EXTRA_MINUTES_NL_50x50_V1, EXTRA_MINUTES_NL_50x50_V1_COUNT, MinuteLayout::MixedIntoGrid },
#endif
#if ENABLE_GRID_NL_50X50_V2
  { GridVariant::NL_50x50_V2, "NL_50x50_V2", "Nederlands 50x50 V2", "nl", "v2", LED_COUNT_GRID_NL_50x50_V2, LED_COUNT_EXTRA_NL_50x50_V2, LED_COUNT_TOTAL_NL_50x50_V2, LETTER_GRID_NL_50x50_V2, WORDS_NL_50x50_V2, WORDS_NL_50x50_V2_COUNT, EXTRA_MINUTES_NL_50x50_V2, EXTRA_MINUTES_NL_50x50_V2_COUNT, MinuteLayout::AfterGrid },
#endif
#if ENABLE_GRID_NL_50X50_V3
  { GridVariant::NL_50x50_V3, "NL_50x50_V3", "Nederlands 50x50 V3", "nl", "v3", LED_COUNT_GRID_NL_50x50_V3, LED_COUNT_EXTRA_NL_50x50_V3, LED_COUNT_TOTAL_NL_50x50_V3, LETTER_GRID_NL_50x50_V3, WORDS_NL_50x50_V3, WORDS_NL_50x50_V3_COUNT, EXTRA_MINUTES_NL_50x50_V3, EXTRA_MINUTES_NL_50x50_V3_COUNT, MinuteLayout::AfterGrid },
#endif
#if ENABLE_GRID_NL_60X50_LOGO_V1
  { GridVariant::NL_60x50_LOGO_V1, "NL_60x50_LOGO_V1", "Nederlands 60x50 Logo V1", "nl", "v1", LED_COUNT_GRID_NL_60x50_LOGO_V1, LED_COUNT_EXTRA_NL_60x50_LOGO_V1, LED_COUNT_TOTAL_NL_60x50_LOGO_V1, LETTER_GRID_NL_60x50_LOGO_V1, WORDS_NL_60x50_LOGO_V1, WORDS_NL_60x50_LOGO_V1_COUNT, EXTRA_MINUTES_NL_60x50_LOGO_V1, EXTRA_MINUTES_NL_60x50_LOGO_V1_COUNT, MinuteLayout::AfterGrid },
#endif
#if ENABLE_GRID_NL_20X20_V1
  { GridVariant::NL_20x20_V1, "NL_20x20_V1", "Nederlands 20x20 V1", "nl", "v1", LED_COUNT_GRID_NL_20x20_V1, LED_COUNT_EXTRA_NL_20x20_V1, LED_COUNT_TOTAL_NL_20x20_V1, LETTER_GRID_NL_20x20_V1, WORDS_NL_20x20_V1, WORDS_NL_20x20_V1_COUNT, EXTRA_MINUTES_NL_20x20_V1, EXTRA_MINUTES_NL_20x20_V1_COUNT, MinuteLayout::AfterGrid },
#endif
};

static const GridVariantData* activeVariant = &GRID_VARIANTS[0];
static MinuteLayout activeMinuteLayout = MinuteLayout::AfterGrid;

void applyActiveVariant(const GridVariantData* data) {
  activeVariant = data;
  LETTER_GRID = data->letterGrid;
  ACTIVE_WORDS = data->words;
  ACTIVE_WORD_COUNT = data->wordCount;
  EXTRA_MINUTE_LEDS = data->minuteLeds;
  EXTRA_MINUTE_LED_COUNT = data->minuteCount;
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

} // namespace

// Public state
const char* const* LETTER_GRID = GRID_VARIANTS[0].letterGrid;
const WordPosition* ACTIVE_WORDS = GRID_VARIANTS[0].words;
size_t ACTIVE_WORD_COUNT = GRID_VARIANTS[0].wordCount;
const uint16_t* EXTRA_MINUTE_LEDS = GRID_VARIANTS[0].minuteLeds;
size_t EXTRA_MINUTE_LED_COUNT = GRID_VARIANTS[0].minuteCount;

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
  if (id >= countof(GRID_VARIANTS)) return false;
  return setActiveGridVariant(GRID_VARIANTS[id].variant);
}

bool setActiveGridVariantByKey(const char* key) {
  const GridVariantData* data = findVariantByKey(key);
  if (!data) return false;
  applyActiveVariant(data);
  return true;
}

GridVariant gridVariantFromId(uint8_t id) {
  if (id >= countof(GRID_VARIANTS)) {
    return GRID_VARIANTS[0].variant;
  }
  return GRID_VARIANTS[id].variant;
}

GridVariant gridVariantFromKey(const char* key) {
  const GridVariantData* data = findVariantByKey(key);
  if (!data) {
    return GRID_VARIANTS[0].variant;
  }
  return data->variant;
}

uint8_t gridVariantToId(GridVariant variant) {
  for (size_t i = 0; i < countof(GRID_VARIANTS); ++i) {
    if (GRID_VARIANTS[i].variant == variant) {
      return static_cast<uint8_t>(i);
    }
  }
  return 0;
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

const WordPosition* find_word(const char* name) {
  if (!name) return nullptr;
  for (size_t i = 0; i < ACTIVE_WORD_COUNT; ++i) {
    if (strcmp(ACTIVE_WORDS[i].word, name) == 0) {
      return &ACTIVE_WORDS[i];
    }
  }
  return nullptr;
}
