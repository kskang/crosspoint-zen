#include <gtest/gtest.h>

#include "src/activities/reader/ReaderOrientation.h"

namespace {

using ReaderOrientation::FLIP_LANDSCAPE;
using ReaderOrientation::FLIP_PORTRAIT;
using ReaderOrientation::INVERTED;
using ReaderOrientation::LANDSCAPE_CCW;
using ReaderOrientation::LANDSCAPE_CW;
using ReaderOrientation::next;
using ReaderOrientation::PORTRAIT;
using ReaderOrientation::ROTATE_90;

TEST(ReaderOrientation, Rotate90CyclesThroughAllFourInQuarterTurns) {
  EXPECT_EQ(next(ROTATE_90, PORTRAIT), LANDSCAPE_CW);
  EXPECT_EQ(next(ROTATE_90, LANDSCAPE_CW), INVERTED);
  EXPECT_EQ(next(ROTATE_90, INVERTED), LANDSCAPE_CCW);
  EXPECT_EQ(next(ROTATE_90, LANDSCAPE_CCW), PORTRAIT);
}

TEST(ReaderOrientation, FlipPortraitTogglesWithinThePortraitPair) {
  EXPECT_EQ(next(FLIP_PORTRAIT, PORTRAIT), INVERTED);
  EXPECT_EQ(next(FLIP_PORTRAIT, INVERTED), PORTRAIT);
}

TEST(ReaderOrientation, FlipPortraitEntersPortraitFromEitherLandscape) {
  EXPECT_EQ(next(FLIP_PORTRAIT, LANDSCAPE_CW), PORTRAIT);
  EXPECT_EQ(next(FLIP_PORTRAIT, LANDSCAPE_CCW), PORTRAIT);
}

TEST(ReaderOrientation, FlipLandscapeTogglesWithinTheLandscapePair) {
  EXPECT_EQ(next(FLIP_LANDSCAPE, LANDSCAPE_CW), LANDSCAPE_CCW);
  EXPECT_EQ(next(FLIP_LANDSCAPE, LANDSCAPE_CCW), LANDSCAPE_CW);
}

TEST(ReaderOrientation, FlipLandscapeEntersClockwiseFromEitherPortrait) {
  EXPECT_EQ(next(FLIP_LANDSCAPE, PORTRAIT), LANDSCAPE_CW);
  EXPECT_EQ(next(FLIP_LANDSCAPE, INVERTED), LANDSCAPE_CW);
}

// A corrupt or cross-firmware settings byte must not produce an out-of-range
// orientation that would index past the renderer's orientation table.
TEST(ReaderOrientation, OutOfRangeCurrentOrientationStaysInRange) {
  for (uint8_t function = ROTATE_90; function <= FLIP_LANDSCAPE; ++function) {
    for (uint8_t current = 4; current < 255; ++current) {
      EXPECT_LT(next(function, current), 4) << "function=" << int(function) << " current=" << int(current);
    }
  }
}

TEST(ReaderOrientation, UnknownFunctionLeavesOrientationUnchanged) {
  for (uint8_t current = PORTRAIT; current <= LANDSCAPE_CCW; ++current) {
    EXPECT_EQ(next(/*function=*/0, current), current);
    EXPECT_EQ(next(/*function=*/99, current), current);
  }
}

}  // namespace
