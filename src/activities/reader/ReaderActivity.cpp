#include "ReaderActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderActivity.h"
#include "ReaderUtils.h"
#include "ReadingStatsActivity.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "TxtReaderActivity.h"
#include "XtcReaderActivity.h"

namespace {
constexpr uint32_t MIN_PAGE_DWELL_MS = 2000;
constexpr uint32_t MAX_READING_IDLE_MS = 5 * 60 * 1000;
constexpr uint32_t MIN_SAVED_READING_SECONDS = 10;
constexpr uint32_t MIN_SESSION_SECONDS = 60;
constexpr uint16_t RECENT_PACE_SAMPLES = 50;
}  // namespace

ReaderActivity::ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               std::string bookPath, const bool allowFastInitialRefresh)
    : Activity(name, renderer, mappedInput), bookPath(std::move(bookPath)) {
  if (allowFastInitialRefresh) {
    const int refreshFrequency = SETTINGS.getRefreshFrequency();
    pagesUntilFullRefresh = refreshFrequency > 1 ? refreshFrequency : 2;
  }
}

std::unique_ptr<ReaderActivity> ReaderActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string path, const bool allowFastInitialRefresh) {
  // ActivityManager requires heap ownership; each branch allocates exactly one screen-lifetime object.
  std::unique_ptr<ReaderActivity> activity;
  if (FsHelpers::hasXtcExtension(path)) {
    activity = makeUniqueNoThrow<XtcReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    activity = makeUniqueNoThrow<TxtReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else {
    activity = makeUniqueNoThrow<EpubReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  }

  if (!activity) {
    LOG_ERR("READER", "OOM: reader activity");
  }
  return activity;
}

void ReaderActivity::applyInitialOrientation() { ReaderUtils::applyOrientation(renderer, SETTINGS.orientation); }

void ReaderActivity::disableFastInitialRefresh() { pagesUntilFullRefresh = 0; }

void ReaderActivity::onEnter() {
  Activity::onEnter();

  // Heap ledger for field crash reports: free vs largest block distinguishes a
  // leak (free falls) from fragmentation (free stable, largest collapses).
  LOG_INF("MEM", "reader enter: free=%u max_block=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  if (!Storage.exists(bookPath.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", bookPath.c_str());
    finish();
    return;
  }

  sdFontSystem.ensureLoaded(renderer);
  applyInitialOrientation();

  if (!loadBook()) {
    finish();
    return;
  }

  readingStats = ReadingStatsStore::loadBook(bookPath);
  globalReadingStats = ReadingStatsStore::loadGlobal();
  readingStatsLoaded = true;

  APP_STATE.openEpubPath = bookPath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(bookPath, getBookTitle(), getBookAuthor(), getBookThumbBmpPath());
  requestUpdate();
}

