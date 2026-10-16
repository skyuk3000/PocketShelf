#include "filter_panel.hpp"

namespace romm3ds::ui {

bool filterControlEnabled(FilterControl control) noexcept {
    return control == FilterControl::Type || control == FilterControl::Search;
}

const char* filterControlName(FilterControl control) noexcept {
    switch (control) {
        case FilterControl::Type: return "TYPE";
        case FilterControl::Search: return "SEARCH";
        case FilterControl::Platform: return "PLATFORM";
        case FilterControl::Region: return "REGION";
    }
    return "FILTER";
}

bool FilterPanelState::focusedEnabled() const noexcept {
    return filterControlEnabled(focused_);
}

void FilterPanelState::left() noexcept {
    const auto current = static_cast<std::size_t>(focused_);
    focused_ = static_cast<FilterControl>((current + kControlCount - 1) % kControlCount);
}

void FilterPanelState::right() noexcept {
    const auto current = static_cast<std::size_t>(focused_);
    focused_ = static_cast<FilterControl>((current + 1) % kControlCount);
}

} // namespace romm3ds::ui
