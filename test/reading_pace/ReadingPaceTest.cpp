#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "src/ReadingStats.h"

namespace {

// ReaderActivity::trackPageTurn only samples forward turns that stayed between
// the minimum page dwell and the reading idle cutoff.
constexpr uint32_t MIN_SAMPLE_SECONDS = 2;
constexpr uint32_t MAX_SAMPLE_SECONDS = 300;

BookReadingStats replay(const std::vector<uint32_t>& samples) {
  BookReadingStats stats;
  for (const uint32_t sample : samples) stats.recordForwardPageRead(sample);
  return stats;
}

double trueMean(const std::vector<uint32_t>& samples) {
  double sum = 0.0;
  for (const uint32_t sample : samples) sum += sample;
  return sum / static_cast<double>(samples.size());
}

// Ordinary reading that drifts between 12 and 30 seconds a page.
std::vector<uint32_t> ordinaryReading(const size_t pages) {
  std::vector<uint32_t> samples;
  samples.reserve(pages);
  for (size_t i = 0; i < pages; i++) samples.push_back(12 + static_cast<uint32_t>((i * 7) % 19));
  return samples;
}

TEST(ReadingPace, TracksTheTrueMeanOfOrdinaryReading) {
  const std::vector<uint32_t> samples = ordinaryReading(800);
  const BookReadingStats stats = replay(samples);

  EXPECT_NEAR(stats.secondsPerForwardPage(), trueMean(samples), 0.5);
}

// Regression: every sample below the average used to knock a full second off it,
// draining the pace down to the minimum page dwell with no way back up.
TEST(ReadingPace, DoesNotDecayWhenSamplesSitJustBelowTheAverage) {
  BookReadingStats stats;
  for (int i = 0; i < 400; i++) stats.recordForwardPageRead(20);
  ASSERT_NEAR(stats.secondsPerForwardPage(), 20.0, 0.01);

  for (int i = 0; i < 400; i++) stats.recordForwardPageRead(19);

  EXPECT_GT(stats.secondsPerForwardPage(), 19.0);
}

TEST(ReadingPace, FastPagesMoveTheAverageOnlyByTheirShare) {
  std::vector<uint32_t> samples;
  samples.reserve(800);
  for (size_t i = 0; i < 800; i++) samples.push_back(i % 10 == 0 ? 2 : 20);
  const BookReadingStats stats = replay(samples);

  // 10% at 2s and 90% at 20s averages 18.2s.
  EXPECT_NEAR(stats.secondsPerForwardPage(), 18.2, 0.5);
}

TEST(ReadingPace, RecoversAfterABurstOfFastPages) {
  BookReadingStats stats;
  for (int i = 0; i < 300; i++) stats.recordForwardPageRead(18);
  for (int i = 0; i < 20; i++) stats.recordForwardPageRead(3);
  const double afterBurst = stats.secondsPerForwardPage();
  ASSERT_LT(afterBurst, 18.0);

  for (int i = 0; i < 100; i++) stats.recordForwardPageRead(20);

  EXPECT_GT(stats.secondsPerForwardPage(), afterBurst);
}

TEST(ReadingPace, HalvingAtTheSampleCapPreservesTheAverage) {
  BookReadingStats stats;
  for (uint16_t i = 0; i < BookReadingStats::MAX_PACE_SAMPLES; i++) stats.recordForwardPageRead(24);
  ASSERT_EQ(stats.paceSampleCount, BookReadingStats::MAX_PACE_SAMPLES);
  const double beforeCap = stats.secondsPerForwardPage();

  stats.recordForwardPageRead(24);

  EXPECT_LE(stats.paceSampleCount, BookReadingStats::MAX_PACE_SAMPLES);
  EXPECT_NEAR(stats.secondsPerForwardPage(), beforeCap, 0.1);
}

TEST(ReadingPace, SampleCountNeverExceedsTheCap) {
  BookReadingStats stats;
  for (int i = 0; i < 5000; i++) stats.recordForwardPageRead(15);

  EXPECT_LE(stats.paceSampleCount, BookReadingStats::MAX_PACE_SAMPLES);
  EXPECT_NEAR(stats.secondsPerForwardPage(), 15.0, 0.5);
}

// Only the rounded average and the sample count reach the stats file, so the
// pace has to survive being rebuilt from those two numbers.
TEST(ReadingPace, SurvivesSaveAndLoadRoundTrips) {
  const std::vector<uint32_t> samples = ordinaryReading(200);
  BookReadingStats stats = replay(samples);
  const double beforeReload = stats.secondsPerForwardPage();

  for (int reload = 0; reload < 20; reload++) {
    BookReadingStats reloaded;
    reloaded.avgSecondsPerForwardPage = stats.avgSecondsPerForwardPage;
    reloaded.paceSampleCount = stats.paceSampleCount;
    reloaded.seedPaceTotalFromStoredAverage();
    stats = reloaded;
  }

  EXPECT_NEAR(stats.secondsPerForwardPage(), beforeReload, 0.5);
}

TEST(ReadingPace, KeepsTrackingAfterAReload) {
  BookReadingStats stats;
  for (int i = 0; i < 100; i++) stats.recordForwardPageRead(20);

  BookReadingStats reloaded;
  reloaded.avgSecondsPerForwardPage = stats.avgSecondsPerForwardPage;
  reloaded.paceSampleCount = stats.paceSampleCount;
  reloaded.seedPaceTotalFromStoredAverage();

  for (int i = 0; i < 100; i++) reloaded.recordForwardPageRead(10);

  // 100 pages at 20s followed by 100 at 10s averages 15s.
  EXPECT_NEAR(reloaded.secondsPerForwardPage(), 15.0, 0.5);
}

TEST(ReadingPace, NoSamplesMeansNoPace) {
  const BookReadingStats stats;

  EXPECT_EQ(stats.paceSampleCount, 0);
  EXPECT_EQ(stats.secondsPerForwardPage(), 0.0f);
}

TEST(ReadingPace, ZeroLengthSamplesAreIgnored) {
  BookReadingStats stats;
  stats.recordForwardPageRead(0);

  EXPECT_EQ(stats.paceSampleCount, 0);
  EXPECT_EQ(stats.secondsPerForwardPage(), 0.0f);
}

// A recent stretch close to the running average is ordinary variation, not a
// change of speed.
TEST(ReadingPace, SmallDriftDoesNotRestartThePace) {
  BookReadingStats stats;
  for (int i = 0; i < 300; i++) stats.recordForwardPageRead(20);

  stats.restartPaceIfRecentDiverged(18 * 50, 50);

  EXPECT_EQ(stats.paceSampleCount, 300);
  EXPECT_NEAR(stats.secondsPerForwardPage(), 20.0, 0.5);
}

TEST(ReadingPace, RestartsWhenTheRecentStretchIsMuchFaster) {
  BookReadingStats stats;
  for (int i = 0; i < 300; i++) stats.recordForwardPageRead(20);

  stats.restartPaceIfRecentDiverged(10 * 50, 50);

  EXPECT_EQ(stats.paceSampleCount, 50);
  EXPECT_NEAR(stats.secondsPerForwardPage(), 10.0, 0.5);
}

TEST(ReadingPace, RestartsWhenTheRecentStretchIsMuchSlower) {
  BookReadingStats stats;
  for (int i = 0; i < 300; i++) stats.recordForwardPageRead(10);

  stats.restartPaceIfRecentDiverged(20 * 50, 50);

  EXPECT_EQ(stats.paceSampleCount, 50);
  EXPECT_NEAR(stats.secondsPerForwardPage(), 20.0, 0.5);
}

TEST(ReadingPace, RestartLeavesAnEmptyWindowAlone) {
  BookReadingStats stats;
  for (int i = 0; i < 100; i++) stats.recordForwardPageRead(20);

  stats.restartPaceIfRecentDiverged(0, 0);

  EXPECT_EQ(stats.paceSampleCount, 100);
  EXPECT_NEAR(stats.secondsPerForwardPage(), 20.0, 0.5);
}

// Regression: the reader saves on pause and reloads on resume, and the file only
// carries a rounded average. Without the restart, a sample that moves the average
// by less than half a second is erased by every reload, so the pace can freeze at
// a stale value no matter how much the reader speeds up.
TEST(ReadingPace, FollowsARealSpeedChangeAcrossSaveAndLoadCycles) {
  constexpr uint16_t RECENT_WINDOW = 50;
  BookReadingStats stats;
  for (int i = 0; i < 500; i++) stats.recordForwardPageRead(20);

  uint32_t recentSeconds = 0;
  uint16_t recentSamples = 0;
  for (int page = 1; page <= 300; page++) {
    stats.recordForwardPageRead(10);
    recentSeconds += 10;
    recentSamples++;
    if (recentSamples == RECENT_WINDOW) {
      stats.restartPaceIfRecentDiverged(recentSeconds, recentSamples);
      recentSeconds = 0;
      recentSamples = 0;
    }
    if (page % 15 == 0) {
      // Opening a menu pauses and resumes the reader, which saves and reloads.
      BookReadingStats reloaded;
      reloaded.avgSecondsPerForwardPage = stats.avgSecondsPerForwardPage;
      reloaded.paceSampleCount = stats.paceSampleCount;
      reloaded.seedPaceTotalFromStoredAverage();
      stats = reloaded;
    }
  }

  EXPECT_NEAR(stats.secondsPerForwardPage(), 10.0, 1.0);
}

TEST(ReadingPace, HandlesTheFullSampleRange) {
  BookReadingStats shortest;
  for (int i = 0; i < 1200; i++) shortest.recordForwardPageRead(MIN_SAMPLE_SECONDS);
  EXPECT_NEAR(shortest.secondsPerForwardPage(), static_cast<double>(MIN_SAMPLE_SECONDS), 0.5);

  BookReadingStats longest;
  for (int i = 0; i < 1200; i++) longest.recordForwardPageRead(MAX_SAMPLE_SECONDS);
  EXPECT_NEAR(longest.secondsPerForwardPage(), static_cast<double>(MAX_SAMPLE_SECONDS), 0.5);
}

}  // namespace
