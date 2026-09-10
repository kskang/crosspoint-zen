#include "ReadingStatsActivity.h"

#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
static constexpr std::array<StrId, 6> BOOK_METRIC_LABELS = {
    StrId::STR_STATS_SESSIONS_SHORT, StrId::STR_STATS_TIME_SHORT, StrId::STR_STATS_PAGES_SHORT,
    StrId::STR_STATS_AVG_SHORT,      StrId::STR_STATS_PACE_SHORT, StrId::STR_STATS_TIME_LEFT_SHORT,
};
static constexpr std::array<StrId, 5> GLOBAL_METRIC_LABELS = {
    StrId::STR_STATS_SESSIONS_SHORT, StrId::STR_STATS_TIME_SHORT, StrId::STR_STATS_PAGES_SHORT,
    StrId::STR_STATS_AVG_SHORT,      StrId::STR_STATS_PACE_SHORT,
};
}  // namespace

ReadingStatsActivity::ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                           std::string bookTitle)
    : UiListActivity("ReadingStats", renderer, mappedInput),
      bookPath(std::move(bookPath)),
      bookTitle(std::move(bookTitle)) {}

void ReadingStatsActivity::onEnter() {
  UiListActivity::onEnter();
  reload();
}

void ReadingStatsActivity::addRow(const StrId label, const char* value, const RowAction action) {
  if (rowCount >= static_cast<int>(MAX_ROWS)) return;
  fui::ListItem item;
  item.label = I18N.get(label);
  item.subtitle = value;
  item.actionValue = static_cast<int16_t>(rowCount);
  rows[rowCount] = item;
  actions[rowCount] = action;
  rowCount++;
}

void ReadingStatsActivity::reload() {
  RenderLock lock(*this);
  rowCount = 0;
  globalStats = ReadingStatsStore::loadGlobal();
  if (!bookPath.empty()) {
    bookStats = ReadingStatsStore::loadBook(bookPath);
    snprintf(metricValues[BookSessions].data(), metricValues[BookSessions].size(), "%u", bookStats.sessionCount);
    ReadingStatsStore::formatDuration(bookStats.totalReadingSeconds, metricValues[BookTime].data(),
                                      metricValues[BookTime].size());
    snprintf(metricValues[BookForwardPages].data(), metricValues[BookForwardPages].size(), "%lu",
             static_cast<unsigned long>(bookStats.totalPagesTurned));
    if (bookStats.sessionCount > 0) {
      ReadingStatsStore::formatDuration(bookStats.totalReadingSeconds / bookStats.sessionCount,
                                        metricValues[BookAverageSession].data(),
                                        metricValues[BookAverageSession].size());
    } else {
      snprintf(metricValues[BookAverageSession].data(), metricValues[BookAverageSession].size(), "%s",
               tr(STR_STATS_EMPTY_VALUE));
    }
    if (bookStats.totalReadingSeconds > 0) {
      const float pagesPerMinute =
          static_cast<float>(bookStats.totalPagesTurned) * 60.0f / bookStats.totalReadingSeconds;
      snprintf(metricValues[BookPace].data(), metricValues[BookPace].size(), "%.1f", pagesPerMinute);
    } else {
      snprintf(metricValues[BookPace].data(), metricValues[BookPace].size(), "%s", tr(STR_STATS_EMPTY_VALUE));
    }
    if (bookStats.estimatedTimeLeftSeconds > 0) {
      ReadingStatsStore::formatDuration(bookStats.estimatedTimeLeftSeconds, metricValues[BookTimeLeft].data(),
                                        metricValues[BookTimeLeft].size());
    } else {
      snprintf(metricValues[BookTimeLeft].data(), metricValues[BookTimeLeft].size(), "%s", tr(STR_STATS_EMPTY_VALUE));
    }
  }

  snprintf(metricValues[GlobalSessions].data(), metricValues[GlobalSessions].size(), "%lu",
           static_cast<unsigned long>(globalStats.totalSessions));
  ReadingStatsStore::formatDuration(globalStats.totalReadingSeconds, metricValues[GlobalTime].data(),
                                    metricValues[GlobalTime].size());
  snprintf(metricValues[GlobalForwardPages].data(), metricValues[GlobalForwardPages].size(), "%lu",
           static_cast<unsigned long>(globalStats.totalPagesTurned));
  if (globalStats.totalSessions > 0) {
    ReadingStatsStore::formatDuration(globalStats.totalReadingSeconds / globalStats.totalSessions,
                                      metricValues[GlobalAverageSession].data(),
                                      metricValues[GlobalAverageSession].size());
  } else {
    snprintf(metricValues[GlobalAverageSession].data(), metricValues[GlobalAverageSession].size(), "%s",
             tr(STR_STATS_EMPTY_VALUE));
  }
  if (globalStats.totalReadingSeconds > 0) {
    const float pagesPerMinute =
        static_cast<float>(globalStats.totalPagesTurned) * 60.0f / globalStats.totalReadingSeconds;
    snprintf(metricValues[GlobalPace].data(), metricValues[GlobalPace].size(), "%.1f", pagesPerMinute);
  } else {
    snprintf(metricValues[GlobalPace].data(), metricValues[GlobalPace].size(), "%s", tr(STR_STATS_EMPTY_VALUE));
  }

  addRow(StrId::STR_STATS_BACKUP, ReadingStatsStore::hasGlobalBackup() ? tr(STR_STATS_BACKUP_READY) : "",
         RowAction::Backup);
  addRow(StrId::STR_STATS_RESTORE,
         ReadingStatsStore::hasGlobalBackup() ? tr(STR_STATS_BACKUP_READY) : tr(STR_STATS_NO_BACKUP),
         RowAction::Restore);
  if (!bookPath.empty()) addRow(StrId::STR_STATS_RESET_BOOK, bookTitle.c_str(), RowAction::ResetBook);
  addRow(StrId::STR_STATS_RESET_ALL, "", RowAction::ResetGlobal);
  nav.follow(rowCount);
}

