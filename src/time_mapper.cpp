// time_mapper.cpp
#include <vector>
#include <time.h>
#include "config.h"
#include "grid_layout.h"
#include "led_events.h"
#include "log.h"
#include "wordposition.h"
#include "time_mapper.h"

static const char* HOURS[] = {
  "TWAALF", "EEN", "TWEE", "DRIE", "VIER", "VIJF", "ZES",
  "ZEVEN", "ACHT", "NEGEN", "TIEN", "ELF"
};

std::vector<uint16_t> get_leds_for_word(const char* word) {
  std::vector<uint16_t> result;
  const WordPosition* w = find_word(word);
  if (w) {
    for (int i = 0; i < 32 && w->indices[i] != 0; ++i) {
      result.push_back(static_cast<uint16_t>(w->indices[i]));
    }
  }
  return result;
}

// Helper: merges multiple LED vectors
std::vector<uint16_t> merge_leds(std::initializer_list<std::vector<uint16_t>> lists) {
  std::vector<uint16_t> result;
  for (const auto& list : lists) {
    result.insert(result.end(), list.begin(), list.end());
  }
  return result;
}

std::vector<uint16_t> get_led_indices_for_time(struct tm* timeinfo) {
  int hour = timeinfo->tm_hour;
  int minute = timeinfo->tm_min;

  // Always round down to the lower 5-minute interval
  int rounded_minute = (minute / 5) * 5;
  int extra_minutes = minute % 5;

  if (rounded_minute == 60) {
    rounded_minute = 0;
    hour = (hour + 1) % 24;
  }

  int hour12 = hour % 12;
  if (rounded_minute >= 20) hour12 = (hour12 + 1) % 12; // After 'over' or 'half' we look at next hour

  std::vector<uint16_t> leds = merge_leds({
    get_leds_for_word("HET"),
    get_leds_for_word("IS")
  });

  switch (rounded_minute) {
    case 0:
      leds = merge_leds({leds, get_leds_for_word(HOURS[hour12]), get_leds_for_word("UUR")});
      break;
    case 5:
      leds = merge_leds({leds, get_leds_for_word("VIJF_M"), get_leds_for_word("OVER"), get_leds_for_word(HOURS[hour12])});
      break;
    case 10:
      leds = merge_leds({leds, get_leds_for_word("TIEN_M"), get_leds_for_word("OVER"), get_leds_for_word(HOURS[hour12])});
      break;
    case 15:
      leds = merge_leds({leds, get_leds_for_word("KWART"), get_leds_for_word("OVER"), get_leds_for_word(HOURS[hour12])});
      break;
    case 20:
      leds = merge_leds({leds, get_leds_for_word("TIEN_M"), get_leds_for_word("VOOR"), get_leds_for_word("HALF"), get_leds_for_word(HOURS[hour12])});
      break;
    case 25:
      leds = merge_leds({leds, get_leds_for_word("VIJF_M"), get_leds_for_word("VOOR"), get_leds_for_word("HALF"), get_leds_for_word(HOURS[hour12])});
      break;
    case 30:
      leds = merge_leds({leds, get_leds_for_word("HALF"), get_leds_for_word(HOURS[hour12])});
      break;
    case 35:
      leds = merge_leds({leds, get_leds_for_word("VIJF_M"), get_leds_for_word("OVER"), get_leds_for_word("HALF"), get_leds_for_word(HOURS[hour12])});
      break;
    case 40:
      leds = merge_leds({leds, get_leds_for_word("TIEN_M"), get_leds_for_word("OVER"), get_leds_for_word("HALF"), get_leds_for_word(HOURS[hour12])});
      break;
    case 45:
      leds = merge_leds({leds, get_leds_for_word("KWART"), get_leds_for_word("VOOR"), get_leds_for_word(HOURS[hour12])});
      break;
    case 50:
      leds = merge_leds({leds, get_leds_for_word("TIEN_M"), get_leds_for_word("VOOR"), get_leds_for_word(HOURS[hour12])});
      break;
    case 55:
      leds = merge_leds({leds, get_leds_for_word("VIJF_M"), get_leds_for_word("VOOR"), get_leds_for_word(HOURS[hour12])});
      break;
  }

  // Add extra minute LEDs if needed (skip when they are used for LED events, e.g. NTP failed / BLE)
#if SUPPORT_MINUTE_LEDS
  if (EXTRA_MINUTE_LED_GROUP_SIZE > 0) {
#if LED_STATUS_EVENTS_ENABLED && LED_STATUS_EVENT_USE_MINUTE_LEDS
    if (!ledEventIsActive()) {
#endif
      size_t symbolCount = EXTRA_MINUTE_LED_COUNT / EXTRA_MINUTE_LED_GROUP_SIZE;
      for (int i = 0; i < extra_minutes && i < 4 && i < static_cast<int>(symbolCount); ++i) {
        size_t base = static_cast<size_t>(i) * EXTRA_MINUTE_LED_GROUP_SIZE;
        for (size_t j = 0; j < EXTRA_MINUTE_LED_GROUP_SIZE; ++j) {
          leds.push_back(EXTRA_MINUTE_LEDS[base + j]);
        }
      }
#if LED_STATUS_EVENTS_ENABLED && LED_STATUS_EVENT_USE_MINUTE_LEDS
    }
#endif
  }
#endif

  return leds;
}

