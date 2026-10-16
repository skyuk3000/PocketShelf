#pragma once

#include <cstddef>

namespace romm3ds::ui {

enum class BottomPage : std::size_t {
    Details = 0,
    Filters,
    Sort,
    Settings,
};

enum class DetailsView : std::size_t {
    Info = 0,
    Screenshots,
    Manage,
};

enum class BottomControl : std::size_t {
    None = 0,
    DetailsScreenshots,
    DetailsManage,
    DetailsTransfer,
    DetailsSaveData,
    ScreenshotsPrevious,
    ScreenshotsNext,
    ManageNowPlaying,
    ManageLastPlayed,
    ManageBacklogged,
    ManagePlayStatus,
    ManageRating,
    ManageDifficulty,
    ManageCompletion,
    DetailsBack,
    FilterType,
    FilterSearch,
    FilterMetadata,
    SortTitle,
    SettingsUrl,
    SettingsToken,
    SettingsConnect,
    SettingsTheme,
    SettingsUpdate,
};

enum class BottomTouchKind {
    None,
    PageChanged,
    ControlActivated,
};

struct BottomTouchAction {
    BottomTouchKind kind = BottomTouchKind::None;
    BottomPage page = BottomPage::Details;
    BottomControl control = BottomControl::None;
};

class BottomNavigationState {
public:
    BottomPage page() const noexcept { return page_; }
    DetailsView detailsView() const noexcept { return details_view_; }
    BottomControl focused() const noexcept { return focused_; }
    void setPage(BottomPage page) noexcept;
    void openDetailsView(DetailsView view) noexcept;
    void next() noexcept;
    void previous() noexcept;
    BottomTouchAction touch(int x, int y) noexcept;

private:
    BottomPage page_ = BottomPage::Details;
    DetailsView details_view_ = DetailsView::Info;
    BottomControl focused_ = BottomControl::DetailsScreenshots;
};

class ScreenshotSelectionState {
public:
    void setCount(std::size_t count) noexcept;
    std::size_t count() const noexcept { return count_; }
    std::size_t index() const noexcept { return index_; }
    bool empty() const noexcept { return count_ == 0; }
    void next() noexcept;
    void previous() noexcept;
private:
    std::size_t count_ = 0;
    std::size_t index_ = 0;
};

class DetailsScrollState {
public:
    std::size_t offset() const noexcept { return offset_; }
    std::size_t maxOffset() const noexcept { return max_offset_; }
    void setLineCounts(std::size_t content_lines, std::size_t visible_lines) noexcept;
    void reset() noexcept;
    void scrollBy(int lines) noexcept;
    void beginTouch(int y) noexcept;
    bool dragTouch(int y, int pixels_per_line) noexcept;
    void endTouch() noexcept { touching_ = false; }

private:
    std::size_t offset_ = 0;
    std::size_t max_offset_ = 0;
    int touch_y_ = 0;
    bool touching_ = false;
};

const char* bottomPageName(BottomPage page) noexcept;
const char* bottomPageGlyph(BottomPage page) noexcept;
constexpr std::size_t kNoTypeChoice = static_cast<std::size_t>(-1);
std::size_t typeChoiceAt(int x, int y, std::size_t choice_count) noexcept;

} // namespace romm3ds::ui
