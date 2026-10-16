#include "transfer_status.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void test_cia_phases_distinguish_network_staging_from_local_install() {
    const std::string_view staging =
        romm3ds::ui::transferPhaseTitle(romm3ds::ui::TransferPhase::CiaNetworkStaging);
    const std::string_view install =
        romm3ds::ui::transferPhaseTitle(romm3ds::ui::TransferPhase::CiaLocalInstall);
    const std::string_view staging_detail =
        romm3ds::ui::transferPhaseDetail(romm3ds::ui::TransferPhase::CiaNetworkStaging);
    const std::string_view install_detail =
        romm3ds::ui::transferPhaseDetail(romm3ds::ui::TransferPhase::CiaLocalInstall);

    require(staging != install, "CIA staging and install must have distinct progress titles");
    require(staging.find("1/2") != std::string_view::npos,
            "network staging title should identify phase 1 of 2");
    require(install.find("2/2") != std::string_view::npos,
            "local installation title should identify phase 2 of 2");
    require(staging_detail.find("RomM") != std::string_view::npos &&
                staging_detail.find("SD") != std::string_view::npos,
            "staging detail should say bytes travel from RomM to SD");
    require(install_detail.find("local staged SD file") != std::string_view::npos,
            "install detail should identify the local staged file");
    require(install_detail.find("no network") != std::string_view::npos,
            "install detail should explicitly rule out a second network download");
    require(romm3ds::ui::transferPhaseUsesNetwork(
                romm3ds::ui::TransferPhase::CiaNetworkStaging),
            "CIA staging is the sole network phase");
    require(!romm3ds::ui::transferPhaseUsesNetwork(
                romm3ds::ui::TransferPhase::CiaLocalInstall),
            "CIA installation must replay the local staged file without network access");
}

void test_generic_transfer_wording_remains_available() {
    require(!romm3ds::ui::transferPhaseTitle(romm3ds::ui::TransferPhase::Generic).empty(),
            "non-CIA transfers still need a progress title");
}
} // namespace

int main() {
    test_cia_phases_distinguish_network_staging_from_local_install();
    test_generic_transfer_wording_remains_available();
    std::cout << "All transfer status tests passed\n";
    return 0;
}
