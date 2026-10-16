#include "save_selection.hpp"

#include <algorithm>

namespace romm3ds::ui {

void SaveSelectionState::setCounts(std::size_t local_count,
                                   std::size_t remote_count) noexcept {
    local_count_ = local_count;
    remote_count_ = remote_count;
    local_index_ = local_count_ == 0 ? 0 : std::min(local_index_, local_count_ - 1);
    remote_index_ = remote_count_ == 0 ? 0 : std::min(remote_index_, remote_count_ - 1);
}

void SaveSelectionState::previousLocal() noexcept {
    if (local_index_ != 0) --local_index_;
}

void SaveSelectionState::nextLocal() noexcept {
    if (local_index_ + 1 < local_count_) ++local_index_;
}

void SaveSelectionState::previousRemote() noexcept {
    if (remote_index_ != 0) --remote_index_;
}

void SaveSelectionState::nextRemote() noexcept {
    if (remote_index_ + 1 < remote_count_) ++remote_index_;
}

std::size_t SaveSelectionState::windowStart(std::size_t selected, std::size_t count,
                                            std::size_t visible_rows) noexcept {
    if (count == 0 || visible_rows == 0 || count <= visible_rows) return 0;
    const std::size_t maximum_start = count - visible_rows;
    const std::size_t centered = selected == 0 ? 0 : selected - 1;
    return std::min(centered, maximum_start);
}

std::size_t SaveSelectionState::localWindowStart(std::size_t visible_rows) const noexcept {
    return windowStart(local_index_, local_count_, visible_rows);
}

std::size_t SaveSelectionState::remoteWindowStart(std::size_t visible_rows) const noexcept {
    return windowStart(remote_index_, remote_count_, visible_rows);
}

bool SaveSelectionState::selectVisible(std::size_t row, std::size_t visible_rows,
                                       std::size_t count, std::size_t& selected) noexcept {
    if (row >= visible_rows || count == 0) return false;
    const std::size_t index = windowStart(selected, count, visible_rows) + row;
    if (index >= count) return false;
    selected = index;
    return true;
}

bool SaveSelectionState::selectLocalVisibleRow(std::size_t row,
                                                std::size_t visible_rows) noexcept {
    return selectVisible(row, visible_rows, local_count_, local_index_);
}

bool SaveSelectionState::selectRemoteVisibleRow(std::size_t row,
                                                 std::size_t visible_rows) noexcept {
    return selectVisible(row, visible_rows, remote_count_, remote_index_);
}

} // namespace romm3ds::ui
