#pragma once

#include <cstdint>

// Bytes per page measured across the chapters laid out so far, used to turn the bytes left
// in a book into pages left. A single chapter is a poor yardstick: a full-page illustration
// chapter holds almost no markup, because the image is a separate zip entry, so its ratio
// alone would shrink bytes-per-page several times over and balloon the estimate.
// Page counts belong to one render spec, so clear() this whenever the layout changes.
struct PageDensity {
  uint32_t bytes = 0;
  uint32_t pages = 0;
  int accountedSpineIndex = -1;

  void clear() { *this = PageDensity(); }

  void account(const int spineIndex, const uint32_t chapterBytes, const uint16_t chapterPages) {
    if (spineIndex == accountedSpineIndex || chapterBytes == 0 || chapterPages == 0) return;
    if (bytes > UINT32_MAX - chapterBytes || pages > UINT32_MAX - chapterPages) return;
    bytes += chapterBytes;
    pages += chapterPages;
    accountedSpineIndex = spineIndex;
  }

  // The caller's own chapter ratio stands in until a chapter has been laid out in full.
  float bytesPerPage(const float fallback) const {
    if (bytes == 0 || pages == 0) return fallback;
    return static_cast<float>(bytes) / static_cast<float>(pages);
  }
};
