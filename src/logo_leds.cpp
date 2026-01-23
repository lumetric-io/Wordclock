#include "logo_leds.h"

#if defined(PRODUCT_VARIANT_LOGO)

LogoLeds logoLeds;

void LogoLeds::begin() {
  prefs.begin("logo", false);
  brightness = prefs.getUChar("br", 64);
  size_t read = prefs.getBytes("clr", colors, sizeof(colors));
  prefs.end();
  if (read != sizeof(colors)) {
    for (auto& c : colors) {
      c = {};
    }
  }
}

void LogoLeds::setBrightness(uint8_t b) {
  if (brightness == b) return;
  brightness = b;
  persistBrightness();
}

bool LogoLeds::setColor(uint16_t index, uint8_t r, uint8_t g, uint8_t b, bool persist) {
  if (index >= LOGO_LED_COUNT) return false;
  colors[index].r = r;
  colors[index].g = g;
  colors[index].b = b;
  if (persist) {
    persistColors();
  }
  return true;
}

void LogoLeds::setAll(uint8_t r, uint8_t g, uint8_t b) {
  for (auto& c : colors) {
    c.r = r;
    c.g = g;
    c.b = b;
  }
  persistColors();
}

LogoLedColor LogoLeds::getColor(uint16_t index) const {
  if (index >= LOGO_LED_COUNT) return {};
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
  prefs.putBytes("clr", colors, sizeof(colors));
  prefs.end();
}

uint16_t getLogoStartIndex() {
  return getActiveLedCountTotal();
}

uint16_t getTotalStripLength() {
  return static_cast<uint16_t>(getActiveLedCountTotal() + LOGO_LED_COUNT);
}

#endif
