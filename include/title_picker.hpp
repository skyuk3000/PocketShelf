#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace romm3ds::ui {

inline constexpr std::size_t kMaxInstalledTitlesForPicker = 512;
inline constexpr std::size_t kMaxTitlePickerCandidates = 128;
inline constexpr std::size_t kTitlePickerVisibleRows = 7;
inline constexpr std::size_t kNoTitlePickerRow = static_cast<std::size_t>(-1);

struct InstalledTitleChoice {
    std::uint64_t title_id = 0;
    std::uint64_t installed_size = 0;
    std::uint16_t version = 0;
};

struct CheckpointTitleChoice {
    std::uint32_t unique_id = 0;
    std::string display_name;
    std::string directory;
};

struct TitlePickerCandidate {
    std::uint64_t title_id = 0;
    std::uint64_t installed_size = 0;
    std::uint16_t version = 0;
    std::string display_name;
    std::string checkpoint_directory;
};

bool isInstalledApplicationTitle(std::uint64_t title_id) noexcept;

bool buildTitlePickerCandidates(const std::vector<InstalledTitleChoice>& installed,
                                const std::vector<CheckpointTitleChoice>& checkpoint,
                                std::vector<TitlePickerCandidate>& output);

std::size_t titlePickerRowAt(int x, int y, std::size_t visible_count) noexcept;

class TitlePickerState {
public:
    explicit TitlePickerState(std::vector<TitlePickerCandidate> candidates,
                              std::uint64_t current_title_id = 0);

    bool empty() const noexcept { return candidates_.empty(); }
    std::size_t size() const noexcept { return candidates_.size(); }
    std::size_t selected() const noexcept { return selected_; }
    std::size_t firstVisible() const noexcept { return first_visible_; }
    std::size_t visibleCount() const noexcept;
    const TitlePickerCandidate* current() const noexcept;
    const TitlePickerCandidate* visible(std::size_t row) const noexcept;

    void up() noexcept;
    void down() noexcept;
    bool selectVisible(std::size_t row) noexcept;

private:
    void revealSelection() noexcept;

    std::vector<TitlePickerCandidate> candidates_;
    std::size_t selected_ = 0;
    std::size_t first_visible_ = 0;
};

} // namespace romm3ds::ui
