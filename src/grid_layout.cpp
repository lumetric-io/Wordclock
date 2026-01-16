#include "grid_layout.h"

#include <Arduino.h>
#include <string.h>

#include "grid_variants/nl_50x50_v3.h"

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
  { GridVariant::NL_50x50, "NL_50x50", "Nederlands 50x50", "nl", "v3", LED_COUNT_GRID_NL_50x50_V3, LED_COUNT_EXTRA_NL_50x50_V3, LED_COUNT_TOTAL_NL_50x50_V3, LETTER_GRID_NL_50x50_V3, WORDS_NL_50x50_V3, WORDS_NL_50x50_V3_COUNT, EXTRA_MINUTES_NL_50x50_V3, EXTRA_MINUTES_NL_50x50_V3_COUNT, MinuteLayout::AfterGrid }
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
const char* const* LETTER_GRID = LETTER_GRID_NL_50x50_V3;
const WordPosition* ACTIVE_WORDS = WORDS_NL_50x50_V3;
size_t ACTIVE_WORD_COUNT = WORDS_NL_50x50_V3_COUNT;
const uint16_t* EXTRA_MINUTE_LEDS = EXTRA_MINUTES_NL_50x50_V3;
size_t EXTRA_MINUTE_LED_COUNT = EXTRA_MINUTES_NL_50x50_V3_COUNT;

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
  applyActiveVariant(&GRID_VARIANTS[id]);
  return true;
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
  for (size_t i = 0; i < countof(GRID_VARIANTS); ++i) {
    if (GRID_VARIANTS[i].variant == variant) {
      static GridVariantInfo info;
      info.variant = GRID_VARIANTS[i].variant;
      info.key = GRID_VARIANTS[i].key;
      info.label = GRID_VARIANTS[i].label;
      info.language = GRID_VARIANTS[i].language;
      info.version = GRID_VARIANTS[i].version;
      return &info;
    }
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
