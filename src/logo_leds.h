#pragma once

#if defined(PRODUCT_VARIANT_LOGO)

#include <Arduino.h>
#include <Preferences.h>

#include "grid_layout.h"

constexpr uint16_t LOGO_LED_COUNT = 50;

struct LogoLedColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

class LogoLeds {
public:
  void begin();

  void setBrightness(uint8_t b);
  uint8_t getBrightness() const { return brightness; }

  bool setColor(uint16_t index, uint8_t r, uint8_t g, uint8_t b, bool persist = true);
  void setAll(uint8_t r, uint8_t g, uint8_t b);
  void flushColors();

  LogoLedColor getColor(uint16_t index) const;
  const LogoLedColor* getColors() const { return colors; }

private:
  void persistBrightness();
  void persistColors();

  LogoLedColor colors[LOGO_LED_COUNT];
  uint8_t brightness = 64;
  Preferences prefs;
};

extern LogoLeds logoLeds;

uint16_t getLogoStartIndex();
uint16_t getTotalStripLength();

#endif
