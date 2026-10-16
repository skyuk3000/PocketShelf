#pragma once

#include "settings.hpp"

#include <cstddef>
#include <string_view>

namespace romm3ds::ui {

inline constexpr const auto& kTypeChoices = romm3ds::kTypeFilterChoices;

class TypeChooserState {
public:
    static constexpr std::size_t kVisibleRows = 7;
    explicit TypeChooserState(std::string_view current) noexcept;
    void up() noexcept;
    void down() noexcept;
    bool select(std::size_t index) noexcept;
    bool selectVisible(std::size_t row) noexcept;
    void beginTouch(int y) noexcept;
    bool dragTouch(int y, int pixels_per_row) noexcept;
    bool endTouch(std::size_t row) noexcept;
    std::size_t selected() const noexcept { return selected_; }
    std::size_t firstVisible() const noexcept { return first_visible_; }
    std::size_t visibleCount() const noexcept;
    std::string_view value() const noexcept { return kTypeChoices[selected_]; }
    static constexpr std::size_t count() noexcept {
        return kTypeChoices.size();
    }
private:
    void scrollBy(int rows) noexcept;
    void ensureSelectedVisible() noexcept;
    std::size_t selected_ = 0;
    std::size_t first_visible_ = 0;
    int touch_y_ = 0;
    bool touching_ = false;
    bool touch_dragged_ = false;
};

} // namespace romm3ds::ui