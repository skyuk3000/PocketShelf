#pragma once

#include <cstddef>

namespace romm3ds::ui {

class SaveSelectionState {
public:
    void setCounts(std::size_t local_count, std::size_t remote_count) noexcept;

    std::size_t localIndex() const noexcept { return local_index_; }
    std::size_t remoteIndex() const noexcept { return remote_index_; }
    std::size_t localCount() const noexcept { return local_count_; }
    std::size_t remoteCount() const noexcept { return remote_count_; }

    void previousLocal() noexcept;
    void nextLocal() noexcept;
    void previousRemote() noexcept;
    void nextRemote() noexcept;

    std::size_t localWindowStart(std::size_t visible_rows) const noexcept;
    std::size_t remoteWindowStart(std::size_t visible_rows) const noexcept;
    bool selectLocalVisibleRow(std::size_t row, std::size_t visible_rows) noexcept;
    bool selectRemoteVisibleRow(std::size_t row, std::size_t visible_rows) noexcept;

private:
    static std::size_t windowStart(std::size_t selected, std::size_t count,
                                   std::size_t visible_rows) noexcept;
    static bool selectVisible(std::size_t row, std::size_t visible_rows,
                              std::size_t count, std::size_t& selected) noexcept;

    std::size_t local_count_ = 0;
    std::size_t remote_count_ = 0;
    std::size_t local_index_ = 0;
    std::size_t remote_index_ = 0;
};

} // namespace romm3ds::ui
