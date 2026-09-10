#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct BookReadingStats {
  static constexpr uint16_t MAX_PACE_SAMPLES = 1000;
  // Restart the pace once the recent window and the running average differ by 1.3x.
  static constexpr uint32_t PACE_RESTART_RATIO_NUM = 13;
  static constexpr uint32_t PACE_RESTART_RATIO_DEN = 10;

  uint32_t totalReadingSeconds = 0;
  uint32_t totalPagesTurned = 0;
  uint32_t estimatedTimeLeftSeconds = 0;
  uint16_t sessionCount = 0;
  uint16_t avgSecondsPerForwardPage = 0;
  uint16_t paceSampleCount = 0;
  bool isCompleted = false;

  // Not persisted; rebuilt from the stored average on load.
  uint32_t paceSecondsTotal = 0;

  void seedPaceTotalFromStoredAverage() {
    paceSecondsTotal = static_cast<uint32_t>(avgSecondsPerForwardPage) * paceSampleCount;
  }

  float secondsPerForwardPage() const {
    if (paceSampleCount == 0) return 0.0f;
    return static_cast<float>(paceSecondsTotal) / static_cast<float>(paceSampleCount);
  }

  void recordForwardPageRead(uint32_t seconds) {
    if (seconds == 0) return;
    if (seconds > UINT16_MAX) seconds = UINT16_MAX;
    if (paceSampleCount >= MAX_PACE_SAMPLES) {
      // Halving preserves the average while giving later samples more weight.
      paceSecondsTotal /= 2;
      paceSampleCount /= 2;
    }
    paceSecondsTotal += seconds;
    paceSampleCount++;
    refreshStoredAverage();
  }

  // Drop the accumulated history and start over from the recent window once the two
  // disagree, so a real change of reading speed shows up instead of being averaged away.
  void restartPaceIfRecentDiverged(const uint32_t recentSeconds, const uint16_t recentSamples) {
    if (recentSamples == 0 || paceSampleCount == 0) return;
    const uint32_t recentAverage = recentSeconds / recentSamples;
    const uint32_t runningAverage = paceSecondsTotal / paceSampleCount;
    if (recentAverage == 0 || runningAverage == 0) return;
    if (recentAverage * PACE_RESTART_RATIO_DEN <= runningAverage * PACE_RESTART_RATIO_NUM &&
        runningAverage * PACE_RESTART_RATIO_DEN <= recentAverage * PACE_RESTART_RATIO_NUM) {
      return;
    }
    paceSecondsTotal = recentSeconds;
    paceSampleCount = recentSamples;
    refreshStoredAverage();
  }

 private:
  void refreshStoredAverage() {
    const uint32_t average = (paceSecondsTotal + paceSampleCount / 2) / paceSampleCount;
    avgSecondsPerForwardPage = average > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(average);
  }
};

struct GlobalReadingStats {
  uint32_t totalSessions = 0;
  uint32_t totalReadingSeconds = 0;
  uint32_t totalPagesTurned = 0;
  uint32_t completedBooks = 0;
};

class ReadingStatsStore {
 public:
  static BookReadingStats loadBook(const std::string& bookPath);
  static GlobalReadingStats loadGlobal();
  static bool saveBook(const std::string& bookPath, const BookReadingStats& stats);
  static bool saveGlobal(const GlobalReadingStats& stats);
  static bool moveBook(const std::string& oldBookPath, const std::string& newBookPath);
  static bool resetBook(const std::string& bookPath);
  static bool resetGlobal();
  static bool backupGlobal();
  static bool restoreGlobal();
  static bool hasGlobalBackup();
  static void formatDuration(uint32_t seconds, char* buffer, size_t bufferSize);

 private:
  static void bookStatsPath(const std::string& bookPath, char* buffer, size_t bufferSize);
};
