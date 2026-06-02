#include "led_segments.h"

std::vector<LedSegment> buildSegments(const LedSegmentConfig& cfg) {
  std::vector<LedSegment> segments;

  // When there is no dedicated logo pin the logo rides on the tail of the
  // clock chain, so the chain is that much longer.
  uint16_t clockChainLen = cfg.clockTotal;
  if (cfg.hasLogo && !cfg.logoDedicated) {
    clockChainLen = static_cast<uint16_t>(clockChainLen + cfg.logoCount);
  }

  const bool split = cfg.clockPin2 != 0 && cfg.clockSplit > 0 &&
                     cfg.clockSplit < clockChainLen;

  if (split) {
    LedSegment a;
    a.pin = cfg.clockPin;
    a.source = LedBuffer::CLOCK;
    a.logicalStart = 0;
    a.length = cfg.clockSplit;
    segments.push_back(a);

    LedSegment b;
    b.pin = cfg.clockPin2;
    b.source = LedBuffer::CLOCK;
    b.logicalStart = cfg.clockSplit;
    b.length = static_cast<uint16_t>(clockChainLen - cfg.clockSplit);
    segments.push_back(b);
  } else {
    LedSegment a;
    a.pin = cfg.clockPin;
    a.source = LedBuffer::CLOCK;
    a.logicalStart = 0;
    a.length = clockChainLen;
    segments.push_back(a);
  }

  if (cfg.hasLogo && cfg.logoDedicated) {
    LedSegment logo;
    logo.pin = cfg.logoPin;
    logo.source = LedBuffer::LOGO;
    logo.logicalStart = 0;
    logo.length = cfg.logoCount;
    segments.push_back(logo);
  }

  return segments;
}
