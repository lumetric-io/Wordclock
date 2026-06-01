#include "led_controller.h"
#include "config.h"
#include "grid_layout.h"
#include "led_state.h"
#include "night_mode.h"
#if defined(PRODUCT_VARIANT_LOGO)
#include "logo_leds.h"
#endif

#include "led_segments.h"
#include <vector>

#if defined(PRODUCT_VARIANT_LOGO) && defined(LOGO_DATA_PIN)
#define LOGO_HAS_DEDICATED_PIN 1
#else
#define LOGO_HAS_DEDICATED_PIN 0
#endif

// Optional second clock data line. A product opts in by defining BOTH
// CLOCK_DATA_PIN_2 and CLOCK_SEGMENT_SPLIT in its product_config.h; without
// them the clock is driven as a single segment exactly as before.
#if defined(CLOCK_DATA_PIN_2) && defined(CLOCK_SEGMENT_SPLIT)
#define CLOCK_HAS_SPLIT 1
#else
#define CLOCK_HAS_SPLIT 0
#endif

#ifndef PIO_UNIT_TESTING

// ---------------------------------------------------------------------------
// Physical output layer
//
// Upstream code addresses the clock with *logical* LED indices
// (0..getActiveLedCountTotal()-1) and the logo with its own 0-based indices.
// A product maps those logical buffers onto one or more physical data lines
// (GPIOs). buildSegments() turns the product/grid config into a table of
// {pin, source, logicalStart, length}; each entry is driven by its own
// Adafruit_NeoPixel instance. Writing a logical index routes to whichever
// segment carries it. Single-strip and logo products produce a one- or
// two-entry table, so their on-wire behaviour is unchanged.
// ---------------------------------------------------------------------------

static const uint8_t LED_MAX_SEGMENTS = 4;
static Adafruit_NeoPixel g_strips[LED_MAX_SEGMENTS];
static LedSegment g_segments[LED_MAX_SEGMENTS];
static uint8_t g_segmentCount = 0;

// Reconfigure the hardware only when the LED counts actually change.
static bool g_segmentsReady = false;
static uint16_t g_cfgClockTotal = 0xFFFF;
static uint16_t g_cfgLogoCount = 0xFFFF;

static bool g_ledsSuspended = false;

#if defined(PRODUCT_VARIANT_LOGO)
static const uint8_t DIAG_MAX = 4;
static uint16_t g_diagIndices[DIAG_MAX] = {};
static uint8_t g_diagCount = 0;
static uint8_t g_diagR = 0, g_diagG = 0, g_diagB = 0, g_diagW = 0;
#endif

// --- segment plumbing ------------------------------------------------------

static void configureStripsFromSegments() {
  for (uint8_t s = 0; s < g_segmentCount; ++s) {
    Adafruit_NeoPixel& strip = g_strips[s];
    strip.updateType(NEO_GRBW + NEO_KHZ800);
    strip.setPin(g_segments[s].pin);
    // Keep the strip functional even if the layout is missing (length 0).
    strip.updateLength(g_segments[s].length == 0 ? 1 : g_segments[s].length);
    strip.begin();
    strip.clear();
    strip.show();
  }
}

static void ensureSegments() {
  uint16_t clockTotal = getActiveLedCountTotal();
#if defined(PRODUCT_VARIANT_LOGO)
  uint16_t logoCount = getLogoLedCount();
#else
  uint16_t logoCount = 0;
#endif

  if (g_segmentsReady && clockTotal == g_cfgClockTotal &&
      logoCount == g_cfgLogoCount) {
    return;
  }
  g_cfgClockTotal = clockTotal;
  g_cfgLogoCount = logoCount;

  LedSegmentConfig cfg;
  cfg.clockPin = DATA_PIN;
#if CLOCK_HAS_SPLIT
  cfg.clockPin2 = CLOCK_DATA_PIN_2;
  cfg.clockSplit = CLOCK_SEGMENT_SPLIT;
#endif
  cfg.clockTotal = clockTotal;
#if defined(PRODUCT_VARIANT_LOGO)
  cfg.hasLogo = true;
  cfg.logoCount = logoCount;
#if LOGO_HAS_DEDICATED_PIN
  cfg.logoDedicated = true;
  cfg.logoPin = LOGO_DATA_PIN;
#endif
#endif

  std::vector<LedSegment> segs = buildSegments(cfg);
  g_segmentCount = 0;
  for (size_t i = 0; i < segs.size() && g_segmentCount < LED_MAX_SEGMENTS; ++i) {
    g_segments[g_segmentCount++] = segs[i];
  }
  configureStripsFromSegments();
  g_segmentsReady = true;
}