void ReaderActivity::onExit() {
  saveReadingStats(true);
  Activity::onExit();

  LOG_INF("MEM", "reader exit: free=%u max_block=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();

  endOfBookOptions.reset();
  endOfBookOptionsReady.store(false, std::memory_order_release);
}

void ReaderActivity::onPause() { saveReadingStats(false); }

void ReaderActivity::onResume() {
  if (readingStatsLoaded && !readingStatsDirty) {
    const BookReadingStats loadedBookStats = ReadingStatsStore::loadBook(bookPath);
    const GlobalReadingStats loadedGlobalStats = ReadingStatsStore::loadGlobal();
    const bool statsWereReset = loadedBookStats.totalReadingSeconds < readingStats.totalReadingSeconds ||
                                loadedBookStats.totalPagesTurned < readingStats.totalPagesTurned ||
                                loadedBookStats.sessionCount < readingStats.sessionCount ||
                                loadedGlobalStats.totalReadingSeconds < globalReadingStats.totalReadingSeconds ||
                                loadedGlobalStats.totalPagesTurned < globalReadingStats.totalPagesTurned ||
                                loadedGlobalStats.totalSessions < globalReadingStats.totalSessions ||
                                loadedGlobalStats.completedBooks < globalReadingStats.completedBooks;
    readingStats = loadedBookStats;
    globalReadingStats = loadedGlobalStats;
    if (statsWereReset) {
      sessionReadingMs = 0;
      committedSessionSeconds = 0;
      recentPaceSeconds = 0;
      recentPaceSamples = 0;
    }
  }
  pageDisplayedAtMs.store(0, std::memory_order_release);
  paceSampleWarmupPending = true;
}

void ReaderActivity::recordVisiblePageTime(const uint32_t nowMs) {
  const uint32_t startedAt = pageDisplayedAtMs.exchange(0, std::memory_order_acq_rel);
  if (startedAt == 0) return;
  const uint32_t elapsedMs = nowMs - startedAt;
  if (elapsedMs == 0 || elapsedMs > MAX_READING_IDLE_MS) return;

  sessionReadingMs = sessionReadingMs > UINT32_MAX - elapsedMs ? UINT32_MAX : sessionReadingMs + elapsedMs;
}

void ReaderActivity::recordPaceSample(const uint32_t seconds) {
  if (seconds == 0) return;
  readingStats.recordForwardPageRead(seconds);
  recentPaceSeconds += seconds;
  recentPaceSamples++;
  if (recentPaceSamples < RECENT_PACE_SAMPLES) return;

  readingStats.restartPaceIfRecentDiverged(recentPaceSeconds, recentPaceSamples);
  recentPaceSeconds = 0;
  recentPaceSamples = 0;
}

float ReaderActivity::estimatedRemainingPages() {
  const ScreenshotInfo info = getScreenshotInfo();
  if (info.totalPages <= 0 || info.currentPage <= 0 || info.currentPage >= info.totalPages) return 0.0f;
  return static_cast<float>(info.totalPages - info.currentPage);
}

void ReaderActivity::updateEstimatedTimeLeft() {
  const uint32_t previousEstimate = readingStats.estimatedTimeLeftSeconds;
  const float secondsPerPage = readingStats.secondsPerForwardPage();
  if (secondsPerPage <= 0.0f || readingStats.paceSampleCount < 2) {
    readingStats.estimatedTimeLeftSeconds = 0;
    if (previousEstimate != 0) readingStatsDirty = true;
    return;
  }
  const float remainingPages = estimatedRemainingPages();
  if (remainingPages <= 0.0f) {
    readingStats.estimatedTimeLeftSeconds = 0;
    if (previousEstimate != 0) readingStatsDirty = true;
    return;
  }
  const double seconds = static_cast<double>(remainingPages) * secondsPerPage;
  readingStats.estimatedTimeLeftSeconds = seconds >= UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(seconds + 0.5);
  if (readingStats.estimatedTimeLeftSeconds != previousEstimate) readingStatsDirty = true;
}

void ReaderActivity::saveReadingStats(const bool finishSession) {
  if (!readingStatsLoaded) return;
  recordVisiblePageTime(millis());
  updateEstimatedTimeLeft();
  const uint32_t sessionSeconds = sessionReadingMs / 1000;
  if (sessionSeconds >= MIN_SAVED_READING_SECONDS && sessionSeconds > committedSessionSeconds) {
    const uint32_t delta = sessionSeconds - committedSessionSeconds;
    readingStats.totalReadingSeconds =
        readingStats.totalReadingSeconds > UINT32_MAX - delta ? UINT32_MAX : readingStats.totalReadingSeconds + delta;
    globalReadingStats.totalReadingSeconds = globalReadingStats.totalReadingSeconds > UINT32_MAX - delta
                                                 ? UINT32_MAX
                                                 : globalReadingStats.totalReadingSeconds + delta;
    committedSessionSeconds = sessionSeconds;
    readingStatsDirty = true;
  }
  if (finishSession && sessionSeconds >= MIN_SESSION_SECONDS) {
    if (readingStats.sessionCount < UINT16_MAX) readingStats.sessionCount++;
    if (globalReadingStats.totalSessions < UINT32_MAX) globalReadingStats.totalSessions++;
    readingStatsDirty = true;
  }

  if (readingStatsDirty) {
    const bool bookSaved = ReadingStatsStore::saveBook(bookPath, readingStats);
    const bool globalSaved = ReadingStatsStore::saveGlobal(globalReadingStats);
    readingStatsDirty = !bookSaved || !globalSaved;
  }
}

void ReaderActivity::markBookCompleted() {
  // Reaching the end leaves nothing to estimate, whether or not this is the first time.
  if (readingStats.estimatedTimeLeftSeconds != 0) {
    readingStats.estimatedTimeLeftSeconds = 0;
    readingStatsDirty = true;
  }
  if (readingStats.isCompleted) return;
  readingStats.isCompleted = true;
  if (globalReadingStats.completedBooks < UINT32_MAX) globalReadingStats.completedBooks++;
  readingStatsDirty = true;
}

void ReaderActivity::openReadingStats() {
  auto activity = makeUniqueNoThrow<ReadingStatsActivity>(renderer, mappedInput, bookPath, getBookTitle());
  if (!activity) {
    LOG_ERR("READER", "OOM: reading stats activity");
    return;
  }
  startActivityForResult(std::move(activity), [](const ActivityResult&) {});
}

bool ReaderActivity::trackPageTurn(const bool isForward, const bool skip, const int skipAmount) {
  const uint32_t nowMs = millis();
  const uint32_t startedAt = pageDisplayedAtMs.load(std::memory_order_acquire);
  const uint32_t elapsedMs = startedAt == 0 ? 0 : nowMs - startedAt;
  const bool shouldCountForward = shouldCountForwardPageTurn();
  recordVisiblePageTime(nowMs);

  const bool changed = skip ? skipPages(skipAmount) : pageTurn(isForward);
  if (!changed) return false;
  if (skip || !isForward || !shouldCountForward) {
    paceSampleWarmupPending = true;
    if (isForward && shouldCountForward && isAtEndOfBook()) markBookCompleted();
    return true;
  }

  if (elapsedMs >= MIN_PAGE_DWELL_MS) {
    if (readingStats.totalPagesTurned < UINT32_MAX) readingStats.totalPagesTurned++;
    if (globalReadingStats.totalPagesTurned < UINT32_MAX) globalReadingStats.totalPagesTurned++;
    readingStatsDirty = true;
    if (!paceSampleWarmupPending && elapsedMs <= MAX_READING_IDLE_MS) {
      recordPaceSample((elapsedMs + 500) / 1000);
    }
  }
  paceSampleWarmupPending = false;
  if (isAtEndOfBook()) markBookCompleted();
  updateEstimatedTimeLeft();
  return true;
}

bool ReaderActivity::handleBackNavigation() {
  return ReaderUtils::handleBackNavigation(mappedInput, activityManager, bookPath.c_str(),
                                           {this, [](void* ctx) { static_cast<ReaderActivity*>(ctx)->onGoHome(); }});
}

void ReaderActivity::clearEndOfBookOptionsIfNeeded() {
  if (isAtEndOfBook() || !endOfBookOptionsReady.load(std::memory_order_acquire)) return;

  RenderLock lock(*this);
  endOfBookOptionsReady.store(false, std::memory_order_release);
  endOfBookOptions.reset();
}

bool ReaderActivity::endOfBookMenuActive() const {
  return isAtEndOfBook() && endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive();
}

bool ReaderActivity::handleEndOfBookMenu(const bool suppressConfirmRelease) {
  if (suppressConfirmRelease || !endOfBookMenuActive()) {
    return false;
  }

  std::string openPath;
  switch (endOfBookOptions->handleMenuInput(mappedInput, &openPath)) {
    case EndOfBookOptions::Action::OpenBook:
      activityManager.goToReader(openPath);
      return true;
    case EndOfBookOptions::Action::GoHome:
      onGoHome();
      return true;
    case EndOfBookOptions::Action::LastPage:
      onReturnFromEndOfBook();
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::Redraw:
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::None:
      return false;
  }

  return false;
}

bool ReaderActivity::handleEndOfBookPageTurn(const bool prevTriggered, const bool nextTriggered) {
  if (!isAtEndOfBook()) return false;

  if (endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive()) {
    return true;
  }
  if (nextTriggered) {
    onGoHome();
  } else if (prevTriggered) {
    onReturnFromEndOfBook();
    requestUpdate();
  }
  return true;
}

void ReaderActivity::loop() {
  clearEndOfBookOptionsIfNeeded();
  if (handleEndOfBookMenu()) return;
  if (handleFormatInput()) return;
  if (handleBackNavigation()) return;

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  auto [prevTriggered, nextTriggered, fromTilt, fromSideHold] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) return;
  if (handleEndOfBookPageTurn(prevTriggered, nextTriggered)) return;

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool skip = !fromTilt && !fromSideHold && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP &&
                    heldMs >= ReaderUtils::SKIP_HOLD_MS;

  if (prevTriggered) {
    if (skip) {
      trackPageTurn(false, true, -10);
    } else {
      trackPageTurn(false);
    }
  } else {
    if (skip) {
      trackPageTurn(true, true, 10);
    } else {
      trackPageTurn(true);
    }
  }
  requestUpdate();
}

void ReaderActivity::render(RenderLock&&) {
  if (isAtEndOfBook()) {
    if (!endOfBookOptions) {
      endOfBookOptions = makeUniqueNoThrow<EndOfBookOptions>(renderer);
      if (!endOfBookOptions) LOG_ERR("READER", "OOM: EndOfBookOptions");
    }
    renderer.clearScreen();
    if (endOfBookOptions) {
      endOfBookOptions->loadOnce(bookPath);
      // Release-publish AFTER loadOnce() so the main task's acquire load can't
      // observe an object whose names/selector are still being populated.
      endOfBookOptionsReady.store(true, std::memory_order_release);
      endOfBookOptions->render(renderer, mappedInput);
    }
    renderer.displayBuffer();
    onEndOfBookRendered();
    return;
  }

  renderBook();
  uint32_t unset = 0;
  pageDisplayedAtMs.compare_exchange_strong(unset, millis(), std::memory_order_release, std::memory_order_relaxed);
}

bool ReaderActivity::handleForcedRefresh() {
  {
    RenderLock lock(*this);
    pagesUntilFullRefresh = 1;
    forcedRefreshPending = true;
  }
  requestUpdate();
  return true;
}
