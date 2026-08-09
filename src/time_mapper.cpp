// time_mapper.cpp
#include <vector>
#include <time.h>
#include "config.h"
#include "grid_layout.h"
#include "led_events.h"
#include "log.h"
#include "phrase_rules.h"
#include "wordposition.h"
#include "time_mapper.h"

std::vector<uint16_t> get_leds_for_word(const char* word) {
  std::vector<uint16_t> result;
  const WordPosition* w = find_word(word);
  if (w) {
    for (int i = 0; i < w->count; ++i) {
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

namespace {

// Resolve the five-minute step for a wall-clock time and hand back the rule
// that describes it plus the hour to display.
const PhraseStep* resolveStep(struct tm* timeinfo, int& hour12Out) {
  int hour = timeinfo->tm_hour;
  int minute = timeinfo->tm_min;

  // Always round down to the lower 5-minute interval
  int rounded_minute = (minute / 5) * 5;
  if (rounded_minute == 60) {
    rounded_minute = 0;
    hour = (hour + 1) % 24;
  }

  const PhraseRules* rules = getActivePhraseRules();
  if (!rules) return nullptr;

  const PhraseStep* step = &rules->steps[rounded_minute / 5];
  // After 'over'/'half' (or the German equivalents) the phrase names the next
  // hour — which step does that is part of the language's rule table.
  hour12Out = (hour + step->hourOffset) % 12;
  return step;
}

// The hour word for this step. On a step that also lights OCLOCK a variant may
// offer an alternate form ("es ist EIN Uhr" vs "fünf nach EINS"); fall back to
// the regular key when the variant does not define one.
const char* hourKeyForStep(const PhraseStep& step, int hour12) {
  if (step.withOClock) {
    const char* alt = phraseHourAltKey(hour12);
    if (find_word(alt)) return alt;
  }
  return phraseHourKey(hour12);
}

} // namespace

std::vector<uint16_t> get_led_indices_for_time(struct tm* timeinfo) {
  std::vector<uint16_t> leds;

  for (const auto& seg : get_word_segments_with_keys(timeinfo)) {
    leds.insert(leds.end(), seg.leds.begin(), seg.leds.end());
  }

  // Add extra minute LEDs if needed (skip when they are used for LED events, e.g. NTP failed / BLE)
#if SUPPORT_MINUTE_LEDS
  const int extra_minutes = timeinfo->tm_min % 5;
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

// Build the phrase as word-segments (without extra minute LEDs).
// Emission order per step: PREFIX_A, PREFIX_B, slots…, hour, [OCLOCK].
std::vector<WordSegment> get_word_segments_with_keys(struct tm* timeinfo) {
  std::vector<WordSegment> segs;

  int hour12 = 0;
  const PhraseStep* step = resolveStep(timeinfo, hour12);
  if (!step) return segs;

  // Split the prefix into two segments so they can animate separately.
  // Variants without a prefix (e.g. the 20x20 grid) yield empty segments here,
  // which callers already tolerate.
  append_seg(segs, "PREFIX_A");
  append_seg(segs, "PREFIX_B");

  for (const char* slot : step->slots) {
    if (!slot) continue;
    append_seg(segs, slot);
  }

  append_seg(segs, hourKeyForStep(*step, hour12));

  if (step->withOClock) {
    append_seg(segs, "OCLOCK");
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
