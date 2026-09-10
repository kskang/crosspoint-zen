#pragma once
#include <cstdint>

// Orientation shortcuts for the long-press Confirm menu, kept free of firmware
// headers so the mapping can be exercised by the host test suite.
//
// The values below mirror CrossPointSettings::ORIENTATION and the crosszen
// block of CrossPointSettings::LONG_PRESS_MENU_FUNCTION; EpubReaderActivity.cpp
// static_asserts that they still agree.
namespace ReaderOrientation {

constexpr uint8_t PORTRAIT = 0;
constexpr uint8_t LANDSCAPE_CW = 1;
constexpr uint8_t INVERTED = 2;
constexpr uint8_t LANDSCAPE_CCW = 3;
constexpr uint8_t ORIENTATION_COUNT = 4;

constexpr uint8_t ROTATE_90 = 4;
constexpr uint8_t FLIP_PORTRAIT = 5;
constexpr uint8_t FLIP_LANDSCAPE = 6;

// Returns the orientation a long press should switch to, or `current` when the
// function is not one of the three orientation shortcuts.
//
// Both flips enter their own pair from the outside: pressing "flip portrait"
// while landscape lands on PORTRAIT, and "flip landscape" while portrait lands
// on LANDSCAPE_CW. Neither flip can leave its pair afterwards.
constexpr uint8_t next(const uint8_t function, const uint8_t current) {
  const uint8_t orientation = current < ORIENTATION_COUNT ? current : PORTRAIT;
  switch (function) {
    case ROTATE_90:
      return static_cast<uint8_t>((orientation + 1) % ORIENTATION_COUNT);
    case FLIP_PORTRAIT:
      return orientation == PORTRAIT ? INVERTED : PORTRAIT;
    case FLIP_LANDSCAPE:
      return orientation == LANDSCAPE_CW ? LANDSCAPE_CCW : LANDSCAPE_CW;
    default:
      return current;
  }
}

}  // namespace ReaderOrientation
