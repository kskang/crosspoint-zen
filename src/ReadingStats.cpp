#include "ReadingStats.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>

namespace {
constexpr char STATS_DIR[] = "/.crosspoint/reading_stats";
constexpr char GLOBAL_PATH[] = "/.crosspoint/reading_stats/global.bin";
constexpr char GLOBAL_BACKUP_PATH[] = "/.crosspoint/reading_stats/global.backup.bin";
constexpr uint8_t BOOK_VERSION = 2;
constexpr uint8_t GLOBAL_VERSION = 2;
constexpr size_t BOOK_FILE_SIZE = 24;
constexpr size_t GLOBAL_FILE_SIZE = 21;
constexpr size_t PATH_BUFFER_SIZE = 64;

uint16_t readLe16(const uint8_t* data, const size_t offset) {
  return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t readLe32(const uint8_t* data, const size_t offset) {
  return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

void writeLe16(uint8_t* data, const size_t offset, const uint16_t value) {
  data[offset] = value & 0xff;
  data[offset + 1] = value >> 8;
}

void writeLe32(uint8_t* data, const size_t offset, const uint32_t value) {
  data[offset] = value & 0xff;
  data[offset + 1] = value >> 8;
  data[offset + 2] = value >> 16;
  data[offset + 3] = value >> 24;
}

uint32_t checksum(const uint8_t* data, const size_t size) {
  uint32_t value = 0xffffffff;
  for (size_t i = 0; i < size; i++) {
    value ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      value = (value >> 1) ^ (0xedb88320U & (0U - (value & 1U)));
    }
  }
  return ~value;
}

template <size_t N>
bool isValidRecord(const uint8_t (&data)[N], const uint8_t version) {
  static_assert(N >= sizeof(uint32_t) + 1);
  return data[0] == version && readLe32(data, N - sizeof(uint32_t)) == checksum(data, N - sizeof(uint32_t));
}

bool writeAtomically(const char* path, const uint8_t* data, const size_t size) {
  if (!Storage.ensureDirectoryExists(STATS_DIR)) {
    LOG_ERR("STATS", "Could not create stats directory");
    return false;
  }

  char tempPath[PATH_BUFFER_SIZE];
  char previousPath[PATH_BUFFER_SIZE];
  snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);
  snprintf(previousPath, sizeof(previousPath), "%s.previous", path);
  if (Storage.exists(tempPath) && !Storage.remove(tempPath)) return false;

  HalFile file;
  if (!Storage.openFileForWrite("STATS", tempPath, file)) return false;
  if (file.write(data, size) != size) {
    LOG_ERR("STATS", "Short write: %s", tempPath);
    file.close();
    Storage.remove(tempPath);
    return false;
  }
  file.flush();
  file.close();

  if (Storage.exists(previousPath) && !Storage.remove(previousPath)) {
    Storage.remove(tempPath);
    return false;
  }
  const bool hadPreviousFile = Storage.exists(path);
  if (hadPreviousFile && !Storage.rename(path, previousPath)) {
    Storage.remove(tempPath);
    return false;
  }
  if (!Storage.rename(tempPath, path)) {
    LOG_ERR("STATS", "Could not install stats file: %s", path);
    if (hadPreviousFile) Storage.rename(previousPath, path);
    Storage.remove(tempPath);
    return false;
  }
  return true;
}

template <size_t N>
bool readExact(const char* path, uint8_t (&data)[N]) {
  HalFile file;
  if (!Storage.openFileForRead("STATS", path, file) || file.fileSize() != N) return false;
  return file.read(data, N) == static_cast<int>(N);
}

bool moveStatsFile(char* oldPath, const size_t oldPathSize, char* newPath, const size_t newPathSize,
                   const char* suffix) {
  if (suffix[0] != '\0') {
    strncat(oldPath, suffix, oldPathSize - strlen(oldPath) - 1);
    strncat(newPath, suffix, newPathSize - strlen(newPath) - 1);
  }
  if (!Storage.exists(oldPath)) return true;
  if (Storage.exists(newPath) || !Storage.rename(oldPath, newPath)) {
    LOG_ERR("STATS", "Could not move book stats: %s", oldPath);
    return false;
  }
  return true;
}
}  // namespace