// Route a logical CLOCK index to the strip that carries it (no-op if unmapped).
static inline void clockSetPixel(uint16_t logicalIdx, uint32_t color) {
  for (uint8_t s = 0; s < g_segmentCount; ++s) {
    const LedSegment& seg = g_segments[s];
    if (seg.source == LedBuffer::CLOCK && logicalIdx >= seg.logicalStart &&
        static_cast<uint16_t>(logicalIdx - seg.logicalStart) < seg.length) {
      g_strips[s].setPixelColor(
          static_cast<uint16_t>(logicalIdx - seg.logicalStart), color);
      return;
    }
  }
}

static void clearClockStrips() {
  for (uint8_t s = 0; s < g_segmentCount; ++s) {
    if (g_segments[s].source == LedBuffer::CLOCK) g_strips[s].clear();
  }
}

static void showClockStrips() {
  for (uint8_t s = 0; s < g_segmentCount; ++s) {
    if (g_segments[s].source == LedBuffer::CLOCK) g_strips[s].show();
  }
}

// Apply the per-build brightness policy, then push every segment to the wire.
// Logo builds pre-multiply colours and run the strips at 255 (so clock and logo
// keep independent brightnesses); non-logo builds let the hardware scale raw
// colours via setBrightness() — matching the pre-refactor behaviour exactly.
static void finalizeAndShow(uint8_t clockBrightness) {
#if defined(PRODUCT_VARIANT_LOGO)
  (void)clockBrightness;
#endif
  for (uint8_t s = 0; s < g_segmentCount; ++s) {
#if defined(PRODUCT_VARIANT_LOGO)
    g_strips[s].setBrightness(255);
#else
    g_strips[s].setBrightness(clockBrightness);
#endif
  }
  for (uint8_t s = 0; s < g_segmentCount; ++s) {
    g_strips[s].show();
  }
}

static void showSuspended() {
  for (uint8_t s = 0; s < g_segmentCount; ++s) {
    g_strips[s].clear();
    g_strips[s].setBrightness(0);
    g_strips[s].show();
  }
}

#if defined(PRODUCT_VARIANT_LOGO)
static uint8_t applyBrightness(uint8_t value, uint8_t brightness) {
  return static_cast<uint8_t>((static_cast<uint16_t>(value) * brightness) / 255);
}

// Route a logo index to the dedicated logo segment, or — when the logo shares
// the clock chain — to the tail of the clock buffer.
static inline void logoSetPixel(uint16_t logoIdx, uint32_t color) {
  for (uint8_t s = 0; s < g_segmentCount; ++s) {
    if (g_segments[s].source == LedBuffer::LOGO) {
      if (logoIdx < g_segments[s].length) {
        g_strips[s].setPixelColor(logoIdx, color);
      }
      return;
    }
  }
  // No dedicated logo segment: logo is appended to the clock chain.
  clockSetPixel(static_cast<uint16_t>(getActiveLedCountTotal() + logoIdx), color);
}

static void renderLogoLeds() {
  const LogoLedColor* colors = logoLeds.getColors();
  uint8_t logoBrightness = nightMode.applyToBrightness(logoLeds.getBrightness());
  uint16_t count = getLogoLedCount();
  for (uint16_t i = 0; i < count; ++i) {
    const LogoLedColor& c = colors[i];
    logoSetPixel(i, Adafruit_NeoPixel::Color(applyBrightness(c.r, logoBrightness),
                                             applyBrightness(c.g, logoBrightness),
                                             applyBrightness(c.b, logoBrightness),
                                             0));
  }
}

