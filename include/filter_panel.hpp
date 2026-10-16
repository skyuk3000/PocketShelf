#pragma once

#include <cstddef>

namespace romm3ds::ui {

enum class FilterControl : std::size_t {
    Type = 0,
    Search,
    Platform,
    Region,
};

class FilterPanelState {
public:
    void activate() noexcept { active_ = true; }
    void cancel() noexcept { active_ = false; }
    bool active() const noexcept { return active_; }
    FilterControl focused() const noexcept { return focused_; }
    bool focusedEnabled() const noexcept;
    void left() noexcept;
    void right() noexcept;

private:
    static constexpr std::size_t kControlCount = 4;
    FilterControl focused_ = FilterControl::Type;
    bool active_ = false;
};

const char* filterControlName(FilterControl control) noexcept;
bool filterControlEnabled(FilterControl control) noexcept;

} // namespace romm3ds::ui