void ReadingStatsActivity::showView(const ViewMode mode) {
  RenderLock lock(*this);
  viewMode = mode;
  closeRouting();
  app.clearTapFlash();
  nav.reset();
  nav.follow(listCount());
  requestUpdate(true);
}

void ReadingStatsActivity::confirmResetBook() {
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_STATS_RESET_BOOK),
                                                              tr(STR_STATS_RESET_BOOK_PROMPT));
  if (!confirmation) return;
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (!result.isCancelled && ReadingStatsStore::resetBook(bookPath)) {
      reload();
      requestUpdate(true);
    }
  });
}

void ReadingStatsActivity::confirmResetGlobal() {
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_STATS_RESET_ALL),
                                                              tr(STR_STATS_RESET_ALL_PROMPT));
  if (!confirmation) return;
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (!result.isCancelled && ReadingStatsStore::resetGlobal()) {
      reload();
      requestUpdate(true);
    }
  });
}

void ReadingStatsActivity::activateIndex(const int index) {
  if (index < 0 || index >= rowCount) return;
  switch (actions[index]) {
    case RowAction::Backup:
      ReadingStatsStore::backupGlobal();
      reload();
      requestUpdate(true);
      break;
    case RowAction::Restore:
      if (ReadingStatsStore::restoreGlobal()) {
        reload();
        requestUpdate(true);
      }
      break;
    case RowAction::ResetBook:
      confirmResetBook();
      break;
    case RowAction::ResetGlobal:
      confirmResetGlobal();
      break;
    case RowAction::None:
      break;
  }
}

void ReadingStatsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (viewMode == ViewMode::Summary) {
    drawSummary(screen);
    return;
  }

  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rowCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props, true);
  screen.list(props);
}

