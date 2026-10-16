#include "save_sources.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void test_parses_retroarch_save_settings_with_bounds() {
    const std::string text =
        "# RetroArch config\n"
        "savefile_directory = \"sdmc:/retroarch/custom saves\"\n"
        "savefiles_in_content_dir = \"false\"\n"
        "sort_savefiles_enable = \"true\"\n"
        "sort_savefiles_by_content_enable = \"true\"\n";
    romm3ds::RetroArchSaveConfig config;
    romm3ds::RetroArchConfigError error = romm3ds::RetroArchConfigError::None;
    require(romm3ds::parseRetroArchSaveConfig(text, config, error), "RetroArch config parses");
    require(config.savefile_directory == "sdmc:/retroarch/custom saves", "custom save directory retained");
    require(!config.savefiles_in_content_dir, "content directory boolean parses");
    require(config.sort_savefiles_by_core, "per-core boolean parses");
    require(config.sort_savefiles_by_content_directory, "per-content-directory boolean parses");

    require(!romm3ds::parseRetroArchSaveConfig(
                std::string(romm3ds::kMaxRetroArchConfigBytes + 1, 'x'), config, error),
            "oversize RetroArch config rejected");
    require(error == romm3ds::RetroArchConfigError::ConfigTooLarge, "oversize category retained");
}

void test_rejects_non_sd_and_traversing_retroarch_paths() {
    romm3ds::RetroArchSaveConfig config;
    romm3ds::RetroArchConfigError error = romm3ds::RetroArchConfigError::None;
    require(!romm3ds::parseRetroArchSaveConfig(
                "savefile_directory = \"/private/saves\"\n", config, error),
            "non-SD path rejected");
    require(error == romm3ds::RetroArchConfigError::UnsafePath, "non-SD path category");
    require(!romm3ds::parseRetroArchSaveConfig(
                "savefile_directory = \"sdmc:/retroarch/../Nintendo 3DS\"\n", config, error),
            "traversing path rejected");
    require(error == romm3ds::RetroArchConfigError::UnsafePath, "traversal category");
}

void test_resolves_default_retroarch_srm_path() {
    romm3ds::RetroArchSaveConfig config;
    std::string path;
    require(romm3ds::resolveRetroArchSavePath(
                config, "sdmc:/roms/gbc/Pokemon Crystal.gbc", "Gambatte", path),
            "default RetroArch path resolves");
    require(path == "sdmc:/retroarch/cores/savefiles/Pokemon Crystal.srm",
            "3DS RetroArch default save path matches frontend source");
}

void test_resolves_configured_content_and_core_sorting() {
    romm3ds::RetroArchSaveConfig config;
    config.savefile_directory = "sdmc:/retroarch/custom";
    config.sort_savefiles_by_content_directory = true;
    config.sort_savefiles_by_core = true;
    std::string path;
    require(romm3ds::resolveRetroArchSavePath(
                config, "sdmc:/roms/snes/Chrono Trigger.sfc", "Snes9x 2005 Plus", path),
            "sorted RetroArch path resolves");
    require(path == "sdmc:/retroarch/custom/snes/Snes9x 2005 Plus/Chrono Trigger.srm",
            "RetroArch redirect ordering is content directory then core library name");

    config.savefiles_in_content_dir = true;
    config.sort_savefiles_by_content_directory = false;
    config.sort_savefiles_by_core = false;
    require(romm3ds::resolveRetroArchSavePath(
                config, "sdmc:/roms/snes/Chrono Trigger.sfc", "", path),
            "content-directory save resolves without a core name");
    require(path == "sdmc:/roms/snes/Chrono Trigger.srm", "content directory overrides configured root");
}

void test_refuses_to_guess_missing_runtime_core_identity() {
    romm3ds::RetroArchSaveConfig config;
    config.sort_savefiles_by_core = true;
    std::string path = "stale";
    require(!romm3ds::resolveRetroArchSavePath(
                config, "sdmc:/roms/nes/Zelda.nes", "", path),
            "per-core path is not guessed without RetroArch library name");
    require(path.empty(), "failed resolution clears output");
    require(!romm3ds::resolveRetroArchSavePath(
                config, "sdmc:/roms/nes/../Zelda.nes", "Mesen", path),
            "unsafe content path rejected");
}

void test_resolves_only_supported_rom_adjacent_sav_paths() {
    std::string path = "stale";
    require(romm3ds::resolveRomAdjacentSavePath(
                "sdmc:/roms/gba/Golden Sun.gba", "gba", path),
            "GBA ROM receives an exact adjacent save binding");
    require(path == "sdmc:/roms/gba/Golden Sun.sav",
            "adjacent provider replaces only the final ROM extension");
    require(romm3ds::resolveRomAdjacentSavePath(
                "sdmc:/roms/gbc/Pokemon Crystal.GBC", "gbc", path),
            "extension matching is case insensitive");
    require(path == "sdmc:/roms/gbc/Pokemon Crystal.sav",
            "case-insensitive ROM extension still yields lowercase sav");
    require(!romm3ds::resolveRomAdjacentSavePath(
                "sdmc:/roms/gba/Golden Sun.zip", "gba", path),
            "catalogue type must match the exact downloaded ROM extension");
    require(path.empty(), "failed adjacent resolution clears output");
    require(!romm3ds::resolveRomAdjacentSavePath(
                "sdmc:/3ds/PocketShelf/downloads/3ds/Game.3ds", "3ds", path),
            "3DS images are not silently treated as ROM-adjacent sav providers");
    require(!romm3ds::resolveRomAdjacentSavePath(
                "sdmc:/roms/snes/Game.sfc", "sfc", path),
            "unknown emulator save conventions remain explicitly unsupported");
    require(!romm3ds::resolveRomAdjacentSavePath(
                "sdmc:/roms/gba/../Golden Sun.gba", "gba", path),
            "unsafe ROM paths are rejected");
    require(romm3ds::isRomAdjacentSaveVersion("Golden Sun.sav", "Golden Sun.sav"),
            "unversioned matching save remains compatible");
    require(romm3ds::isRomAdjacentSaveVersion(
                "Golden Sun.sav", "Golden Sun [2026-10-15_12-34-56].sav"),
            "RomM named-slot timestamp is accepted for the exact adjacent save");
    require(!romm3ds::isRomAdjacentSaveVersion(
                "Golden Sun.sav", "Advance Wars [2026-10-15_12-34-56].sav"),
            "another game's sav version is rejected");
    require(!romm3ds::isRomAdjacentSaveVersion(
                "Golden Sun.sav", "Golden Sun [not-a-timestamp].sav"),
            "malformed version suffix is rejected");
}
}

int main() {
    test_parses_retroarch_save_settings_with_bounds();
    test_rejects_non_sd_and_traversing_retroarch_paths();
    test_resolves_default_retroarch_srm_path();
    test_resolves_configured_content_and_core_sorting();
    test_refuses_to_guess_missing_runtime_core_identity();
    test_resolves_only_supported_rom_adjacent_sav_paths();
    std::cout << "All save source tests passed\n";
    return 0;
}