void ReadingStatsStore::bookStatsPath(const std::string& bookPath, char* buffer, const size_t bufferSize) {
  uint64_t key = 14695981039346656037ULL;
  for (const unsigned char byte : bookPath) {
    key ^= byte;
    key *= 1099511628211ULL;
  }
  snprintf(buffer, bufferSize, "%s/book_%016llx.bin", STATS_DIR, static_cast<unsigned long long>(key));
}

BookReadingStats ReadingStatsStore::loadBook(const std::string& bookPath) {
  char path[PATH_BUFFER_SIZE];
  bookStatsPath(bookPath, path, sizeof(path));
  char previousPath[PATH_BUFFER_SIZE];
  snprintf(previousPath, sizeof(previousPath), "%s.previous", path);
  uint8_t data[BOOK_FILE_SIZE] = {};
  BookReadingStats stats;
  if ((!readExact(path, data) || !isValidRecord(data, BOOK_VERSION)) &&
      (!readExact(previousPath, data) || !isValidRecord(data, BOOK_VERSION))) {
    return stats;
  }
  stats.totalReadingSeconds = readLe32(data, 1);
  stats.totalPagesTurned = readLe32(data, 5);
  stats.estimatedTimeLeftSeconds = readLe32(data, 9);
  stats.sessionCount = readLe16(data, 13);
  stats.avgSecondsPerForwardPage = readLe16(data, 15);
  stats.paceSampleCount = readLe16(data, 17);
  stats.isCompleted = data[19] != 0;
  stats.seedPaceTotalFromStoredAverage();
  return stats;
}

GlobalReadingStats ReadingStatsStore::loadGlobal() {
  uint8_t data[GLOBAL_FILE_SIZE] = {};
  GlobalReadingStats stats;
  char previousPath[PATH_BUFFER_SIZE];
  snprintf(previousPath, sizeof(previousPath), "%s.previous", GLOBAL_PATH);
  if ((!readExact(GLOBAL_PATH, data) || !isValidRecord(data, GLOBAL_VERSION)) &&
      (!readExact(previousPath, data) || !isValidRecord(data, GLOBAL_VERSION))) {
    return stats;
  }
  stats.totalSessions = readLe32(data, 1);
  stats.totalReadingSeconds = readLe32(data, 5);
  stats.totalPagesTurned = readLe32(data, 9);
  stats.completedBooks = readLe32(data, 13);
  return stats;
}

bool ReadingStatsStore::saveBook(const std::string& bookPath, const BookReadingStats& stats) {
  char path[PATH_BUFFER_SIZE];
  bookStatsPath(bookPath, path, sizeof(path));
  uint8_t data[BOOK_FILE_SIZE] = {};
  data[0] = BOOK_VERSION;
  writeLe32(data, 1, stats.totalReadingSeconds);
  writeLe32(data, 5, stats.totalPagesTurned);
  writeLe32(data, 9, stats.estimatedTimeLeftSeconds);
  writeLe16(data, 13, stats.sessionCount);
  writeLe16(data, 15, stats.avgSecondsPerForwardPage);
  writeLe16(data, 17, stats.paceSampleCount);
  data[19] = stats.isCompleted ? 1 : 0;
  writeLe32(data, BOOK_FILE_SIZE - sizeof(uint32_t), checksum(data, BOOK_FILE_SIZE - sizeof(uint32_t)));
  return writeAtomically(path, data, sizeof(data));
}

bool ReadingStatsStore::saveGlobal(const GlobalReadingStats& stats) {
  uint8_t data[GLOBAL_FILE_SIZE] = {};
  data[0] = GLOBAL_VERSION;
  writeLe32(data, 1, stats.totalSessions);
  writeLe32(data, 5, stats.totalReadingSeconds);
  writeLe32(data, 9, stats.totalPagesTurned);
  writeLe32(data, 13, stats.completedBooks);
  writeLe32(data, GLOBAL_FILE_SIZE - sizeof(uint32_t), checksum(data, GLOBAL_FILE_SIZE - sizeof(uint32_t)));
  return writeAtomically(GLOBAL_PATH, data, sizeof(data));
}

