#include "type_chooser.hpp"

namespace romm3ds::ui {

TypeChooserState::TypeChooserState(std::string_view current) noexcept {
    for (std::size_t i = 0; i < count(); ++i) {
        if (kTypeChoices[i] == current) {
            selected_ = i;
            break;
        }
    }
    ensureSelectedVisible();
}

void TypeChooserState::up() noexcept {
    if (selected_ != 0) --selected_;
    ensureSelectedVisible();
}

void TypeChooserState::down() noexcept {
    if (selected_ + 1 < count()) ++selected_;
    ensureSelectedVisible();
}

bool TypeChooserState::select(std::size_t index) noexcept {
    if (index >= count()) return false;
    selected_ = index;
    ensureSelectedVisible();
    return true;
}

bool TypeChooserState::selectVisible(std::size_t row) noexcept {
    if (row >= visibleCount()) return false;
    return select(first_visible_ + row);
}

std::size_t TypeChooserState::visibleCount() const noexcept {
    const std::size_t remaining = count() - first_visible_;
    return remaining < kVisibleRows ? remaining : kVisibleRows;
}

void TypeChooserState::beginTouch(int y) noexcept {
    touch_y_ = y;
    touching_ = true;
    touch_dragged_ = false;
}

bool TypeChooserState::dragTouch(int y, int pixels_per_row) noexcept {
    if (!touching_ || pixels_per_row <= 0) return false;
    const int delta = touch_y_ - y;
    const int rows = delta / pixels_per_row;
    if (rows == 0) return false;
    const std::size_t before = selected_;
    scrollBy(rows);
    touch_y_ -= rows * pixels_per_row;
    touch_dragged_ = true;
    return selected_ != before;
}

bool TypeChooserState::endTouch(std::size_t row) noexcept {
    if (!touching_) return false;
    touching_ = false;
    if (touch_dragged_) return false;
    return selectVisible(row);
}

void TypeChooserState::scrollBy(int rows) noexcept {
    if (rows > 0) {
        const std::size_t distance = static_cast<std::size_t>(rows);
        const std::size_t remaining = count() - 1 - selected_;
        selected_ += distance < remaining ? distance : remaining;
    } else if (rows < 0) {
        const std::size_t distance = static_cast<std::size_t>(-rows);
        selected_ -= distance < selected_ ? distance : selected_;
    }
    ensureSelectedVisible();
}

void TypeChooserState::ensureSelectedVisible() noexcept {
    if (selected_ < first_visible_) first_visible_ = selected_;
    if (selected_ >= first_visible_ + kVisibleRows) {
        first_visible_ = selected_ - kVisibleRows + 1;
    }
}

} // namespace romm3ds::ui