void ReadingStatsActivity::drawMetricSection(UiScreen& screen, const fui::Rect rect, const char* title,
                                             const StrId* labels, const uint8_t firstValue, const uint8_t count) {
  const auto& theme = screen.theme();
  fui::BoxStyle sectionStyle = theme.popup.resolve(fui::StateNormal);
  if (sectionStyle.border.kind == fui::PaintKind::None || sectionStyle.borderWidth == 0) {
    sectionStyle.border = sectionStyle.foreground;
    sectionStyle.borderWidth = 1;
  }
  screen.target().fill(rect, sectionStyle.background, sectionStyle.radius, sectionStyle.corners);
  if (sectionStyle.border.kind != fui::PaintKind::None && sectionStyle.borderWidth > 0) {
    screen.target().stroke(rect, sectionStyle.border, sectionStyle.borderWidth, sectionStyle.radius,
                           sectionStyle.corners);
  }

  const fui::Rect inner = rect.inset(fui::Insets{theme.spaceSm, theme.spaceSm, theme.spaceSm, theme.spaceSm});
  const int16_t titleHeight = screen.target().lineHeight(theme.bodyText.font);
  fui::TextStyle titleStyle = theme.bodyText;
  titleStyle.align = fui::TextAlign::Center;
  titleStyle.bold = true;
  screen.target().text(fui::Rect{inner.x, inner.y, inner.width, titleHeight}, title, titleStyle);
  const int16_t dividerY = static_cast<int16_t>(inner.y + titleHeight + theme.spaceSm);
  screen.target().line(fui::Point{inner.x, dividerY}, fui::Point{inner.right(), dividerY}, 1, sectionStyle.foreground);

  constexpr uint8_t FIRST_ROW_COUNT = 3;
  constexpr uint8_t ROW_COUNT = 2;
  const int16_t gridY = static_cast<int16_t>(dividerY + theme.spaceSm);
  const fui::Rect grid{inner.x, gridY, inner.width, static_cast<int16_t>(inner.bottom() - gridY)};
  const int16_t cellHeight = static_cast<int16_t>((grid.height - theme.spaceSm) / ROW_COUNT);
  const fui::StyleSet plain = fui::plainStyles(sectionStyle.foreground);

  for (uint8_t index = 0; index < count; index++) {
    const bool firstRow = index < FIRST_ROW_COUNT;
    const uint8_t columns = firstRow ? FIRST_ROW_COUNT : static_cast<uint8_t>(count - FIRST_ROW_COUNT);
    const uint8_t column = firstRow ? index : static_cast<uint8_t>(index - FIRST_ROW_COUNT);
    const uint8_t row = firstRow ? 0 : 1;
    const int16_t cellWidth = static_cast<int16_t>((grid.width - theme.spaceSm * (columns - 1)) / columns);
    const fui::Rect cell{static_cast<int16_t>(grid.x + column * (cellWidth + theme.spaceSm)),
                         static_cast<int16_t>(grid.y + row * (cellHeight + theme.spaceSm)), cellWidth, cellHeight};
    fui::MetricCardProps props;
    props.value = metricValues[firstValue + index].data();
    props.caption = I18N.get(labels[index]);
    props.valueText = theme.titleText;
    props.captionText = theme.smallText;
    props.styles = plain;
    props.padding = fui::Insets{theme.spaceXs, theme.spaceXs, theme.spaceXs, theme.spaceXs};
    props.gap = theme.spaceXs;
    fui::metricCard(screen.frame(), cell, props);
  }
}

void ReadingStatsActivity::drawSummary(UiScreen& screen) {
  const auto& theme = screen.theme();
  const fui::Rect body = screen.body();
  const int16_t titleHeight = screen.target().lineHeight(theme.bodyText.font);
  const int16_t valueHeight = screen.target().lineHeight(theme.titleText.font);
  const int16_t labelHeight = screen.target().lineHeight(theme.smallText.font);
  const int16_t cellHeight = static_cast<int16_t>(valueHeight + labelHeight + theme.spaceSm * 2);
  const int16_t sectionHeight = static_cast<int16_t>(titleHeight + theme.spaceSm * 4 + cellHeight * 2 + theme.spaceSm);
  if (bookPath.empty()) {
    const fui::Rect card{body.x, body.y, body.width, std::min(body.height, sectionHeight)};
    drawMetricSection(screen, card, tr(STR_STATS_ALL_BOOKS), GLOBAL_METRIC_LABELS.data(), GlobalSessions,
                      GLOBAL_METRIC_LABELS.size());
    return;
  }

  const int16_t availableCardHeight = std::max<int16_t>(0, static_cast<int16_t>((body.height - theme.spaceMd) / 2));
  const int16_t cardHeight = std::min(sectionHeight, availableCardHeight);
  const int16_t stackHeight = static_cast<int16_t>(cardHeight * 2 + theme.spaceMd);
  const fui::Rect cardArea{body.x, body.y, body.width, stackHeight};
  fui::Stack<2> sections(cardArea, fui::Axis::Column, theme.spaceMd);
  sections.fixed(cardHeight);
  sections.fixed(cardHeight);
  sections.layout();
  drawMetricSection(screen, sections.rect(0), bookTitle.empty() ? tr(STR_STATS_THIS_BOOK) : bookTitle.c_str(),
                    BOOK_METRIC_LABELS.data(), BookSessions, BOOK_METRIC_LABELS.size());
  drawMetricSection(screen, sections.rect(1), tr(STR_STATS_ALL_BOOKS), GLOBAL_METRIC_LABELS.data(), GlobalSessions,
                    GLOBAL_METRIC_LABELS.size());
}

bool ReadingStatsActivity::handleButtons() {
  if (viewMode == ViewMode::Summary) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return true;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      showView(ViewMode::Manage);
      return true;
    }
    return false;
  }
  return UiListActivity::handleButtons();
}

void ReadingStatsActivity::onBackButton() {
  if (viewMode == ViewMode::Manage) {
    showView(ViewMode::Summary);
    return;
  }
  finish();
}

void ReadingStatsActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight}, headerTitle());
}

void ReadingStatsActivity::drawFooter() {
  const auto labels = viewMode == ViewMode::Summary
                          ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_STATS_MANAGE), "", "")
                          : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
