#include <gtest/gtest.h>

// Pure, hardware-free module — include the source directly (same pattern as the
// other native suites).
#include "../../src/led_segments.cpp"

namespace {

uint32_t sumLengths(const std::vector<LedSegment>& segs) {
  uint32_t total = 0;
  for (const auto& s : segs) total += s.length;
  return total;
}

}  // namespace

// A plain (non-logo) product: one clock data line, one segment.
TEST(LedSegments, NonLogoSingleStrip) {
  LedSegmentConfig cfg;
  cfg.clockPin = 4;
  cfg.clockTotal = 115;

  auto segs = buildSegments(cfg);
  ASSERT_EQ(1u, segs.size());
  EXPECT_EQ(4, segs[0].pin);
  EXPECT_EQ(LedBuffer::CLOCK, segs[0].source);
  EXPECT_EQ(0, segs[0].logicalStart);
  EXPECT_EQ(115, segs[0].length);
}

// Today's logo product (e.g. 55x50 / 100x100 before the split): clock on one
// pin, logo on its own pin.
TEST(LedSegments, LogoDedicatedNoSplit) {
  LedSegmentConfig cfg;
  cfg.clockPin = 4;
  cfg.clockTotal = 537;
  cfg.hasLogo = true;
  cfg.logoDedicated = true;
  cfg.logoPin = 18;
  cfg.logoCount = 52;

  auto segs = buildSegments(cfg);
  ASSERT_EQ(2u, segs.size());

  EXPECT_EQ(4, segs[0].pin);
  EXPECT_EQ(LedBuffer::CLOCK, segs[0].source);
  EXPECT_EQ(0, segs[0].logicalStart);
  EXPECT_EQ(537, segs[0].length);

  EXPECT_EQ(18, segs[1].pin);
  EXPECT_EQ(LedBuffer::LOGO, segs[1].source);
  EXPECT_EQ(0, segs[1].logicalStart);
  EXPECT_EQ(52, segs[1].length);
}

// Fase 2 target for nextgen-logo-100x100: clock split after index 243 (LED 244
// begins segment B) on GPIO 6, logo still on GPIO 18.
TEST(LedSegments, LogoDedicatedWithSplit_100x100) {
  LedSegmentConfig cfg;
  cfg.clockPin = 4;
  cfg.clockPin2 = 6;
  cfg.clockSplit = 244;
  cfg.clockTotal = 537;
  cfg.hasLogo = true;
  cfg.logoDedicated = true;
  cfg.logoPin = 18;
  cfg.logoCount = 52;

  auto segs = buildSegments(cfg);
  ASSERT_EQ(3u, segs.size());

  // Segment A: GPIO 4, logical 0..243 (244 LEDs)
  EXPECT_EQ(4, segs[0].pin);
  EXPECT_EQ(LedBuffer::CLOCK, segs[0].source);
  EXPECT_EQ(0, segs[0].logicalStart);
  EXPECT_EQ(244, segs[0].length);

  // Segment B: GPIO 6, logical 244..536 (293 LEDs)
  EXPECT_EQ(6, segs[1].pin);
  EXPECT_EQ(LedBuffer::CLOCK, segs[1].source);
  EXPECT_EQ(244, segs[1].logicalStart);
  EXPECT_EQ(293, segs[1].length);

  // Logo: GPIO 18, own buffer
  EXPECT_EQ(18, segs[2].pin);
  EXPECT_EQ(LedBuffer::LOGO, segs[2].source);
  EXPECT_EQ(0, segs[2].logicalStart);
  EXPECT_EQ(52, segs[2].length);

  // Clock segments must cover [0, 537) contiguously: no gap, no overlap.
  EXPECT_EQ(segs[0].length, segs[1].logicalStart);
  EXPECT_EQ(537, segs[0].length + segs[1].length);
}

// Legacy/fallback: logo appended to the clock chain (no dedicated pin).
TEST(LedSegments, AppendedLogoSharesClockChain) {
  LedSegmentConfig cfg;
  cfg.clockPin = 4;
  cfg.clockTotal = 100;
  cfg.hasLogo = true;
  cfg.logoDedicated = false;
  cfg.logoCount = 10;

  auto segs = buildSegments(cfg);
  ASSERT_EQ(1u, segs.size());
  EXPECT_EQ(4, segs[0].pin);
  EXPECT_EQ(LedBuffer::CLOCK, segs[0].source);
  EXPECT_EQ(110, segs[0].length);  // clock + appended logo
}

// A split index is meaningless without a second pin — fall back to one segment.
TEST(LedSegments, SplitIgnoredWithoutSecondPin) {
  LedSegmentConfig cfg;
  cfg.clockPin = 4;
  cfg.clockPin2 = 0;
  cfg.clockSplit = 244;
  cfg.clockTotal = 537;

  auto segs = buildSegments(cfg);
  ASSERT_EQ(1u, segs.size());
  EXPECT_EQ(537, segs[0].length);
}

// Degenerate split indices collapse to a single segment.
TEST(LedSegments, SplitIgnoredWhenOutOfRange) {
  LedSegmentConfig cfg;
  cfg.clockPin = 4;
  cfg.clockPin2 = 6;
  cfg.clockTotal = 537;

  cfg.clockSplit = 0;  // 0 => no split
  EXPECT_EQ(1u, buildSegments(cfg).size());

  cfg.clockSplit = 537;  // == total => nothing on the second line
  EXPECT_EQ(1u, buildSegments(cfg).size());

  cfg.clockSplit = 600;  // > total
  EXPECT_EQ(1u, buildSegments(cfg).size());
}

// Edge (not a shipping combo): split + appended logo cuts the combined chain
// and still covers every LED exactly once.
TEST(LedSegments, SplitWithAppendedLogoCutsCombinedChain) {
  LedSegmentConfig cfg;
  cfg.clockPin = 4;
  cfg.clockPin2 = 6;
  cfg.clockSplit = 60;
  cfg.clockTotal = 100;
  cfg.hasLogo = true;
  cfg.logoDedicated = false;
  cfg.logoCount = 10;

  auto segs = buildSegments(cfg);
  ASSERT_EQ(2u, segs.size());
  EXPECT_EQ(60, segs[0].length);
  EXPECT_EQ(60, segs[1].logicalStart);
  EXPECT_EQ(50, segs[1].length);  // (100 + 10) - 60
  EXPECT_EQ(110u, sumLengths(segs));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
