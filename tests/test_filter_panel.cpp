#include "filter_panel.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using romm3ds::ui::FilterControl;
    using romm3ds::ui::FilterPanelState;

    FilterPanelState panel;
    require(!panel.active(), "filter panel starts unfocused");
    require(panel.focused() == FilterControl::Type, "type is the initial control");
    panel.activate();
    require(panel.active(), "panel can receive focus");
    panel.right();
    require(panel.focused() == FilterControl::Search, "right moves to search");
    panel.right();
    require(panel.focused() == FilterControl::Platform, "right moves to platform");
    require(!panel.focusedEnabled(), "platform is visibly disabled until implemented");
    panel.right();
    require(panel.focused() == FilterControl::Region, "right moves to region");
    panel.right();
    require(panel.focused() == FilterControl::Type, "right wraps across controls");
    panel.left();
    require(panel.focused() == FilterControl::Region, "left wraps across controls");
    panel.cancel();
    require(!panel.active(), "B-style cancel returns focus to the carousel");

    std::cout << "All filter panel tests passed\n";
    return 0;
}
