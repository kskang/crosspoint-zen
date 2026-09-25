#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "src/ReadingStats.h"

namespace {

const std::string BOOK = "/books/novel.epub";

uint32_t crc32(const std::vector<uint8_t>& data, const size_t size) {
  uint32_t value = 0xffffffff;
  for (size_t i = 0; i < size; i++) {
    value ^= data[i];
    for (int bit = 0; bit < 8; bit++) value = (value >> 1) ^ (0xedb88320U & (0U - (value & 1U)));
  }
  return ~value;
}

void putLe(std::vector<uint8_t>& data, const size_t offset, const uint32_t value, const size_t width) {
  for (size_t i = 0; i < width; i++) data[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

// The only file the store keeps for a fresh book.
std::string bookFilePath() {
  EXPECT_TRUE(ReadingStatsStore::saveBook(BOOK, {}));
  EXPECT_EQ(fake::files.size(), 1u);
  const std::string path = fake::files.begin()->first;
  fake::files.clear();
  return path;
}

class ReadingStatsStoreTest : public ::testing::Test {
 protected:
  void SetUp() override { fake::files.clear(); }
};

TEST_F(ReadingStatsStoreTest, KeepsThePaceWindowAcrossSaveAndLoad) {
  BookReadingStats stats;
  stats.totalReadingSeconds = 5400;
  stats.totalPagesTurned = 180;
  stats.estimatedTimeLeftSeconds = 3600;
  stats.sessionCount = 7;
  stats.isCompleted = true;
  for (int page = 0; page < 40; page++) stats.recordForwardPageRead(page % 4 == 0 ? 3 : 25);
  ASSERT_TRUE(ReadingStatsStore::saveBook(BOOK, stats));

  const BookReadingStats loaded = ReadingStatsStore::loadBook(BOOK);

  EXPECT_EQ(loaded.totalReadingSeconds, 5400u);
  EXPECT_EQ(loaded.totalPagesTurned, 180u);
  EXPECT_EQ(loaded.estimatedTimeLeftSeconds, 3600u);
  EXPECT_EQ(loaded.sessionCount, 7);
  EXPECT_TRUE(loaded.isCompleted);
  EXPECT_EQ(loaded.paceSampleCount, stats.paceSampleCount);
  EXPECT_EQ(loaded.secondsPerForwardPage(), stats.secondsPerForwardPage());

  // The oldest sample must still be the first to leave after a reload.
  BookReadingStats continued = loaded;
  stats.recordForwardPageRead(60);
  continued.recordForwardPageRead(60);
  EXPECT_EQ(std::vector<uint16_t>(continued.paceSamples, continued.paceSamples + continued.paceSampleCount),
            std::vector<uint16_t>(stats.paceSamples, stats.paceSamples + stats.paceSampleCount));
}

TEST_F(ReadingStatsStoreTest, KeepsTheTotalsOfAVersionTwoFile) {
  const std::string path = bookFilePath();
  std::vector<uint8_t> legacy(24, 0);
  legacy[0] = 2;
  putLe(legacy, 1, 5400, 4);  // reading seconds
  putLe(legacy, 5, 180, 4);   // pages turned
  putLe(legacy, 9, 3600, 4);  // estimated time left
  putLe(legacy, 13, 7, 2);    // sessions
  putLe(legacy, 15, 24, 2);   // mean seconds per page
  putLe(legacy, 17, 150, 2);  // mean sample count
  legacy[19] = 1;             // completed
  putLe(legacy, 20, crc32(legacy, 20), 4);
  fake::files[path] = std::make_shared<std::vector<uint8_t>>(legacy);

  const BookReadingStats loaded = ReadingStatsStore::loadBook(BOOK);

  EXPECT_EQ(loaded.totalReadingSeconds, 5400u);
  EXPECT_EQ(loaded.totalPagesTurned, 180u);
  EXPECT_EQ(loaded.estimatedTimeLeftSeconds, 3600u);
  EXPECT_EQ(loaded.sessionCount, 7);
  EXPECT_TRUE(loaded.isCompleted);
  EXPECT_EQ(loaded.paceSampleCount, 0);
}

TEST_F(ReadingStatsStoreTest, FallsBackToThePreviousCopyWhenTheFileIsCorrupt) {
  const std::string path = bookFilePath();
  BookReadingStats first;
  first.totalReadingSeconds = 100;
  first.recordForwardPageRead(20);
  BookReadingStats second = first;
  second.totalReadingSeconds = 200;
  ASSERT_TRUE(ReadingStatsStore::saveBook(BOOK, first));
  ASSERT_TRUE(ReadingStatsStore::saveBook(BOOK, second));
  (*fake::files[path])[1] ^= 0xff;

  const BookReadingStats loaded = ReadingStatsStore::loadBook(BOOK);

  EXPECT_EQ(loaded.totalReadingSeconds, 100u);
  EXPECT_EQ(loaded.paceSampleCount, 1);
}

}  // namespace
