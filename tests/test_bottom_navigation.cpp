#include "bottom_navigation.hpp"
#include "ui_limits.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using namespace romm3ds::ui;

    BottomNavigationState navigation;
    require(navigation.page() == BottomPage::Details, "details is the initial bottom page");
    require(navigation.detailsView() == DetailsView::Info, "details starts on the info submenu");
    require(navigation.focused() == BottomControl::DetailsScreenshots,
            "screenshots is the first details submenu action");
    navigation.next();
    require(navigation.focused() == BottomControl::DetailsManage,
            "details advances to RomM management");
    navigation.next();
    require(navigation.focused() == BottomControl::DetailsSaveData,
            "details advances to save data");
    navigation.next();
    require(navigation.focused() == BottomControl::DetailsTransfer,
            "details advances to transfer");
    navigation.next();
    require(navigation.focused() == BottomControl::DetailsScreenshots,
            "details focus wraps");
    const int left_x = kBottomContentX + kDetailsActionWidth / 2;
    const int right_x = kBottomContentX + kDetailsActionWidth + kDetailsActionGap +
                        kDetailsActionWidth / 2;
    const int first_action_y = kDetailsActionsY + kDetailsActionHeight / 2;
    const int second_action_y = kDetailsActionsY + kDetailsActionStride +
                                kDetailsActionHeight / 2;
    auto action = navigation.touch(left_x, first_action_y);
    require(action.kind == BottomTouchKind::ControlActivated &&
                action.control == BottomControl::DetailsSaveData,
            "save data card is directly touchable");
    action = navigation.touch(right_x, second_action_y);
    require(action.control == BottomControl::DetailsTransfer,
            "transfer card remains directly touchable");
    action = navigation.touch(left_x, second_action_y);
    require(action.control == BottomControl::DetailsScreenshots,
            "screenshots submenu is directly touchable");
    action = navigation.touch(right_x, first_action_y);
    require(action.control == BottomControl::DetailsManage,
            "RomM management submenu is directly touchable");
    action = navigation.touch(left_x, kDetailsActionsY + kDetailsActionHeight);
    require(action.kind == BottomTouchKind::None,
            "the visual gap between info action rows is not touchable");

    navigation.openDetailsView(DetailsView::Screenshots);
    require(navigation.detailsView() == DetailsView::Screenshots &&
                navigation.focused() == BottomControl::ScreenshotsPrevious,
            "screenshots submenu has explicit controls");
    action = navigation.touch(115, 180);
    require(action.control == BottomControl::ScreenshotsPrevious,
            "previous screenshot is touch accessible");
    action = navigation.touch(250, 180);
    require(action.control == BottomControl::ScreenshotsNext,
            "next screenshot is touch accessible");
    action = navigation.touch(180, 216);
    require(action.control == BottomControl::DetailsBack,
            "screenshots submenu has a touch-accessible back action");

    navigation.openDetailsView(DetailsView::Manage);
    require(navigation.focused() == BottomControl::ManageNowPlaying,
            "management starts on now playing");
    action = navigation.touch(180, 48);
    require(action.control == BottomControl::ManageNowPlaying,
            "now playing has an explicit touch row");
    action = navigation.touch(180, 72);
    require(action.control == BottomControl::ManageLastPlayed,
            "last played has an explicit touch row");
    action = navigation.touch(180, 120);
    require(action.control == BottomControl::ManagePlayStatus,
            "play status has an explicit touch row");
    action = navigation.touch(180, 216);
    require(action.control == BottomControl::DetailsBack,
            "management has a touch-accessible back action");
    navigation.openDetailsView(DetailsView::Info);

    action = navigation.touch(24, 90);
    require(action.kind == BottomTouchKind::PageChanged, "rail touch changes page immediately");
    require(action.page == BottomPage::Filters && navigation.page() == BottomPage::Filters,
            "second rail target opens filters");
    require(navigation.focused() == BottomControl::FilterType,
            "filters page starts on type");

    action = navigation.touch(180, 109);
    require(action.kind == BottomTouchKind::ControlActivated,
            "touching a page card activates it");
    require(action.control == BottomControl::FilterSearch,
            "search card has the expected hit box");

    navigation.next();
    require(navigation.focused() == BottomControl::FilterMetadata,
            "filters advance to metadata");
    navigation.next();
    require(navigation.focused() == BottomControl::FilterType,
            "d-pad focus wraps within the current page");
    navigation.previous();
    require(navigation.focused() == BottomControl::FilterMetadata,
            "reverse d-pad focus wraps within the current page");
    action = navigation.touch(180, 174);
    require(action.control == BottomControl::FilterMetadata,
            "metadata filter card is directly touchable");

    action = navigation.touch(24, 149);
    require(action.page == BottomPage::Sort, "third rail target opens sort");
    require(navigation.focused() == BottomControl::SortTitle,
            "sort page has a title-sort control");

    action = navigation.touch(24, 208);
    require(action.page == BottomPage::Settings, "fourth rail target opens settings");
    require(navigation.focused() == BottomControl::SettingsUrl,
            "settings starts on URL");
    navigation.next();
    require(navigation.focused() == BottomControl::SettingsToken, "settings advances to token");
    navigation.next();
    require(navigation.focused() == BottomControl::SettingsTheme, "settings advances to theme");
    navigation.next();
    require(navigation.focused() == BottomControl::SettingsConnect, "settings advances to connect");
    navigation.next();
    require(navigation.focused() == BottomControl::SettingsUpdate, "settings advances to update check");
    navigation.next();
    require(navigation.focused() == BottomControl::SettingsUrl, "settings focus wraps");

    action = navigation.touch(180, 150);
    require(action.control == BottomControl::SettingsConnect,
            "connect card is directly touchable");
    action = navigation.touch(180, 198);
    require(action.control == BottomControl::SettingsUpdate,
            "update check card is directly touchable");

    DetailsScrollState scroll;
    scroll.setLineCounts(9, 5);
    require(scroll.maxOffset() == 4 && scroll.offset() == 0,
            "details scrolling derives a bounded maximum");
    scroll.scrollBy(2);
    require(scroll.offset() == 2, "details can scroll down");
    scroll.scrollBy(99);
    require(scroll.offset() == 4, "details scrolling clamps at the bottom");
    scroll.scrollBy(-99);
    require(scroll.offset() == 0, "details scrolling clamps at the top");
    scroll.beginTouch(100);
    require(scroll.dragTouch(70, 12), "upward touch drag scrolls metadata down");
    require(scroll.offset() == 2, "touch drag converts bounded pixels to lines");
    scroll.endTouch();

    ScreenshotSelectionState screenshots;
    screenshots.setCount(0);
    require(screenshots.empty() && screenshots.index() == 0,
            "empty screenshot state is stable");
    screenshots.next();
    screenshots.previous();
    require(screenshots.index() == 0, "empty screenshot navigation remains bounded");
    screenshots.setCount(4);
    screenshots.previous();
    require(screenshots.index() == 3, "screenshot selection wraps backwards");
    screenshots.setCount(4);
    require(screenshots.index() == 0,
            "opening or changing a game starts from its first screenshot");
    screenshots.next();
    require(screenshots.index() == 1, "screenshot selection advances from the reset index");

    action = navigation.touch(319, 239);
    require(action.kind == BottomTouchKind::None, "outside controls does not activate anything");
    action = navigation.touch(53, 84);
    require(action.kind == BottomTouchKind::None, "rail boundary does not leak into content");

    require(typeChoiceAt(80, 42, 7) == 0, "first type row is touchable");
    require(typeChoiceAt(80, 192, 7) == 6, "last type row is touchable");
    require(typeChoiceAt(20, 37, 7) == kNoTypeChoice, "type rows reject touches outside modal");
    require(typeChoiceAt(80, 216, 7) == kNoTypeChoice, "type rows reject touches below choices");

    require(std::string(bottomPageName(BottomPage::Details)) == "INFO", "page labels are concise");
    require(std::string(bottomPageGlyph(BottomPage::Settings)) == "S", "rail glyphs are stable");

    std::cout << "All bottom navigation tests passed\n";
    return 0;
}
