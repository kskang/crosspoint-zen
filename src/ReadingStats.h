#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

struct BookReadingStats {
  // The pace is the median of the latest forward pages, so pages skimmed in seconds or left
  // on screen for minutes move it only once they make up half the window. Odd so the median
  // is a single sample.
  static constexpr uint8_t PACE_WINDOW = 31;

  uint32_t totalReadingSeconds = 0;
  uint32_t totalPagesTurned = 0;
  uint32_t estimatedTimeLeftSeconds = 0;
  uint16_t sessionCount = 0;
  bool isCompleted = false;
  uint8_t paceSampleCount = 0;
  // Oldest first.
  uint16_t paceSamples[PACE_WINDOW] = {};

  float secondsPerForwardPage() const {
    if (paceSampleCount == 0) return 0.0f;
    uint16_t sorted[PACE_WINDOW];
    std::copy(paceSamples, paceSamples + paceSampleCount, sorted);
    std::sort(sorted, sorted + paceSampleCount);
    const uint8_t middle = paceSampleCount / 2;
    if (paceSampleCount % 2 == 1) return sorted[middle];
    return (static_cast<float>(sorted[middle - 1]) + static_cast<float>(sorted[middle])) / 2.0f;
  }

  void recordForwardPageRead(uint32_t seconds) {
    if (seconds == 0) return;
    if (seconds > UINT16_MAX) seconds = UINT16_MAX;
    if (paceSampleCount == PACE_WINDOW) {
      std::copy(paceSamples + 1, paceSamples + PACE_WINDOW, paceSamples);
      paceSampleCount--;
    }
    paceSamples[paceSampleCount++] = static_cast<uint16_t>(seconds);
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