static void append_seg(std::vector<WordSegment>& segs, const char* key) {
  segs.push_back(WordSegment{key, get_leds_for_word(key)});
}

// Build the phrase as word-segments (without extra minute LEDs)
std::vector<WordSegment> get_word_segments_with_keys(struct tm* timeinfo) {
  int hour = timeinfo->tm_hour;
  int minute = timeinfo->tm_min;

  int rounded_minute = (minute / 5) * 5;
  if (rounded_minute == 60) {
    rounded_minute = 0;
    hour = (hour + 1) % 24;
  }

  int hour12 = hour % 12;
  if (rounded_minute >= 20) hour12 = (hour12 + 1) % 12;

  std::vector<WordSegment> segs;
  // Split "HET" and "IS" so they can animate separately
  append_seg(segs, "HET");
  append_seg(segs, "IS");

  switch (rounded_minute) {
    case 0:
      append_seg(segs, HOURS[hour12]);
      append_seg(segs, "UUR");
      break;
    case 5:
      append_seg(segs, "VIJF_M");
      append_seg(segs, "OVER");
      append_seg(segs, HOURS[hour12]);
      break;
    case 10:
      append_seg(segs, "TIEN_M");
      append_seg(segs, "OVER");
      append_seg(segs, HOURS[hour12]);
      break;
    case 15:
      append_seg(segs, "KWART");
      append_seg(segs, "OVER");
      append_seg(segs, HOURS[hour12]);
      break;
    case 20:
      append_seg(segs, "TIEN_M");
      append_seg(segs, "VOOR");
      append_seg(segs, "HALF");
      append_seg(segs, HOURS[hour12]);
      break;
    case 25:
      append_seg(segs, "VIJF_M");
      append_seg(segs, "VOOR");
      append_seg(segs, "HALF");
      append_seg(segs, HOURS[hour12]);
      break;
    case 30:
      append_seg(segs, "HALF");
      append_seg(segs, HOURS[hour12]);
      break;
    case 35:
      append_seg(segs, "VIJF_M");
      append_seg(segs, "OVER");
      append_seg(segs, "HALF");
      append_seg(segs, HOURS[hour12]);
      break;
    case 40:
      append_seg(segs, "TIEN_M");
      append_seg(segs, "OVER");
      append_seg(segs, "HALF");
      append_seg(segs, HOURS[hour12]);
      break;
    case 45:
      append_seg(segs, "KWART");
      append_seg(segs, "VOOR");
      append_seg(segs, HOURS[hour12]);
      break;
    case 50:
      append_seg(segs, "TIEN_M");
      append_seg(segs, "VOOR");
      append_seg(segs, HOURS[hour12]);
      break;
    case 55:
      append_seg(segs, "VIJF_M");
      append_seg(segs, "VOOR");
      append_seg(segs, HOURS[hour12]);
      break;
  }

  return segs;
}

// Preserve legacy API for callers that only need LED indices
std::vector<std::vector<uint16_t>> get_word_segments_for_time(struct tm* timeinfo) {
  auto withKeys = get_word_segments_with_keys(timeinfo);
  std::vector<std::vector<uint16_t>> segs;
  segs.reserve(withKeys.size());
  for (const auto& seg : withKeys) {
    segs.push_back(seg.leds);
  }
  return segs;
}
