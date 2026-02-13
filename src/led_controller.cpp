#include "led_controller.h"
#include "config.h"
#include "grid_layout.h"
#include "led_state.h"
#include "night_mode.h"
#if defined(PRODUCT_VARIANT_LOGO)
#include "logo_leds.h"
#endif

#include <vector>

#if defined(PRODUCT_VARIANT_LOGO) && defined(LOGO_DATA_PIN)
#define LOGO_HAS_DEDICATED_PIN 1
#else
#define LOGO_HAS_DEDICATED_PIN 0
#endif

#ifndef PIO_UNIT_TESTING
// Instance of the NeoPixel strip; length is synchronized with the active grid variant.
#if defined(PRODUCT_VARIANT_LOGO)
static Adafruit_NeoPixel clockStrip;
#if LOGO_HAS_DEDICATED_PIN
static Adafruit_NeoPixel logoStrip;
static uint16_t activeLogoStripLength = 0;
#endif
static uint16_t activeClockStripLength = 0;
#else
static Adafruit_NeoPixel strip;
static uint16_t activeStripLength = 0;
#endif
static bool g_ledsSuspended = false;

static void ensureStripLength() {
#if defined(PRODUCT_VARIANT_LOGO)
  uint16_t requiredClock = getActiveLedCountTotal();
  uint16_t requiredLogo = getLogoLedCount();
#if !LOGO_HAS_DEDICATED_PIN
  requiredClock = static_cast<uint16_t>(requiredClock + requiredLogo);
#endif
  if (requiredClock == 0) requiredClock = 1; // keep strip functional even if layout is missing

  if (requiredClock != activeClockStripLength) {
    activeClockStripLength = requiredClock;
    clockStrip.updateType(NEO_GRBW + NEO_KHZ800);
    clockStrip.setPin(DATA_PIN);
    clockStrip.updateLength(activeClockStripLength);
    clockStrip.begin();
    clockStrip.clear();
    clockStrip.show();
  }

#if LOGO_HAS_DEDICATED_PIN
  if (requiredLogo == 0) {
    requiredLogo = 1; // keep strip functional even if layout is missing
  }
  if (requiredLogo != activeLogoStripLength) {
    activeLogoStripLength = requiredLogo;
    logoStrip.updateType(NEO_GRBW + NEO_KHZ800);
    logoStrip.setPin(LOGO_DATA_PIN);
    logoStrip.updateLength(activeLogoStripLength);
    logoStrip.begin();
    logoStrip.clear();
    logoStrip.show();
  }
#endif
#else
  uint16_t required = getActiveLedCountTotal();
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
#endif
}
#else
static std::vector<uint16_t> lastShown;
#endif

#if defined(PRODUCT_VARIANT_LOGO) && !defined(PIO_UNIT_TESTING)
static uint8_t applyBrightness(uint8_t value, uint8_t brightness) {
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * brightness) / 255);
}

static void renderLogoLeds() {
  const LogoLedColor* colors = logoLeds.getColors();
  uint8_t logoBrightness = nightMode.applyToBrightness(logoLeds.getBrightness());
  uint16_t count = getLogoLedCount();
  uint16_t clockOffset = getActiveLedCountTotal();
  for (uint16_t i = 0; i < count; ++i) {
#if LOGO_HAS_DEDICATED_PIN
    if (i >= logoStrip.numPixels()) break;
#else
    uint16_t idx = static_cast<uint16_t>(clockOffset + i);
    if (idx >= clockStrip.numPixels()) break;
#endif
    const LogoLedColor& c = colors[i];
#if LOGO_HAS_DEDICATED_PIN
    logoStrip.setPixelColor(
      i,
      logoStrip.Color(
        applyBrightness(c.r, logoBrightness),
        applyBrightness(c.g, logoBrightness),
        applyBrightness(c.b, logoBrightness),
        0
      )
    );
#else
    clockStrip.setPixelColor(
      idx,
      clockStrip.Color(
        applyBrightness(c.r, logoBrightness),
        applyBrightness(c.g, logoBrightness),
        applyBrightness(c.b, logoBrightness),
        0
      )
    );
#endif
  }
}
#endif

