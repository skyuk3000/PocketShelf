#include "type_chooser.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using romm3ds::ui::TypeChooserState;

    TypeChooserState chooser("gba");
    require(chooser.value() == "gba", "chooser should open on active filter");
    require(chooser.firstVisible() == 0, "an initially visible filter keeps the list at the top");
    chooser.down();
    require(chooser.value() == "gb", "down should select next visible type");
    chooser.up();
    require(chooser.value() == "gba", "up should restore prior type");
    require(chooser.select(5), "direct selection accepts a valid choice");
    require(chooser.selected() == 5, "direct selection updates the choice");
    require(!chooser.select(chooser.count()), "direct selection rejects an invalid choice");
    require(chooser.selected() == 5, "invalid selection preserves the current choice");

    TypeChooserState hidden("gbc");
    require(hidden.selected() == 7, "chooser restores a filter below the initial viewport");
    require(hidden.firstVisible() == 1, "opening keeps the restored filter visible");
    require(hidden.visibleCount() == 7, "chooser exposes only rows that fit above the footer");
    hidden.down();
    require(hidden.value() == "nes" && hidden.firstVisible() == 2,
            "d-pad down scrolls the viewport when selection reaches its bottom");
    hidden.down();
    require(hidden.value() == "sfc" && hidden.firstVisible() == 3,
            "the final file type remains reachable");
    hidden.down();
    require(hidden.value() == "sfc" && hidden.firstVisible() == 3,
            "d-pad navigation is bounded at the final file type");
    hidden.up();
    require(hidden.value() == "nes" && hidden.firstVisible() == 3,
            "moving within the viewport does not jump its scroll position");
    require(hidden.selectVisible(0), "touch can select the first displayed row");
    require(hidden.value() == "3dsx", "touch rows map through the current scroll offset");
    require(!hidden.selectVisible(hidden.visibleCount()),
            "touch rejects a row outside the displayed viewport");

    TypeChooserState touch("");
    touch.beginTouch(150);
    require(touch.dragTouch(100, 25), "an upward drag scrolls toward later file types");
    require(touch.value() == "3ds" && touch.firstVisible() == 0,
            "touch drag advances selection by whole rows");
    require(!touch.endTouch(4), "releasing a drag does not activate a touched row");
    touch.beginTouch(100);
    require(touch.endTouch(4), "a stationary touch activates its displayed row");
    require(touch.value() == "nds", "stationary touch selects the tapped row");
    touch.beginTouch(100);
    require(touch.dragTouch(-500, 25), "large touch drags are accepted");
    require(touch.value() == "sfc" && touch.firstVisible() == 3,
            "touch scrolling clamps at the final choice");

    TypeChooserState all("unknown");
    require(all.value().empty(), "unknown current filter should fall back to All supported");
    all.up();
    require(all.value().empty(), "up should remain bounded at All supported");
    std::cout << "All type chooser tests passed\n";
    return 0;
}