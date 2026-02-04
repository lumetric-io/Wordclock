#include "logo_leds.h"
#include "grid_variants/nl_100x100_logo_v1.h"
#include "grid_variants/nl_55x50_logo_v1.h"

#if defined(PRODUCT_VARIANT_LOGO)

LogoLeds logoLeds;

void LogoLeds::begin() {
  prefs.begin("logo", false);
  brightness = prefs.getUChar("br", 64);
  size_t read = prefs.getBytes("clr", colors, sizeof(colors));
  prefs.end();
  const uint16_t expectedCount = getLogoLedCount();
  const size_t expectedBytes = sizeof(LogoLedColor) * expectedCount;
  const size_t storageBytes = sizeof(LogoLedColor) * LOGO_LED_STORAGE_COUNT;
  if (read != expectedBytes && read != storageBytes) {
    read = 0;
  }
  size_t slotsRead = read / sizeof(LogoLedColor);
  for (size_t i = slotsRead; i < LOGO_LED_STORAGE_COUNT; ++i) {
    colors[i] = {};
  }
}

void LogoLeds::setBrightness(uint8_t b) {
  if (brightness == b) return;
  brightness = b;
  persistBrightness();
}

bool LogoLeds::setColor(uint16_t index, uint8_t r, uint8_t g, uint8_t b, bool persist) {
  if (index >= getLogoLedCount()) return false;
  colors[index].r = r;
  colors[index].g = g;
  colors[index].b = b;
  if (persist) {
    persistColors();
  }
  return true;
}

void LogoLeds::setAll(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t count = getLogoLedCount();
  for (uint16_t i = 0; i < count; ++i) {
    auto& c = colors[i];
    c.r = r;
    c.g = g;
    c.b = b;
  }
  persistColors();
}

LogoLedColor LogoLeds::getColor(uint16_t index) const {
  if (index >= getLogoLedCount()) return {};
  return colors[index];
}

void LogoLeds::flushColors() {
  persistColors();
}

void LogoLeds::persistBrightness() {
  prefs.begin("logo", false);
  prefs.putUChar("br", brightness);
  prefs.end();
}

void LogoLeds::persistColors() {
  prefs.begin("logo", false);
  uint16_t count = getLogoLedCount();
  prefs.putBytes("clr", colors, sizeof(LogoLedColor) * count);
  prefs.end();
}

uint16_t getLogoStartIndex() {
  return 0;
}

uint16_t getTotalStripLength() {
  return static_cast<uint16_t>(getActiveLedCountTotal() + getLogoLedCount());
}

uint16_t getLogoLedCount() {
  switch (getActiveGridVariant()) {
    case GridVariant::NL_100x100_LOGO_V1:
      return nl_100x100_logo_v1::LOGO_LED_COUNT;
    case GridVariant::NL_55x50_LOGO_V1:
      return nl_55x50_logo_v1::LOGO_LED_COUNT;
    default:
      return nl_55x50_logo_v1::LOGO_LED_COUNT;
  }
}

#endif
