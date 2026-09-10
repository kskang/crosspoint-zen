#pragma once

#include <I18n.h>

#include <array>
#include <string>

#include "ReadingStats.h"
#include "activities/UiListActivity.h"

class ReadingStatsActivity final : public UiListActivity {
  enum class ViewMode : uint8_t { Summary, Manage };
  enum class RowAction : uint8_t { None, Backup, Restore, ResetBook, ResetGlobal };
  enum MetricValue : uint8_t {
    BookSessions,
    BookTime,
    BookForwardPages,
    BookAverageSession,
    BookPace,
    BookTimeLeft,
    GlobalSessions,
    GlobalTime,
    GlobalForwardPages,
    GlobalAverageSession,
    GlobalPace,
    MetricValueCount,
  };
  static constexpr size_t MAX_ROWS = 4;

  std::string bookPath;
  std::string bookTitle;
  BookReadingStats bookStats;
  GlobalReadingStats globalStats;
  std::array<freeink::ui::ListItem, MAX_ROWS> rows{};
  std::array<RowAction, MAX_ROWS> actions{};
  std::array<std::array<char, 40>, MetricValueCount> metricValues{};
  int rowCount = 0;
  ViewMode viewMode = ViewMode::Summary;

  void reload();
  void addRow(StrId label, const char* value, RowAction action = RowAction::None);
  void showView(ViewMode mode);
  void drawSummary(UiScreen& screen);
  void drawMetricSection(UiScreen& screen, freeink::ui::Rect rect, const char* title, const StrId* labels,
                         uint8_t firstValue, uint8_t count);
  void confirmResetBook();
  void confirmResetGlobal();

  int listCount() const override { return viewMode == ViewMode::Manage ? rowCount : 0; }
  const char* headerTitle() const override { return tr(STR_READING_STATS); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  void onBackButton() override;
  void drawChrome() override;
  void drawFooter() override;

 public:
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath = {},
                       std::string bookTitle = {});
  void onEnter() override;
};
