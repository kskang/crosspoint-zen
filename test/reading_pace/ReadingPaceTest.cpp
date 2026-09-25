#include <gtest/gtest.h>

#include <cstdint>

#include "src/ReadingStats.h"

namespace {

constexpr uint8_t WINDOW = BookReadingStats::PACE_WINDOW;

void record(BookReadingStats& stats, const int pages, const uint32_t seconds) {
  for (int i = 0; i < pages; i++) stats.recordForwardPageRead(seconds);
}

TEST(ReadingPace, IgnoresFrontMatterSkimmedInSeconds) {
  BookReadingStats stats;
  record(stats, 12, 3);
  record(stats, 20, 30);

  EXPECT_EQ(stats.secondsPerForwardPage(), 30.0f);
}

TEST(ReadingPace, IgnoresPagesLeftOnScreenForMinutes) {
  BookReadingStats stats;
  for (int page = 1; page <= 200; page++) stats.recordForwardPageRead(page % 5 == 0 ? 150 : 30);

  EXPECT_EQ(stats.secondsPerForwardPage(), 30.0f);
}

TEST(ReadingPace, FollowsARealSpeedChangeWithinHalfTheWindow) {
  BookReadingStats stats;
  record(stats, 500, 20);
  record(stats, WINDOW / 2 + 1, 10);

  EXPECT_EQ(stats.secondsPerForwardPage(), 10.0f);
}

TEST(ReadingPace, AveragesTheTwoMiddleSamplesWhileTheWindowFills) {
  BookReadingStats stats;
  stats.recordForwardPageRead(10);
  stats.recordForwardPageRead(40);
  stats.recordForwardPageRead(20);
  stats.recordForwardPageRead(30);

  EXPECT_EQ(stats.secondsPerForwardPage(), 25.0f);
}

TEST(ReadingPace, KeepsOnlyTheLatestWindow) {
  BookReadingStats stats;
  record(stats, 5000, 15);

  EXPECT_EQ(stats.paceSampleCount, WINDOW);
  EXPECT_EQ(stats.secondsPerForwardPage(), 15.0f);
}

TEST(ReadingPace, HasNoPaceWithoutSamples) {
  BookReadingStats stats;
  stats.recordForwardPageRead(0);

  EXPECT_EQ(stats.paceSampleCount, 0);
  EXPECT_EQ(stats.secondsPerForwardPage(), 0.0f);
}

TEST(ReadingPace, ClampsSamplesToTheStoredRange) {
  BookReadingStats stats;
  stats.recordForwardPageRead(UINT32_MAX);

  EXPECT_EQ(stats.secondsPerForwardPage(), static_cast<float>(UINT16_MAX));
}

}  // namespace