static void applyDiagOverride() {
  for (uint8_t i = 0; i < g_diagCount; ++i) {
    clockSetPixel(g_diagIndices[i],
                  Adafruit_NeoPixel::Color(g_diagR, g_diagG, g_diagB, g_diagW));
  }
}

void setDiagLedOverride(const uint16_t* indices, uint8_t count, uint8_t r,
                        uint8_t g, uint8_t b, uint8_t w) {
  g_diagCount = count < DIAG_MAX ? count : DIAG_MAX;
  for (uint8_t i = 0; i < g_diagCount; ++i) g_diagIndices[i] = indices[i];
  g_diagR = r; g_diagG = g; g_diagB = b; g_diagW = w;
  if (g_diagCount > 0) {
    for (uint8_t i = 0; i < g_diagCount; ++i) {
      clockSetPixel(g_diagIndices[i], Adafruit_NeoPixel::Color(r, g, b, w));
    }
    showClockStrips();
  }
}

void clearDiagLedOverride() {
  if (g_diagCount > 0) {
    for (uint8_t i = 0; i < g_diagCount; ++i) {
      clockSetPixel(g_diagIndices[i], 0);
    }
    showClockStrips();
  }
  g_diagCount = 0;
}
#endif  // PRODUCT_VARIANT_LOGO

#else   // PIO_UNIT_TESTING
static std::vector<uint16_t> lastShown;
#endif  // PIO_UNIT_TESTING


// Clear LEDs as early as possible during boot to prevent garbage flashes.
// The grid layout isn't loaded yet, so use a safe maximum length and clear
// every data line this product may drive; ensureSegments() reconfigures the
// real lengths later.
void earlyLedClear() {
#ifndef PIO_UNIT_TESTING
  static const uint16_t EARLY_CLEAR_LED_COUNT = 600;

  uint8_t pins[LED_MAX_SEGMENTS];
  uint8_t pinCount = 0;
  pins[pinCount++] = DATA_PIN;
#if CLOCK_HAS_SPLIT
  pins[pinCount++] = CLOCK_DATA_PIN_2;
#endif
#if defined(PRODUCT_VARIANT_LOGO) && LOGO_HAS_DEDICATED_PIN
  pins[pinCount++] = LOGO_DATA_PIN;
#endif

  for (uint8_t i = 0; i < pinCount; ++i) {
    g_strips[i].updateType(NEO_GRBW + NEO_KHZ800);
    g_strips[i].setPin(pins[i]);
    g_strips[i].updateLength(EARLY_CLEAR_LED_COUNT);
    g_strips[i].begin();
    g_strips[i].clear();
    g_strips[i].show();
  }

  // Force ensureSegments() to reconfigure with the real layout next call.
  g_segmentsReady = false;
  g_cfgClockTotal = 0xFFFF;
  g_cfgLogoCount = 0xFFFF;
#endif
}

void initLeds() {
#ifndef PIO_UNIT_TESTING
  ensureSegments();
#if defined(PRODUCT_VARIANT_LOGO)
  for (uint8_t s = 0; s < g_segmentCount; ++s) {
    g_strips[s].setBrightness(255);
    g_strips[s].clear();
    g_strips[s].show();
  }
#else
  uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
  for (uint8_t s = 0; s < g_segmentCount; ++s) {
    g_strips[s].setBrightness(brightness);
    g_strips[s].clear();
    g_strips[s].show();
  }
#endif
#else
  lastShown.clear();
#endif
}

void setLedsSuspended(bool suspended) {
#ifndef PIO_UNIT_TESTING
  g_ledsSuspended = suspended;
  if (g_ledsSuspended) {
    ensureSegments();
    showSuspended();
  }
#else
  (void)suspended;
#endif
}

