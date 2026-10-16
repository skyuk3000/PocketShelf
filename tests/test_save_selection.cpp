#include "save_selection.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void test_selection_is_bounded_and_preserved_when_lists_refresh() {
    romm3ds::ui::SaveSelectionState state;
    state.setCounts(5, 4);
    require(state.localIndex() == 0 && state.remoteIndex() == 0,
            "new save lists start on their first entries");
    state.nextLocal();
    state.nextLocal();
    state.nextRemote();
    require(state.localIndex() == 2 && state.remoteIndex() == 1,
            "local and remote selections move independently");

    state.setCounts(2, 1);
    require(state.localIndex() == 1 && state.remoteIndex() == 0,
            "refresh clamps selections to the remaining entries");
    state.setCounts(0, 0);
    state.nextLocal();
    state.previousRemote();
    require(state.localIndex() == 0 && state.remoteIndex() == 0,
            "empty lists never underflow");
}

void test_visible_window_keeps_selected_backup_on_screen() {
    romm3ds::ui::SaveSelectionState state;
    state.setCounts(6, 5);
    for (int i = 0; i < 4; ++i) state.nextLocal();
    for (int i = 0; i < 3; ++i) state.nextRemote();
    require(state.localWindowStart(2) == 3,
            "local window scrolls to include the selected backup");
    require(state.remoteWindowStart(2) == 2,
            "remote window scrolls independently");
    require(state.selectLocalVisibleRow(0, 2) && state.localIndex() == 3,
            "touch can select a visible local row");
    require(state.selectRemoteVisibleRow(1, 2) && state.remoteIndex() == 3,
            "touch can select a visible remote row");
    require(!state.selectRemoteVisibleRow(2, 2),
            "touch rows outside the bounded window are rejected");
}

void test_previous_and_next_stop_at_list_edges() {
    romm3ds::ui::SaveSelectionState state;
    state.setCounts(2, 2);
    state.previousLocal();
    state.previousRemote();
    require(state.localIndex() == 0 && state.remoteIndex() == 0,
            "previous stops at the first entry");
    state.nextLocal(); state.nextLocal();
    state.nextRemote(); state.nextRemote();
    require(state.localIndex() == 1 && state.remoteIndex() == 1,
            "next stops at the last entry");
}
} // namespace

int main() {
    test_selection_is_bounded_and_preserved_when_lists_refresh();
    test_visible_window_keeps_selected_backup_on_screen();
    test_previous_and_next_stop_at_list_edges();
    std::cout << "All save selection tests passed\n";
    return 0;
}