// Clear LEDs as early as possible during boot to prevent garbage flashes
// This uses a safe maximum LED count since grid layout isn't loaded yet
void earlyLedClear() {
#ifndef PIO_UNIT_TESTING
  // Use a reasonable maximum LED count for initial clear
  // This will be properly reconfigured later by ensureStripLength()
  static const uint16_t EARLY_CLEAR_LED_COUNT = 256;

#if defined(PRODUCT_VARIANT_LOGO)
  clockStrip.updateType(NEO_GRBW + NEO_KHZ800);
  clockStrip.setPin(DATA_PIN);
  clockStrip.updateLength(EARLY_CLEAR_LED_COUNT);
  clockStrip.begin();
  clockStrip.clear();
  clockStrip.show();

#if LOGO_HAS_DEDICATED_PIN
  logoStrip.updateType(NEO_GRBW + NEO_KHZ800);
  logoStrip.setPin(LOGO_DATA_PIN);
  logoStrip.updateLength(EARLY_CLEAR_LED_COUNT);
  logoStrip.begin();
  logoStrip.clear();
  logoStrip.show();
#endif

  // Reset lengths so ensureStripLength() will reconfigure properly
  activeClockStripLength = 0;
#if LOGO_HAS_DEDICATED_PIN
  activeLogoStripLength = 0;
#endif
#else
  strip.updateType(NEO_GRBW + NEO_KHZ800);
  strip.setPin(DATA_PIN);
  strip.updateLength(EARLY_CLEAR_LED_COUNT);
  strip.begin();
  strip.clear();
  strip.show();

  // Reset length so ensureStripLength() will reconfigure properly
  activeStripLength = 0;
#endif
#endif
}

void initLeds() {
#ifndef PIO_UNIT_TESTING
  ensureStripLength();
#if defined(PRODUCT_VARIANT_LOGO)
  clockStrip.setBrightness(255);
  clockStrip.clear();
  clockStrip.show();
#if LOGO_HAS_DEDICATED_PIN
  logoStrip.setBrightness(255);
  logoStrip.clear();
  logoStrip.show();
#endif
#else
  uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
  strip.setBrightness(brightness);
  strip.clear();
  strip.show();
#endif
#else
  lastShown.clear();
#endif
}

void setLedsSuspended(bool suspended) {
#ifndef PIO_UNIT_TESTING
  g_ledsSuspended = suspended;
  if (g_ledsSuspended) {
    ensureStripLength();
#if defined(PRODUCT_VARIANT_LOGO)
    clockStrip.clear();
    clockStrip.setBrightness(0);
    clockStrip.show();
#if LOGO_HAS_DEDICATED_PIN
    logoStrip.clear();
    logoStrip.setBrightness(0);
    logoStrip.show();
#endif
#else
    strip.clear();
    strip.setBrightness(0);
    strip.show();
#endif
  }
#else
  (void)suspended;
#endif
}

void showLeds(const std::vector<uint16_t> &ledIndices) {
#ifndef PIO_UNIT_TESTING
  ensureStripLength();
  if (g_ledsSuspended) {
#if defined(PRODUCT_VARIANT_LOGO)
    clockStrip.clear();
    clockStrip.setBrightness(0);
    clockStrip.show();
#if LOGO_HAS_DEDICATED_PIN
    logoStrip.clear();
    logoStrip.setBrightness(0);
    logoStrip.show();
#endif
#else
    strip.clear();
    strip.setBrightness(0);
    strip.show();
#endif
    return;
  }
#if defined(PRODUCT_VARIANT_LOGO)
  clockStrip.clear();
  uint8_t clockBrightness = nightMode.applyToBrightness(ledState.getBrightness());
  for (uint16_t idx : ledIndices) {
    if (idx < clockStrip.numPixels()) {
      uint8_t r, g, b, w;
      ledState.getRGBW(r, g, b, w);
      clockStrip.setPixelColor(
        idx,
        clockStrip.Color(
          applyBrightness(r, clockBrightness),
          applyBrightness(g, clockBrightness),
          applyBrightness(b, clockBrightness),
          applyBrightness(w, clockBrightness)
        )
      );
    }
  }
  renderLogoLeds();
  clockStrip.setBrightness(255);
#if LOGO_HAS_DEDICATED_PIN
  logoStrip.setBrightness(255);
#endif
#else
  strip.clear();
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
#if defined(PRODUCT_VARIANT_LOGO)
  clockStrip.show();
#if LOGO_HAS_DEDICATED_PIN
  logoStrip.show();
#endif
#else
  strip.show();
#endif
#else
  lastShown = ledIndices;
#endif
}