void showLeds(const std::vector<uint16_t> &ledIndices) {
#ifndef PIO_UNIT_TESTING
  ensureSegments();
  if (g_ledsSuspended) {
    showSuspended();
    return;
  }
  clearClockStrips();
  uint8_t clockBrightness = nightMode.applyToBrightness(ledState.getBrightness());
  uint8_t r, g, b, w;
  ledState.getRGBW(r, g, b, w);
  for (uint16_t idx : ledIndices) {
#if defined(PRODUCT_VARIANT_LOGO)
    clockSetPixel(idx,
                  Adafruit_NeoPixel::Color(applyBrightness(r, clockBrightness),
                                           applyBrightness(g, clockBrightness),
                                           applyBrightness(b, clockBrightness),
                                           applyBrightness(w, clockBrightness)));
#else
    clockSetPixel(idx, Adafruit_NeoPixel::Color(r, g, b, w));
#endif
  }
#if defined(PRODUCT_VARIANT_LOGO)
  renderLogoLeds();
  applyDiagOverride();
#endif
  finalizeAndShow(clockBrightness);
#else
  lastShown = ledIndices;
#endif
}

void showLedsColor(const std::vector<uint16_t> &ledIndices,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
#ifndef PIO_UNIT_TESTING
  ensureSegments();
  if (g_ledsSuspended) {
    showSuspended();
    return;
  }
  clearClockStrips();
  uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
  for (uint16_t idx : ledIndices) {
#if defined(PRODUCT_VARIANT_LOGO)
    clockSetPixel(idx, Adafruit_NeoPixel::Color(applyBrightness(r, brightness),
                                                applyBrightness(g, brightness),
                                                applyBrightness(b, brightness),
                                                applyBrightness(w, brightness)));
#else
    clockSetPixel(idx, Adafruit_NeoPixel::Color(r, g, b, w));
#endif
  }
#if defined(PRODUCT_VARIANT_LOGO)
  renderLogoLeds();
  applyDiagOverride();
#endif
  finalizeAndShow(brightness);
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
  ensureSegments();
  if (g_ledsSuspended) {
    return;
  }
  uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
  for (uint16_t idx : ledIndices) {
#if defined(PRODUCT_VARIANT_LOGO)
    clockSetPixel(idx, Adafruit_NeoPixel::Color(applyBrightness(r, brightness),
                                                applyBrightness(g, brightness),
                                                applyBrightness(b, brightness),
                                                applyBrightness(w, brightness)));
#else
    clockSetPixel(idx, Adafruit_NeoPixel::Color(r, g, b, w));
#endif
  }
  finalizeAndShow(brightness);
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
  ensureSegments();
  if (g_ledsSuspended) {
    showSuspended();
    return;
  }
  clearClockStrips();
  uint8_t r, g, b, w;
  ledState.getRGBW(r, g, b, w);
  uint8_t brightness = nightMode.applyToBrightness(ledState.getBrightness());
  for (size_t i = 0; i < ledIndices.size() && i < brightnessMultipliers.size(); ++i) {
    uint16_t idx = ledIndices[i];
    uint8_t multiplier = brightnessMultipliers[i];
    uint8_t finalR = (r * multiplier) / 255;
    uint8_t finalG = (g * multiplier) / 255;
    uint8_t finalB = (b * multiplier) / 255;
    uint8_t finalW = (w * multiplier) / 255;
#if defined(PRODUCT_VARIANT_LOGO)
    clockSetPixel(idx,
                  Adafruit_NeoPixel::Color(applyBrightness(finalR, brightness),
                                           applyBrightness(finalG, brightness),
                                           applyBrightness(finalB, brightness),
                                           applyBrightness(finalW, brightness)));
#else
    clockSetPixel(idx, Adafruit_NeoPixel::Color(finalR, finalG, finalB, finalW));
#endif
  }
#if defined(PRODUCT_VARIANT_LOGO)
  renderLogoLeds();
  applyDiagOverride();
#endif
  finalizeAndShow(brightness);
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