bool ReadingStatsStore::moveBook(const std::string& oldBookPath, const std::string& newBookPath) {
  char oldPath[PATH_BUFFER_SIZE];
  char newPath[PATH_BUFFER_SIZE];
  bookStatsPath(oldBookPath, oldPath, sizeof(oldPath));
  bookStatsPath(newBookPath, newPath, sizeof(newPath));
  const bool movedCurrent = moveStatsFile(oldPath, sizeof(oldPath), newPath, sizeof(newPath), "");
  bookStatsPath(oldBookPath, oldPath, sizeof(oldPath));
  bookStatsPath(newBookPath, newPath, sizeof(newPath));
  const bool movedPrevious = moveStatsFile(oldPath, sizeof(oldPath), newPath, sizeof(newPath), ".previous");
  return movedCurrent && movedPrevious;
}

bool ReadingStatsStore::resetBook(const std::string& bookPath) {
  char path[PATH_BUFFER_SIZE];
  bookStatsPath(bookPath, path, sizeof(path));
  bool ok = !Storage.exists(path) || Storage.remove(path);
  char auxiliaryPath[PATH_BUFFER_SIZE];
  snprintf(auxiliaryPath, sizeof(auxiliaryPath), "%s.previous", path);
  if (Storage.exists(auxiliaryPath) && !Storage.remove(auxiliaryPath)) ok = false;
  snprintf(auxiliaryPath, sizeof(auxiliaryPath), "%s.tmp", path);
  if (Storage.exists(auxiliaryPath) && !Storage.remove(auxiliaryPath)) ok = false;
  return ok;
}

bool ReadingStatsStore::resetGlobal() { return saveGlobal({}); }

bool ReadingStatsStore::backupGlobal() {
  if (!Storage.exists(GLOBAL_PATH) && !saveGlobal({})) return false;
  uint8_t data[GLOBAL_FILE_SIZE] = {};
  char previousPath[PATH_BUFFER_SIZE];
  snprintf(previousPath, sizeof(previousPath), "%s.previous", GLOBAL_PATH);
  if ((!readExact(GLOBAL_PATH, data) || !isValidRecord(data, GLOBAL_VERSION)) &&
      (!readExact(previousPath, data) || !isValidRecord(data, GLOBAL_VERSION))) {
    LOG_ERR("STATS", "Global stats file is invalid");
    return false;
  }
  return writeAtomically(GLOBAL_BACKUP_PATH, data, sizeof(data));
}

bool ReadingStatsStore::restoreGlobal() {
  uint8_t backup[GLOBAL_FILE_SIZE] = {};
  char previousPath[PATH_BUFFER_SIZE];
  snprintf(previousPath, sizeof(previousPath), "%s.previous", GLOBAL_BACKUP_PATH);
  if ((!readExact(GLOBAL_BACKUP_PATH, backup) || !isValidRecord(backup, GLOBAL_VERSION)) &&
      (!readExact(previousPath, backup) || !isValidRecord(backup, GLOBAL_VERSION))) {
    LOG_ERR("STATS", "Global stats backup is invalid");
    return false;
  }
  return writeAtomically(GLOBAL_PATH, backup, sizeof(backup));
}

bool ReadingStatsStore::hasGlobalBackup() {
  uint8_t data[GLOBAL_FILE_SIZE] = {};
  if (readExact(GLOBAL_BACKUP_PATH, data) && isValidRecord(data, GLOBAL_VERSION)) return true;
  char previousPath[PATH_BUFFER_SIZE];
  snprintf(previousPath, sizeof(previousPath), "%s.previous", GLOBAL_BACKUP_PATH);
  return readExact(previousPath, data) && isValidRecord(data, GLOBAL_VERSION);
}

void ReadingStatsStore::formatDuration(const uint32_t seconds, char* buffer, const size_t bufferSize) {
  if (!buffer || bufferSize == 0) return;
  if (seconds < 60) {
    snprintf(buffer, bufferSize, "%s", tr(STR_STATS_LESS_THAN_MIN));
    return;
  }
  const uint32_t hours = seconds / 3600;
  const uint32_t minutes = (seconds % 3600) / 60;
  if (hours == 0) {
    snprintf(buffer, bufferSize, tr(STR_STATS_MINUTES_FORMAT), static_cast<unsigned long>(minutes));
  } else {
    snprintf(buffer, bufferSize, tr(STR_STATS_HOURS_MINUTES_FORMAT), static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes));
  }
}
