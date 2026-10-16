#include "bottom_navigation.hpp"
#include "catalog_parser.hpp"
#include "ui_limits.hpp"

namespace romm3ds::ui {
namespace {

struct Rect {
    int x;
    int y;
    int width;
    int height;

    bool contains(int px, int py) const noexcept {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

constexpr Rect kRailTargets[] = {
    {kBottomRailX, kBottomRailY, kBottomRailWidth, kBottomRailHeight},
    {kBottomRailX, kBottomRailY + kBottomRailStride, kBottomRailWidth, kBottomRailHeight},
    {kBottomRailX, kBottomRailY + 2 * kBottomRailStride, kBottomRailWidth, kBottomRailHeight},
    {kBottomRailX, kBottomRailY + 3 * kBottomRailStride, kBottomRailWidth, kBottomRailHeight},
};

BottomControl firstControl(BottomPage page, DetailsView details_view) noexcept {
    switch (page) {
        case BottomPage::Details:
            if (details_view == DetailsView::Screenshots) return BottomControl::ScreenshotsPrevious;
            if (details_view == DetailsView::Manage) return BottomControl::ManageNowPlaying;
            return BottomControl::DetailsScreenshots;
        case BottomPage::Filters: return BottomControl::FilterType;
        case BottomPage::Sort: return BottomControl::SortTitle;
        case BottomPage::Settings: return BottomControl::SettingsUrl;
    }
    return BottomControl::None;
}

BottomControl controlAt(BottomPage page, DetailsView details_view, int x, int y) noexcept {
    switch (page) {
        case BottomPage::Details:
            if (details_view == DetailsView::Info) {
                const int right_x = kBottomContentX + kDetailsActionWidth + kDetailsActionGap;
                if (Rect{kBottomContentX, kDetailsActionsY, kDetailsActionWidth,
                         kDetailsActionHeight}.contains(x, y)) return BottomControl::DetailsSaveData;
                if (Rect{right_x, kDetailsActionsY, kDetailsActionWidth,
                         kDetailsActionHeight}.contains(x, y)) return BottomControl::DetailsManage;
                if (Rect{kBottomContentX, kDetailsActionsY + kDetailsActionStride,
                         kDetailsActionWidth, kDetailsActionHeight}.contains(x, y))
                    return BottomControl::DetailsScreenshots;
                if (Rect{right_x, kDetailsActionsY + kDetailsActionStride,
                         kDetailsActionWidth, kDetailsActionHeight}.contains(x, y))
                    return BottomControl::DetailsTransfer;
                return BottomControl::None;
            }
            if (details_view == DetailsView::Screenshots) {
                if (Rect{kBottomContentX, kScreenshotsActionsY, kDetailsActionWidth,
                         kDetailsActionHeight}.contains(x, y)) return BottomControl::ScreenshotsPrevious;
                const int right_x = kBottomContentX + kDetailsActionWidth + kDetailsActionGap;
                if (Rect{right_x, kScreenshotsActionsY, kDetailsActionWidth,
                         kDetailsActionHeight}.contains(x, y)) return BottomControl::ScreenshotsNext;
                if (Rect{kBottomContentX, kScreenshotsBackY, kBottomContentWidth,
                         kDetailsActionHeight}.contains(x, y)) return BottomControl::DetailsBack;
                return BottomControl::None;
            }
            for (std::size_t index = 0; index < 7; ++index) {
                const int row_y = kManageRowY + static_cast<int>(index) * kManageRowStride;
                if (Rect{kBottomContentX, row_y, kBottomContentWidth,
                         kManageRowHeight}.contains(x, y)) {
                    return static_cast<BottomControl>(
                        static_cast<std::size_t>(BottomControl::ManageNowPlaying) + index);
                }
            }
            if (Rect{kBottomContentX, kManageBackY, kBottomContentWidth,
                     kManageRowHeight}.contains(x, y)) return BottomControl::DetailsBack;
            return BottomControl::None;
        case BottomPage::Filters:
            if (Rect{kBottomContentX, kFilterTypeY, kBottomContentWidth,
                     kFilterCardHeight}.contains(x, y)) return BottomControl::FilterType;
            if (Rect{kBottomContentX, kFilterSearchY, kBottomContentWidth,
                     kFilterCardHeight}.contains(x, y)) return BottomControl::FilterSearch;
            if (Rect{kBottomContentX, kFilterMetadataY, kBottomContentWidth,
                     kFilterCardHeight}.contains(x, y)) return BottomControl::FilterMetadata;
            return BottomControl::None;
        case BottomPage::Sort:
            return Rect{kBottomContentX, kSortCardY, kBottomContentWidth,
                        kSortCardHeight}.contains(x, y) ?
                BottomControl::SortTitle : BottomControl::None;
        case BottomPage::Settings:
            if (Rect{kBottomContentX, kSettingsUrlY, kBottomContentWidth,
                     kSettingsCardHeight}.contains(x, y)) return BottomControl::SettingsUrl;
            if (Rect{kBottomContentX, kSettingsTokenY, kBottomContentWidth,
                     kSettingsCardHeight}.contains(x, y)) return BottomControl::SettingsToken;
            if (Rect{kBottomContentX, kSettingsThemeY, kBottomContentWidth,
                     kSettingsCardHeight}.contains(x, y)) return BottomControl::SettingsTheme;
            if (Rect{kBottomContentX, kSettingsConnectY, kBottomContentWidth,
                     kSettingsCardHeight}.contains(x, y)) return BottomControl::SettingsConnect;
            if (Rect{kBottomContentX, kSettingsUpdateY, kBottomContentWidth,
                     kSettingsCardHeight}.contains(x, y)) return BottomControl::SettingsUpdate;
            return BottomControl::None;
    }
    return BottomControl::None;
}

} // namespace

void BottomNavigationState::setPage(BottomPage page) noexcept {
    page_ = page;
    if (page_ == BottomPage::Details) details_view_ = DetailsView::Info;
    focused_ = firstControl(page_, details_view_);
}

void BottomNavigationState::openDetailsView(DetailsView view) noexcept {
    page_ = BottomPage::Details;
    details_view_ = view;
    focused_ = firstControl(page_, details_view_);
}

void BottomNavigationState::next() noexcept {
    switch (page_) {
        case BottomPage::Details:
            if (details_view_ == DetailsView::Info) {
                if (focused_ == BottomControl::DetailsScreenshots) focused_ = BottomControl::DetailsManage;
                else if (focused_ == BottomControl::DetailsManage) focused_ = BottomControl::DetailsSaveData;
                else if (focused_ == BottomControl::DetailsSaveData) focused_ = BottomControl::DetailsTransfer;
                else focused_ = BottomControl::DetailsScreenshots;
            } else if (details_view_ == DetailsView::Screenshots) {
                if (focused_ == BottomControl::ScreenshotsPrevious) focused_ = BottomControl::ScreenshotsNext;
                else if (focused_ == BottomControl::ScreenshotsNext) focused_ = BottomControl::DetailsBack;
                else focused_ = BottomControl::ScreenshotsPrevious;
            } else {
                if (focused_ == BottomControl::DetailsBack) focused_ = BottomControl::ManageNowPlaying;
                else if (focused_ == BottomControl::ManageCompletion) focused_ = BottomControl::DetailsBack;
                else focused_ = static_cast<BottomControl>(static_cast<std::size_t>(focused_) + 1);
            }
            break;
        case BottomPage::Sort:
            break;
        case BottomPage::Filters:
            if (focused_ == BottomControl::FilterType) focused_ = BottomControl::FilterSearch;
            else if (focused_ == BottomControl::FilterSearch) focused_ = BottomControl::FilterMetadata;
            else focused_ = BottomControl::FilterType;
            break;
        case BottomPage::Settings:
            if (focused_ == BottomControl::SettingsUrl) focused_ = BottomControl::SettingsToken;
            else if (focused_ == BottomControl::SettingsToken) focused_ = BottomControl::SettingsTheme;
            else if (focused_ == BottomControl::SettingsTheme) focused_ = BottomControl::SettingsConnect;
            else if (focused_ == BottomControl::SettingsConnect) focused_ = BottomControl::SettingsUpdate;
            else focused_ = BottomControl::SettingsUrl;
            break;
    }
}

void BottomNavigationState::previous() noexcept {
    switch (page_) {
        case BottomPage::Details:
            if (details_view_ == DetailsView::Info) {
                if (focused_ == BottomControl::DetailsScreenshots) focused_ = BottomControl::DetailsTransfer;
                else if (focused_ == BottomControl::DetailsTransfer) focused_ = BottomControl::DetailsSaveData;
                else if (focused_ == BottomControl::DetailsSaveData) focused_ = BottomControl::DetailsManage;
                else focused_ = BottomControl::DetailsScreenshots;
            } else if (details_view_ == DetailsView::Screenshots) {
                if (focused_ == BottomControl::ScreenshotsPrevious) focused_ = BottomControl::DetailsBack;
                else if (focused_ == BottomControl::DetailsBack) focused_ = BottomControl::ScreenshotsNext;
                else focused_ = BottomControl::ScreenshotsPrevious;
            } else {
                if (focused_ == BottomControl::ManageNowPlaying) focused_ = BottomControl::DetailsBack;
                else if (focused_ == BottomControl::DetailsBack) focused_ = BottomControl::ManageCompletion;
                else focused_ = static_cast<BottomControl>(static_cast<std::size_t>(focused_) - 1);
            }
            break;
        case BottomPage::Sort:
            break;
        case BottomPage::Filters:
            if (focused_ == BottomControl::FilterType) focused_ = BottomControl::FilterMetadata;
            else if (focused_ == BottomControl::FilterMetadata) focused_ = BottomControl::FilterSearch;
            else focused_ = BottomControl::FilterType;
            break;
        case BottomPage::Settings:
            if (focused_ == BottomControl::SettingsUrl) focused_ = BottomControl::SettingsUpdate;
            else if (focused_ == BottomControl::SettingsUpdate) focused_ = BottomControl::SettingsConnect;
            else if (focused_ == BottomControl::SettingsConnect) focused_ = BottomControl::SettingsTheme;
            else if (focused_ == BottomControl::SettingsTheme) focused_ = BottomControl::SettingsToken;
            else focused_ = BottomControl::SettingsUrl;
            break;
    }
}

BottomTouchAction BottomNavigationState::touch(int x, int y) noexcept {
    for (std::size_t i = 0; i < sizeof(kRailTargets) / sizeof(kRailTargets[0]); ++i) {
        if (kRailTargets[i].contains(x, y)) {
            setPage(static_cast<BottomPage>(i));
            return {BottomTouchKind::PageChanged, page_, focused_};
        }
    }
    const BottomControl control = controlAt(page_, details_view_, x, y);
    if (control != BottomControl::None) {
        focused_ = control;
        return {BottomTouchKind::ControlActivated, page_, control};
    }
    return {BottomTouchKind::None, page_, BottomControl::None};
}

void ScreenshotSelectionState::setCount(std::size_t count) noexcept {
    count_ = count > kMaxScreenshots ? kMaxScreenshots : count;
    index_ = 0;
}

void ScreenshotSelectionState::next() noexcept {
    if (count_ != 0) index_ = (index_ + 1) % count_;
}

void ScreenshotSelectionState::previous() noexcept {
    if (count_ != 0) index_ = index_ == 0 ? count_ - 1 : index_ - 1;
}

void DetailsScrollState::setLineCounts(std::size_t content_lines,
                                       std::size_t visible_lines) noexcept {
    max_offset_ = content_lines > visible_lines ? content_lines - visible_lines : 0;
    if (offset_ > max_offset_) offset_ = max_offset_;
}

void DetailsScrollState::reset() noexcept {
    offset_ = 0;
    max_offset_ = 0;
    touching_ = false;
}

void DetailsScrollState::scrollBy(int lines) noexcept {
    if (lines < 0) {
        const std::size_t amount = static_cast<std::size_t>(-lines);
        offset_ = amount > offset_ ? 0 : offset_ - amount;
    } else {
        const std::size_t amount = static_cast<std::size_t>(lines);
        offset_ = amount > max_offset_ - offset_ ? max_offset_ : offset_ + amount;
    }
}

void DetailsScrollState::beginTouch(int y) noexcept {
    touch_y_ = y;
    touching_ = true;
}

bool DetailsScrollState::dragTouch(int y, int pixels_per_line) noexcept {
    if (!touching_ || pixels_per_line <= 0) return false;
    const int delta = touch_y_ - y;
    const int lines = delta / pixels_per_line;
    if (lines == 0) return false;
    const std::size_t before = offset_;
    scrollBy(lines);
    touch_y_ -= lines * pixels_per_line;
    return offset_ != before;
}

const char* bottomPageName(BottomPage page) noexcept {
    switch (page) {
        case BottomPage::Details: return "INFO";
        case BottomPage::Filters: return "FIND";
        case BottomPage::Sort: return "SORT";
        case BottomPage::Settings: return "SETUP";
    }
    return "PAGE";
}

const char* bottomPageGlyph(BottomPage page) noexcept {
    switch (page) {
        case BottomPage::Details: return "i";
        case BottomPage::Filters: return "F";
        case BottomPage::Sort: return "AZ";
        case BottomPage::Settings: return "S";
    }
    return "?";
}

std::size_t typeChoiceAt(int x, int y, std::size_t choice_count) noexcept {
    if (x < 58 || x >= 310 || y < kTypeChooserY || choice_count == 0) return kNoTypeChoice;
    const std::size_t index = static_cast<std::size_t>((y - kTypeChooserY) /
                                                       kTypeChooserStride);
    if (index >= choice_count) return kNoTypeChoice;
    const int row_y = kTypeChooserY + static_cast<int>(index) * kTypeChooserStride;
    return y < row_y + kTypeChooserHeight ? index : kNoTypeChoice;
}

} // namespace romm3ds::ui
