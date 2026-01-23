#include "led_controller.h"
#include "config.h"
#include "grid_layout.h"
#include "led_state.h"
#include "night_mode.h"
#if defined(PRODUCT_VARIANT_LOGO)
#include "logo_leds.h"
#endif

#include <vector>

#ifndef PIO_UNIT_TESTING
// Instance of the NeoPixel strip; length is synchronized with the active grid variant.
static Adafruit_NeoPixel strip;
static uint16_t activeStripLength = 0;
static bool g_ledsSuspended = false;

static void ensureStripLength() {
  uint16_t required = getActiveLedCountTotal();
#if defined(PRODUCT_VARIANT_LOGO)
  required = getTotalStripLength();
#endif
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

#if defined(PRODUCT_VARIANT_LOGO) && !defined(PIO_UNIT_TESTING)
static uint8_t applyBrightness(uint8_t value, uint8_t brightness) {
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * brightness) / 255);
}

static void renderLogoLeds() {
  const uint16_t logoStart = getLogoStartIndex();
  const LogoLedColor* colors = logoLeds.getColors();
  uint8_t logoBrightness = nightMode.applyToBrightness(logoLeds.getBrightness());
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
#endif

void initLeds() {
#ifndef PIO_UNIT_TESTING
  ensureStripLength();
#if defined(PRODUCT_VARIANT_LOGO)
  strip.setBrightness(255);
#else
  uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
  strip.setBrightness(brightness);
#endif
  strip.clear();
  strip.show();
#else
  lastShown.clear();
#endif
}

void setLedsSuspended(bool suspended) {
#ifndef PIO_UNIT_TESTING
  g_ledsSuspended = suspended;
  if (g_ledsSuspended) {
    ensureStripLength();
    strip.clear();
    strip.setBrightness(0);
    strip.show();
  }
#else
  (void)suspended;
#endif
}

void showLeds(const std::vector<uint16_t> &ledIndices) {
#ifndef PIO_UNIT_TESTING
  ensureStripLength();
  if (g_ledsSuspended) {
    strip.clear();
    strip.setBrightness(0);
    strip.show();
    return;
  }
  strip.clear();
#if defined(PRODUCT_VARIANT_LOGO)
  uint8_t clockBrightness = nightMode.applyToBrightness(ledState.getBrightness());
  for (uint16_t idx : ledIndices) {
    if (idx < strip.numPixels()) {
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
  renderLogoLeds();
  strip.setBrightness(255);
#else
  for (uint16_t idx : ledIndices) {
    if (idx < strip.numPixels()) {
      // Use the calculated RGB and W
      uint8_t r, g, b, w;
      ledState.getRGBW(r, g, b, w);
      strip.setPixelColor(idx, strip.Color(r, g, b, w));
    }
  }
  uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
  strip.setBrightness(brightness);
#endif
  strip.show();
#else
  lastShown = ledIndices;
#endif
}

void showLedsWithBrightness(const std::vector<uint16_t> &ledIndices, 
                            const std::vector<uint8_t> &brightnessMultipliers) {
#ifndef PIO_UNIT_TESTING
  ensureStripLength();
  if (g_ledsSuspended) {
    strip.clear();
    strip.setBrightness(0);
    strip.show();
    return;
  }
  strip.clear();
  uint8_t r, g, b, w;
  ledState.getRGBW(r, g, b, w);

  for (size_t i = 0; i < ledIndices.size() && i < brightnessMultipliers.size(); ++i) {
    uint16_t idx = ledIndices[i];
    if (idx < strip.numPixels()) {
      uint8_t multiplier = brightnessMultipliers[i];
      uint8_t finalR = (r * multiplier) / 255;
      uint8_t finalG = (g * multiplier) / 255;
      uint8_t finalB = (b * multiplier) / 255;
      uint8_t finalW = (w * multiplier) / 255;
#if defined(PRODUCT_VARIANT_LOGO)
      uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
      strip.setPixelColor(
        idx,
        strip.Color(
          applyBrightness(finalR, brightness),
          applyBrightness(finalG, brightness),
          applyBrightness(finalB, brightness),
          applyBrightness(finalW, brightness)
        )
      );
#else
      strip.setPixelColor(idx, strip.Color(finalR, finalG, finalB, finalW));
#endif
    }
  }
#if defined(PRODUCT_VARIANT_LOGO)
  renderLogoLeds();
  strip.setBrightness(255);
#else
  uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
  strip.setBrightness(brightness);
#endif
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
