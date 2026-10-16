#include "cache.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

romm3ds::CatalogEntry entry() {
    romm3ds::CatalogEntry value;
    value.rom_id = 42; value.file_id = 701; value.size = 1234;
    value.title = "A \"quoted\" title"; value.platform = "Nintendo 3DS";
    value.region = "USA"; value.file_name = "Game.cia"; value.type = "cia";
    value.summary = "line one\nline two"; value.status = "completed";
    value.genres = "Adventure, RPG"; value.franchises = "Pocket";
    value.collections = "Main series"; value.publishers = "Publisher";
    value.developers = "Developer";
    value.metadata_flags = romm3ds::MetadataSummaryOmitted |
                           romm3ds::MetadataGenresTruncated |
                           romm3ds::MetadataUserSupplied;
    value.screenshots = {"http://host/assets/0.jpg", "http://host/assets/1.jpg"};
    value.rating = 8; value.difficulty = 6; value.completion = 75;
    value.now_playing = true; value.backlogged = true; value.has_last_played = true;
    value.download_url = "http://host/api/roms/701/files/content/Game.cia";
    value.cover_url = "http://host/assets/cover.jpg";
    value.action = romm3ds::CatalogAction::InstallCia;
    return value;
}

void test_catalog_cache_round_trip_and_bounds() {
    const std::string path = "/tmp/pocketshelf-cache-" + std::to_string(static_cast<long long>(getpid())) + ".ndjson";
    std::remove(path.c_str());
    std::vector<romm3ds::CatalogEntry> input{entry()};
    require(romm3ds::saveCatalogCache(path, input), "cache save");
    std::vector<romm3ds::CatalogEntry> output;
    require(romm3ds::loadCatalogCache(path, output), "cache load");
    require(output.size() == 1, "one cached record");
    require(output[0].title == input[0].title && output[0].summary == input[0].summary,
            "escaped fields round trip");
    require(output[0].genres == input[0].genres && output[0].developers == input[0].developers &&
                output[0].screenshots == input[0].screenshots && output[0].rating == 8 &&
                output[0].difficulty == 6 && output[0].completion == 75 &&
                output[0].now_playing && output[0].backlogged && output[0].has_last_played &&
                output[0].metadata_flags == input[0].metadata_flags,
            "RomM metadata, screenshot list, and user properties round trip");
    std::remove(path.c_str());
}

void test_cover_cache_paths_are_numeric_only() {
    require(romm3ds::coverCachePath("sdmc:/3ds/PocketShelf/cache/covers", 701) ==
                "sdmc:/3ds/PocketShelf/cache/covers/701.img", "numeric path");
    require(romm3ds::coverCachePath("base", 0).empty(), "zero id rejected");
}

void test_catalog_cache_round_trips_maximum_valid_romm_fields() {
    const std::string path = "/tmp/pocketshelf-cache-max-" +
        std::to_string(static_cast<long long>(getpid())) + ".ndjson";
    std::remove(path.c_str());
    auto value = entry();
    value.title.assign(450, 't');
    value.platform.assign(450, 'p');
    value.file_name = std::string(446, 'f') + ".cia";
    value.summary.assign(768, 's');
    value.genres.assign(450, 'g');
    value.franchises.assign(450, 'r');
    value.collections.assign(450, 'c');
    value.publishers.assign(450, 'u');
    value.developers.assign(450, 'd');
    value.download_url = "http://host/" + std::string(500, 'x');
    value.cover_url.clear();
    value.screenshots.clear();
    require(romm3ds::saveCatalogCache(path, {value}),
            "maximum valid RomM fields should fit the cache record limit");
    std::vector<romm3ds::CatalogEntry> output;
    require(romm3ds::loadCatalogCache(path, output),
            "maximum valid RomM fields should parse back from cache");
    require(output.size() == 1 && output[0].title == value.title &&
                output[0].developers == value.developers &&
                output[0].summary == value.summary,
            "maximum valid fields should not be silently lost in cache serialization");
    std::remove(path.c_str());
}

void test_screenshot_cache_paths_are_unique_per_validated_resource() {
    const std::string first = romm3ds::screenshotCachePath(
        "cache", 42, 0, "http://host/assets/romm/resources/roms/7/42/screenshots/0.jpg?ts=1");
    const std::string second = romm3ds::screenshotCachePath(
        "cache", 42, 1, "http://host/assets/romm/resources/roms/7/42/screenshots/1.jpg?ts=1");
    const std::string refreshed = romm3ds::screenshotCachePath(
        "cache", 42, 0, "http://host/assets/romm/resources/roms/7/42/screenshots/0.jpg?ts=2");
    require(!first.empty() && first != second && first != refreshed,
            "each screenshot URL and index has an independent cache path");
    require(first.find("/s42-0-") != std::string::npos,
            "screenshot cache path retains bounded numeric identity");
    require(romm3ds::screenshotCachePath("cache", 0, 0, "http://host/image.jpg").empty(),
            "zero ROM id is rejected for screenshot cache");
    require(romm3ds::screenshotCachePath("cache", 42, 0, "").empty(),
            "empty screenshot URL is rejected for screenshot cache");
}
}

int main() {
    test_catalog_cache_round_trip_and_bounds();
    test_catalog_cache_round_trips_maximum_valid_romm_fields();
    test_cover_cache_paths_are_numeric_only();
    test_screenshot_cache_paths_are_unique_per_validated_resource();
    std::cout << "All cache tests passed\n";
    return 0;
}
