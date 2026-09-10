#include <gtest/gtest.h>

#include <cstdint>

#include "src/activities/reader/PageDensity.h"

namespace {

constexpr float PROSE_FALLBACK = 2000.0f;

TEST(PageDensity, FallsBackUntilAChapterIsAccounted) {
  const PageDensity density;

  EXPECT_FLOAT_EQ(density.bytesPerPage(PROSE_FALLBACK), PROSE_FALLBACK);
}

TEST(PageDensity, UsesTheChapterRatioOnceOneIsAccounted) {
  PageDensity density;
  density.account(0, 40000, 20);

  EXPECT_FLOAT_EQ(density.bytesPerPage(PROSE_FALLBACK), 2000.0f);
}

TEST(PageDensity, AveragesAcrossAccountedChapters) {
  PageDensity density;
  density.account(0, 40000, 20);  // 2000 bytes a page
  density.account(1, 20000, 20);  // 1000 bytes a page

  // 60000 bytes over 40 pages, not the mean of the two ratios.
  EXPECT_FLOAT_EQ(density.bytesPerPage(PROSE_FALLBACK), 1500.0f);
}

TEST(PageDensity, AccountsEachChapterOnlyOncePerVisit) {
  PageDensity density;
  density.account(0, 40000, 20);
  density.account(0, 40000, 20);
  density.account(0, 40000, 20);

  EXPECT_EQ(density.bytes, 40000u);
  EXPECT_EQ(density.pages, 20u);
}

// The case this exists for: a full-page illustration chapter carries a tiny XHTML file
// because the image itself is a separate zip entry. Its ratio alone would shrink
// bytes-per-page several times over and balloon the remaining-page estimate.
TEST(PageDensity, ASinglePageImageChapterBarelyMovesTheAverage) {
  PageDensity density;
  for (int chapter = 0; chapter < 10; chapter++) density.account(chapter, 40000, 20);
  const float beforeImage = density.bytesPerPage(PROSE_FALLBACK);

  density.account(10, 500, 1);

  const float afterImage = density.bytesPerPage(PROSE_FALLBACK);
  EXPECT_NEAR(afterImage, beforeImage, beforeImage * 0.05f);
  // Using that chapter alone would have given 500 bytes a page.
  EXPECT_GT(afterImage, 1800.0f);
}

TEST(PageDensity, IgnoresChaptersWithNoBytesOrNoPages) {
  PageDensity density;
  density.account(0, 0, 20);
  density.account(1, 40000, 0);

  EXPECT_EQ(density.bytes, 0u);
  EXPECT_EQ(density.pages, 0u);
  EXPECT_FLOAT_EQ(density.bytesPerPage(PROSE_FALLBACK), PROSE_FALLBACK);
}

// Page counts belong to one render spec, so a font or orientation change invalidates them.
TEST(PageDensity, ClearReturnsToTheFallback) {
  PageDensity density;
  density.account(0, 40000, 20);
  density.clear();

  EXPECT_FLOAT_EQ(density.bytesPerPage(PROSE_FALLBACK), PROSE_FALLBACK);
  EXPECT_EQ(density.accountedSpineIndex, -1);
}

TEST(PageDensity, ReaccountsTheSameChapterAfterAClear) {
  PageDensity density;
  density.account(0, 40000, 20);
  density.clear();
  density.account(0, 30000, 20);

  EXPECT_FLOAT_EQ(density.bytesPerPage(PROSE_FALLBACK), 1500.0f);
}

TEST(PageDensity, SaturationKeepsTheLastGoodAverage) {
  PageDensity density;
  density.bytes = UINT32_MAX - 10;
  density.pages = 1000;
  const float before = density.bytesPerPage(PROSE_FALLBACK);

  density.account(7, 40000, 20);

  EXPECT_FLOAT_EQ(density.bytesPerPage(PROSE_FALLBACK), before);
}

}  // namespace
