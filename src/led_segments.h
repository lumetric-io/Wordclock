#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

// Which logical framebuffer a physical segment draws from.
enum class LedBuffer : uint8_t { CLOCK, LOGO };

// One physical LED data line (a single GPIO) carrying a contiguous slice of a
// logical buffer. Upstream code addresses LEDs by *logical* index; a segment
// maps logical indices [logicalStart, logicalStart + length) of `source` onto
// the physical pixels 0..length-1 of one Adafruit_NeoPixel instance.
struct LedSegment {
  uint8_t pin = 0;
  LedBuffer source = LedBuffer::CLOCK;
  uint16_t logicalStart = 0;
  uint16_t length = 0;
};

// Everything buildSegments() needs to lay out a product's physical data lines.
// Pins live in product_config.h (board wiring); LED counts come from the active
// grid variant. Absence of a second clock pin / split yields a single clock
// segment, so single-strip and logo products are unaffected.
struct LedSegmentConfig {
  uint8_t clockPin = 0;          // primary clock data line
  uint8_t clockPin2 = 0;         // optional 2nd clock data line (0 = none)
  uint16_t clockSplit = 0;       // # of clock LEDs on clockPin; rest on clockPin2 (0 = no split)
  uint16_t clockTotal = 0;       // total logical clock LEDs (grid + extra)
  bool hasLogo = false;
  bool logoDedicated = false;    // logo on its own pin vs. appended to the clock chain
  uint8_t logoPin = 0;
  uint16_t logoCount = 0;
};

// Pure (hardware-free) layout: turn a config into the ordered physical segment
// table. Order is clock segment(s) in chain order, then the dedicated logo
// segment if any. A split is honoured only when a second pin is set and the
// split index is strictly inside the clock chain; otherwise the clock is one
// segment. When the logo has no dedicated pin it is appended to the clock
// chain (the chain length grows by logoCount).
std::vector<LedSegment> buildSegments(const LedSegmentConfig& cfg);
