#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "EndOfBookOptions.h"
#include "ReadingStats.h"
#include "activities/Activity.h"

class ReaderActivity : public Activity {
 protected:
  std::string bookPath;
  int pagesUntilFullRefresh = 0;
  bool forcedRefreshPending = false;

  BookReadingStats readingStats;
  GlobalReadingStats globalReadingStats;
  std::atomic<uint32_t> pageDisplayedAtMs{0};
  uint32_t sessionReadingMs = 0;
  uint32_t committedSessionSeconds = 0;
  // Outlives pause/resume so the window still fills when the reader opens menus often.
  uint32_t recentPaceSeconds = 0;
  uint16_t recentPaceSamples = 0;
  bool paceSampleWarmupPending = true;
  bool readingStatsLoaded = false;
  bool readingStatsDirty = false;

  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  std::atomic<bool> endOfBookOptionsReady{false};

  explicit ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                          std::string bookPath, bool allowFastInitialRefresh);

  virtual bool loadBook() = 0;
  virtual std::string getBookTitle() const = 0;
  virtual std::string getBookAuthor() const { return ""; }
  virtual std::string getBookThumbBmpPath() const { return ""; }

  virtual bool handleFormatInput() { return false; }
  virtual bool pageTurn(bool isForward) = 0;
  virtual bool skipPages(int amount) { return pageTurn(amount > 0); }
  virtual bool isAtEndOfBook() const = 0;
  virtual bool shouldCountForwardPageTurn() const { return true; }
  virtual float estimatedRemainingPages();
  virtual void onReturnFromEndOfBook() {}

  virtual void renderBook() = 0;
  virtual void applyInitialOrientation();
  virtual void onEndOfBookRendered() {}

  bool handleBackNavigation();
  /** True while the end-of-book suggestion menu is on screen and owning input. */
  bool endOfBookMenuActive() const;
  bool handleEndOfBookMenu(bool suppressConfirmRelease = false);
  bool handleEndOfBookPageTurn(bool prevTriggered, bool nextTriggered);
  void clearEndOfBookOptionsIfNeeded();
  void disableFastInitialRefresh();
  void recordVisiblePageTime(uint32_t nowMs);
  void recordPaceSample(uint32_t seconds);
  void updateEstimatedTimeLeft();
  void saveReadingStats(bool finishSession);
  void markBookCompleted();
  void openReadingStats();
  bool trackPageTurn(bool isForward, bool skip = false, int skipAmount = 0);

 public:
  ~ReaderActivity() override = default;

  static std::unique_ptr<ReaderActivity> create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                std::string path, bool allowFastInitialRefresh);

  void onEnter() override;
  void onExit() override;
  void onPause() override;
  void onResume() override;
  void loop() override;
  void render(RenderLock&& lock) override;

  bool isReaderActivity() const final { return true; }
  bool handleForcedRefresh() final;
};