void showLedsColor(const std::vector<uint16_t> &ledIndices,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
#ifndef PIO_UNIT_TESTING
  ensureStripLength();
  if (g_ledsSuspended) {
#if defined(PRODUCT_VARIANT_LOGO)
    clockStrip.clear();
    clockStrip.setBrightness(0);
    clockStrip.show();
#if LOGO_HAS_DEDICATED_PIN
    logoStrip.clear();
    logoStrip.setBrightness(0);
    logoStrip.show();
#endif
#else
    strip.clear();
    strip.setBrightness(0);
    strip.show();
#endif
    return;
  }
#if defined(PRODUCT_VARIANT_LOGO)
  clockStrip.clear();
  uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
  for (uint16_t idx : ledIndices) {
    if (idx < clockStrip.numPixels()) {
      clockStrip.setPixelColor(
        idx,
        clockStrip.Color(
          applyBrightness(r, brightness),
          applyBrightness(g, brightness),
          applyBrightness(b, brightness),
          applyBrightness(w, brightness)
        )
      );
    }
  }
  renderLogoLeds();
  clockStrip.setBrightness(255);
#if LOGO_HAS_DEDICATED_PIN
  logoStrip.setBrightness(255);
#endif
#else
  strip.clear();
  for (uint16_t idx : ledIndices) {
    if (idx < strip.numPixels()) {
      strip.setPixelColor(idx, strip.Color(r, g, b, w));
    }
  }
  uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
  strip.setBrightness(brightness);
#endif
#if defined(PRODUCT_VARIANT_LOGO)
  clockStrip.show();
#if LOGO_HAS_DEDICATED_PIN
  logoStrip.show();
#endif
#else
  strip.show();
#endif
#else
  (void)ledIndices;
  (void)r;
  (void)g;
  (void)b;
  (void)w;
#endif
}

void setLedsColorOverlay(const std::vector<uint16_t> &ledIndices,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
#ifndef PIO_UNIT_TESTING
  ensureStripLength();
  if (g_ledsSuspended) {
    return;
  }
#if defined(PRODUCT_VARIANT_LOGO)
  uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
  for (uint16_t idx : ledIndices) {
    if (idx < clockStrip.numPixels()) {
      clockStrip.setPixelColor(
        idx,
        clockStrip.Color(
          applyBrightness(r, brightness),
          applyBrightness(g, brightness),
          applyBrightness(b, brightness),
          applyBrightness(w, brightness)
        )
      );
    }
  }
  clockStrip.setBrightness(255);
#if LOGO_HAS_DEDICATED_PIN
  logoStrip.setBrightness(255);
#endif
  clockStrip.show();
#if LOGO_HAS_DEDICATED_PIN
  logoStrip.show();
#endif
#else
  for (uint16_t idx : ledIndices) {
    if (idx < strip.numPixels()) {
      strip.setPixelColor(idx, strip.Color(r, g, b, w));
    }
  }
  uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
  strip.setBrightness(brightness);
  strip.show();
#endif
#else
  (void)ledIndices;
  (void)r;
  (void)g;
  (void)b;
  (void)w;
#endif
}

void showLedsWithBrightness(const std::vector<uint16_t> &ledIndices, 
                            const std::vector<uint8_t> &brightnessMultipliers) {
#ifndef PIO_UNIT_TESTING
  ensureStripLength();
  if (g_ledsSuspended) {
#if defined(PRODUCT_VARIANT_LOGO)
    clockStrip.clear();
    clockStrip.setBrightness(0);
    clockStrip.show();
#if LOGO_HAS_DEDICATED_PIN
    logoStrip.clear();
    logoStrip.setBrightness(0);
    logoStrip.show();
#endif
#else
    strip.clear();
    strip.setBrightness(0);
    strip.show();
#endif
    return;
  }
#if defined(PRODUCT_VARIANT_LOGO)
  clockStrip.clear();
#else
  strip.clear();
#endif
  uint8_t r, g, b, w;
  ledState.getRGBW(r, g, b, w);

  const uint16_t stripCount =
#if defined(PRODUCT_VARIANT_LOGO)
    clockStrip.numPixels();
#else
    strip.numPixels();
#endif

  for (size_t i = 0; i < ledIndices.size() && i < brightnessMultipliers.size(); ++i) {
    uint16_t idx = ledIndices[i];
    if (idx < stripCount) {
      uint8_t multiplier = brightnessMultipliers[i];
      uint8_t finalR = (r * multiplier) / 255;
      uint8_t finalG = (g * multiplier) / 255;
      uint8_t finalB = (b * multiplier) / 255;
      uint8_t finalW = (w * multiplier) / 255;
#if defined(PRODUCT_VARIANT_LOGO)
      uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
      clockStrip.setPixelColor(
        idx,
        clockStrip.Color(
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
  clockStrip.setBrightness(255);
#if LOGO_HAS_DEDICATED_PIN
  logoStrip.setBrightness(255);
#endif
#else
  uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
  strip.setBrightness(brightness);
#endif
#if defined(PRODUCT_VARIANT_LOGO)
  clockStrip.show();
#if LOGO_HAS_DEDICATED_PIN
  logoStrip.show();
#endif
#else
  strip.show();
#endif
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
