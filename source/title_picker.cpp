#include "title_picker.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace romm3ds::ui {
namespace {

std::uint32_t titleUniqueId(std::uint64_t title_id) noexcept {
    return static_cast<std::uint32_t>(title_id) >> 8;
}

bool nameLess(const TitlePickerCandidate& left, const TitlePickerCandidate& right) {
    const auto folded = [](const std::string& value) {
        std::string result = value;
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return result;
    };
    const std::string left_name = folded(left.display_name);
    const std::string right_name = folded(right.display_name);
    if (left_name != right_name) return left_name < right_name;
    return left.title_id < right.title_id;
}

} // namespace

bool isInstalledApplicationTitle(std::uint64_t title_id) noexcept {
    return (title_id >> 32) == 0x00040000ULL && title_id != 0;
}

bool buildTitlePickerCandidates(const std::vector<InstalledTitleChoice>& installed,
                                const std::vector<CheckpointTitleChoice>& checkpoint,
                                std::vector<TitlePickerCandidate>& output) {
    output.clear();
    std::map<std::uint32_t, std::size_t> folder_counts;
    std::map<std::uint32_t, const CheckpointTitleChoice*> folders;
    for (const auto& item : checkpoint) {
        if (item.unique_id == 0 || item.display_name.empty() || item.directory.empty()) continue;
        ++folder_counts[item.unique_id];
        folders[item.unique_id] = &item;
    }

    std::set<std::uint64_t> seen;
    for (const auto& item : installed) {
        if (!isInstalledApplicationTitle(item.title_id) || !seen.insert(item.title_id).second) continue;
        const std::uint32_t unique_id = titleUniqueId(item.title_id);
        const auto count = folder_counts.find(unique_id);
        const auto folder = folders.find(unique_id);
        if (count == folder_counts.end() || count->second != 1 || folder == folders.end()) continue;
        if (output.size() >= kMaxTitlePickerCandidates) {
            output.clear();
            return false;
        }
        output.push_back(TitlePickerCandidate{
            item.title_id,
            item.installed_size,
            item.version,
            folder->second->display_name,
            folder->second->directory,
        });
    }
    std::sort(output.begin(), output.end(), nameLess);
    return true;
}

std::size_t titlePickerRowAt(int x, int y, std::size_t visible_count) noexcept {
    constexpr int kLeft = 12;
    constexpr int kRight = 308;
    constexpr int kTop = 42;
    constexpr int kRowHeight = 21;
    if (x < kLeft || x >= kRight || y < kTop) return kNoTitlePickerRow;
    const std::size_t row = static_cast<std::size_t>((y - kTop) / kRowHeight);
    if (row >= visible_count || row >= kTitlePickerVisibleRows) return kNoTitlePickerRow;
    return row;
}

TitlePickerState::TitlePickerState(std::vector<TitlePickerCandidate> candidates,
                                   std::uint64_t current_title_id)
    : candidates_(std::move(candidates)) {
    if (candidates_.size() > kMaxTitlePickerCandidates)
        candidates_.resize(kMaxTitlePickerCandidates);
    const auto current = std::find_if(candidates_.begin(), candidates_.end(),
        [current_title_id](const auto& candidate) { return candidate.title_id == current_title_id; });
    if (current != candidates_.end())
        selected_ = static_cast<std::size_t>(current - candidates_.begin());
    revealSelection();
}

std::size_t TitlePickerState::visibleCount() const noexcept {
    if (first_visible_ >= candidates_.size()) return 0;
    return std::min(kTitlePickerVisibleRows, candidates_.size() - first_visible_);
}

const TitlePickerCandidate* TitlePickerState::current() const noexcept {
    return selected_ < candidates_.size() ? &candidates_[selected_] : nullptr;
}

const TitlePickerCandidate* TitlePickerState::visible(std::size_t row) const noexcept {
    if (row >= visibleCount()) return nullptr;
    return &candidates_[first_visible_ + row];
}

void TitlePickerState::up() noexcept {
    if (selected_ != 0) --selected_;
    revealSelection();
}

void TitlePickerState::down() noexcept {
    if (selected_ + 1 < candidates_.size()) ++selected_;
    revealSelection();
}

bool TitlePickerState::selectVisible(std::size_t row) noexcept {
    if (row >= visibleCount()) return false;
    selected_ = first_visible_ + row;
    revealSelection();
    return true;
}

void TitlePickerState::revealSelection() noexcept {
    if (candidates_.empty()) {
        selected_ = 0;
        first_visible_ = 0;
        return;
    }
    if (selected_ >= candidates_.size()) selected_ = candidates_.size() - 1;
    if (selected_ < first_visible_) first_visible_ = selected_;
    if (selected_ >= first_visible_ + kTitlePickerVisibleRows)
        first_visible_ = selected_ - kTitlePickerVisibleRows + 1;
}

} // namespace romm3ds::ui
