#include "title_picker.hpp"

#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

romm3ds::ui::TitlePickerCandidate candidate(std::uint64_t id, const char* name) {
    return {id, 0, 0, name, std::string("/checkpoint/") + name};
}

void test_matches_only_installed_applications_with_unique_checkpoint_folders() {
    const std::vector<romm3ds::ui::InstalledTitleChoice> installed = {
        {0x0004000000123400ULL, 100, 2},
        {0x0004000E00123400ULL, 20, 3},
        {0x0004000000567800ULL, 200, 4},
        {0x0004000000ABC000ULL, 300, 5},
    };
    const std::vector<romm3ds::ui::CheckpointTitleChoice> checkpoint = {
        {0x001234, "Alpha", "/checkpoint/Alpha"},
        {0x005678, "Beta", "/checkpoint/Beta"},
        {0x005678, "Duplicate Beta", "/checkpoint/Duplicate"},
    };
    std::vector<romm3ds::ui::TitlePickerCandidate> choices;
    require(romm3ds::ui::buildTitlePickerCandidates(installed, checkpoint, choices),
            "bounded candidate matching succeeds");
    require(choices.size() == 1, "updates, unmatched titles, and ambiguous folders are excluded");
    require(choices[0].title_id == 0x0004000000123400ULL, "exact installed title id is retained");
    require(choices[0].display_name == "Alpha" && choices[0].checkpoint_directory == "/checkpoint/Alpha",
            "Checkpoint display name and exact directory are retained");
    require(choices[0].installed_size == 100 && choices[0].version == 2,
            "AM title information is retained for display");
}

void test_sorts_names_and_enforces_candidate_bound() {
    std::vector<romm3ds::ui::InstalledTitleChoice> installed;
    std::vector<romm3ds::ui::CheckpointTitleChoice> checkpoint;
    for (std::size_t i = 0; i < romm3ds::ui::kMaxTitlePickerCandidates + 1; ++i) {
        const std::uint32_t unique = static_cast<std::uint32_t>(0x100000 + i);
        installed.push_back({0x0004000000000000ULL | (static_cast<std::uint64_t>(unique) << 8), i, 1});
        checkpoint.push_back({unique, i == 0 ? "Zulu" : "Alpha" + std::to_string(i),
                              "/checkpoint/" + std::to_string(i)});
    }
    std::vector<romm3ds::ui::TitlePickerCandidate> choices{{1, 0, 0, "stale", "stale"}};
    require(!romm3ds::ui::buildTitlePickerCandidates(installed, checkpoint, choices),
            "candidate overflow is rejected instead of truncated");
    require(choices.empty(), "overflow clears partial candidates");

    installed.pop_back();
    checkpoint.pop_back();
    require(romm3ds::ui::buildTitlePickerCandidates(installed, checkpoint, choices),
            "maximum candidate count is accepted");
    require(choices.size() == romm3ds::ui::kMaxTitlePickerCandidates,
            "maximum candidate count is preserved");
    require(choices.front().display_name < choices.back().display_name,
            "picker choices are sorted by display name");
}

void test_picker_navigation_stays_bounded_and_keeps_selection_visible() {
    std::vector<romm3ds::ui::TitlePickerCandidate> choices;
    for (std::uint64_t i = 0; i < 10; ++i) choices.push_back(candidate(100 + i, ("Game " + std::to_string(i)).c_str()));
    romm3ds::ui::TitlePickerState state(std::move(choices), 105);
    require(state.selected() == 5 && state.firstVisible() == 0, "current binding is initially selected");
    state.down();
    state.down();
    require(state.selected() == 7 && state.firstVisible() == 1,
            "moving below the window scrolls by one row");
    for (int i = 0; i < 20; ++i) state.down();
    require(state.selected() == 9 && state.firstVisible() == 3, "down stops at the final bounded row");
    for (int i = 0; i < 20; ++i) state.up();
    require(state.selected() == 0 && state.firstVisible() == 0, "up stops at the first row");
    require(state.visible(0) != nullptr && state.visible(0)->title_id == 100,
            "visible row resolves to the exact candidate");
    require(state.visible(romm3ds::ui::kTitlePickerVisibleRows) == nullptr,
            "rows outside the visible window are rejected");
}

void test_touch_rows_require_the_bounded_list_rectangle() {
    require(romm3ds::ui::titlePickerRowAt(20, 43, 7) == 0, "first picker row is touchable");
    require(romm3ds::ui::titlePickerRowAt(307, 182, 7) == 6, "last picker row is touchable");
    require(romm3ds::ui::titlePickerRowAt(11, 43, 7) == romm3ds::ui::kNoTitlePickerRow,
            "touch left of picker is rejected");
    require(romm3ds::ui::titlePickerRowAt(20, 183, 6) == romm3ds::ui::kNoTitlePickerRow,
            "touch outside visible candidate count is rejected");

    romm3ds::ui::TitlePickerState state({candidate(1, "One"), candidate(2, "Two")});
    require(state.selectVisible(1) && state.current()->title_id == 2,
            "touch selection chooses the exact visible title id");
    require(!state.selectVisible(2) && state.current()->title_id == 2,
            "touch cannot select beyond the bounded list");
}

} // namespace

int main() {
    test_matches_only_installed_applications_with_unique_checkpoint_folders();
    test_sorts_names_and_enforces_candidate_bound();
    test_picker_navigation_stays_bounded_and_keeps_selection_visible();
    test_touch_rows_require_the_bounded_list_rectangle();
    std::cout << "All title picker tests passed\n";
    return 0;
}
