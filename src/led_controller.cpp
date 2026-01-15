#include "led_controller.h"
#include "config.h"
#include "grid_layout.h"
#include "logo_leds.h"
#include "led_state.h"
#include "night_mode.h"

#include <vector>

#ifndef PIO_UNIT_TESTING
// Instance of the NeoPixel strip; length is synchronized with the active grid variant.
static Adafruit_NeoPixel strip;
static uint16_t activeStripLength = 0;

static void ensureStripLength() {
  uint16_t required = getTotalStripLength();
  if (required == 0) {
    required = 1; // keep strip functional even if layout is missing
  }
  if (required != activeStripLength) {
    activeStripLength = required;
    strip.updateType(NEO_GRBW + NEO_KHZ800);
    strip.setPin(DATA_PIN);
    strip.updateLength(activeStripLength);
    strip.begin();
    strip.clear();
    strip.show();
  }
}
#else
static std::vector<uint16_t> lastShown;
#endif

static void renderLeds(const std::vector<uint16_t> &ledIndices, bool includeLogo) {
#ifndef PIO_UNIT_TESTING
  ensureStripLength();
  strip.clear();
  uint8_t clockBrightness = nightMode.applyToBrightness(ledState.getBrightness());
  uint8_t logoBrightness = nightMode.applyToBrightness(logoLeds.getBrightness());

  auto applyBrightness = [](uint8_t value, uint8_t brightness) -> uint8_t {
    return static_cast<uint8_t>((static_cast<uint16_t>(value) * brightness) / 255);
  };

  for (uint16_t idx : ledIndices) {
    if (idx < strip.numPixels()) {
      // Use the calculated RGB and W
      uint8_t r, g, b, w;
      ledState.getRGBW(r, g, b, w);
      strip.setPixelColor(
        idx,
        strip.Color(
          applyBrightness(r, clockBrightness),
          applyBrightness(g, clockBrightness),
          applyBrightness(b, clockBrightness),
          applyBrightness(w, clockBrightness)
        )
      );
    }
  }

  if (includeLogo) {
    const uint16_t logoStart = getLogoStartIndex();
    const LogoLedColor* colors = logoLeds.getColors();
    for (uint16_t i = 0; i < LOGO_LED_COUNT; ++i) {
      uint16_t stripIdx = static_cast<uint16_t>(logoStart + i);
      if (stripIdx >= strip.numPixels()) break;
      const LogoLedColor& c = colors[i];
      strip.setPixelColor(
        stripIdx,
        strip.Color(
          applyBrightness(c.r, logoBrightness),
          applyBrightness(c.g, logoBrightness),
          applyBrightness(c.b, logoBrightness),
          0
        )
      );
    }
  }

  strip.setBrightness(255);
  strip.show();
#else
  if (includeLogo) {
    lastShown = ledIndices;
  } else {
    lastShown.clear();
  }
#endif
}

void initLeds() {
#ifndef PIO_UNIT_TESTING
  ensureStripLength();
  strip.setBrightness(255); // We apply brightness per segment
  strip.clear();
  strip.show();
#else
  lastShown.clear();
#endif
}

void showLeds(const std::vector<uint16_t> &ledIndices) {
  renderLeds(ledIndices, true);
}

void showLedsCombined(const std::vector<uint16_t> &ledIndices, bool includeLogo) {
  renderLeds(ledIndices, includeLogo);
}

void blinkAllLeds(uint8_t blinks, uint16_t onMs, uint16_t offMs) {
#ifndef PIO_UNIT_TESTING
  uint16_t total = getTotalStripLength();
  std::vector<uint16_t> all;
  all.reserve(total);
  for (uint16_t i = 0; i < total; ++i) {
    all.push_back(i);
  }
  for (uint8_t i = 0; i < blinks; ++i) {
    showLedsCombined(all, false); // includeLogo=false ensures full off between blinks
    delay(onMs);
    showLedsCombined({}, false);
    delay(offMs);
  }
#else
  lastShown.clear();
#endif
}

void showLedsWithBrightness(const std::vector<uint16_t> &ledIndices, 
                            const std::vector<uint8_t> &brightnessMultipliers) {
#ifndef PIO_UNIT_TESTING
  ensureStripLength();
  strip.clear();
  uint8_t r, g, b, w;
  ledState.getRGBW(r, g, b, w);
  
  for (size_t i = 0; i < ledIndices.size() && i < brightnessMultipliers.size(); ++i) {
    uint16_t idx = ledIndices[i];
    if (idx < strip.numPixels()) {
      uint8_t multiplier = brightnessMultipliers[i];
      // Apply brightness as color intensity (0-255 where 255 = full brightness)
      uint8_t finalR = (r * multiplier) / 255;
      uint8_t finalG = (g * multiplier) / 255;
      uint8_t finalB = (b * multiplier) / 255;
      uint8_t finalW = (w * multiplier) / 255;
      strip.setPixelColor(idx, strip.Color(finalR, finalG, finalB, finalW));
    }
  }
  uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
  strip.setBrightness(brightness);
  strip.show();
#else
  lastShown = ledIndices;
#endif
}

#ifdef PIO_UNIT_TESTING
const std::vector<uint16_t>& test_getLastShownLeds() {
  return lastShown;
}

void test_clearLastShownLeds() {
  lastShown.clear();
}
#endif
