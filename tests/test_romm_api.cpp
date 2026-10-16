#include "romm_api.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

const char* pageJson() {
    return R"JSON({
      "items": [{
        "id": 42,
        "name": "Pok\u00e9mon X",
        "fs_name_no_ext": "Pokemon X",
        "platform_display_name": "Nintendo 3DS",
        "summary": "A long adventure",
        "regions": ["USA", "Europe"],
        "metadatum": {
          "genres": ["Role-playing (RPG)", "Adventure"],
          "franchises": ["Pokemon"],
          "collections": ["Pokemon main series"],
          "publishers": ["Nintendo"],
          "developers": ["Game Freak"]
        },
        "path_cover_small": "/assets/romm/resources/roms/7/42/cover/small.jpg?ts=2026-10-12T08:30:00",
        "merged_screenshots": [
          "/assets/romm/resources/roms/7/42/screenshots/0.jpg?ts=2026-10-12T08:30:00",
          "/assets/romm/resources/roms/7/42/screenshots/1.jpg"
        ],
        "rom_user": {"status": "finished", "rating": 8, "difficulty": 6,
                     "completion": 75, "now_playing": true, "backlogged": false,
                     "last_played": "2026-10-13T20:15:00Z"},
        "files": [
          {"id": 701, "rom_id": 42, "file_name": "Pokemon X.cia", "file_size_bytes": 1234, "is_top_level": true},
          {"id": 702, "rom_id": 42, "file_name": "manual.pdf", "file_size_bytes": 55, "is_top_level": true}
        ],
        "ignored": {"nested": [1, {"still": "ignored"}]}
      }],
      "total": 31,
      "limit": 12,
      "offset": 0,
      "char_index": {}, "rom_id_index": [], "filter_values": {}
    })JSON";
}

void test_parses_bounded_romm_page_and_supported_files() {
    romm3ds::RommPage page;
    romm3ds::RommParseError error = romm3ds::RommParseError::None;
    require(romm3ds::parseRommPage(pageJson(), page, error), "RomM page should parse");
    require(error == romm3ds::RommParseError::None, "parse should have no error");
    require(page.total == 31 && page.limit == 12 && page.offset == 0, "page metadata");
    require(page.entries.size() == 1, "unsupported PDF should be skipped");
    const auto& entry = page.entries[0];
    require(entry.rom_id == 42 && entry.file_id == 701, "RomM and file ids");
    require(entry.title == "Pok\xC3\xA9mon X", "unicode escape decoded to UTF-8");
    require(entry.platform == "Nintendo 3DS", "platform parsed");
    require(entry.region == "USA, Europe", "regions parsed");
    require(entry.genres == "Role-playing (RPG), Adventure", "genres parsed");
    require(entry.franchises == "Pokemon", "franchises parsed");
    require(entry.collections == "Pokemon main series", "collections parsed");
    require(entry.publishers == "Nintendo", "publishers parsed");
    require(entry.developers == "Game Freak", "developers parsed");
    require(entry.screenshots.size() == 2, "all bounded merged screenshots parsed");
    require(entry.screenshots[0] ==
                "/assets/romm/resources/roms/7/42/screenshots/0.jpg?ts=2026-10-12T08:30:00" &&
                entry.screenshots[1] ==
                "/assets/romm/resources/roms/7/42/screenshots/1.jpg",
            "merged screenshot order retained");
    require(entry.status == "finished" && entry.rating == 8 && entry.difficulty == 6 &&
                entry.completion == 75 && entry.now_playing && !entry.backlogged &&
                entry.has_last_played,
            "RomM 5.4 user properties parsed");
    require(entry.file_name == "Pokemon X.cia" && entry.type == "cia", "file parsed");
    require(entry.action == romm3ds::CatalogAction::InstallCia, "CIA action");
    require(entry.cover_url == "/assets/romm/resources/roms/7/42/cover/small.jpg?ts=2026-10-12T08:30:00",
            "RomM 5.4 cache-busted cover path parsed");
}

void test_caps_screenshots_and_consumes_the_remaining_array() {
    const std::string json = R"JSON({
      "items":[{"id":42,"name":"Bounded","fs_name_no_ext":"Bounded",
        "platform_display_name":"Nintendo 3DS","summary":null,"regions":[],
        "merged_screenshots":[
          "/assets/romm/resources/roms/7/42/screenshots/0.jpg",
          "/assets/romm/resources/roms/7/42/screenshots/1.jpg",
          "/assets/romm/resources/roms/7/42/screenshots/2.jpg",
          "/assets/romm/resources/roms/7/42/screenshots/3.jpg",
          "/assets/romm/resources/roms/7/42/screenshots/4.jpg",
          "/assets/romm/resources/roms/7/42/screenshots/5.jpg"],
        "path_cover_small":null,"rom_user":null,
        "files":[{"id":701,"rom_id":42,"file_name":"Bounded.cia","file_size_bytes":1234}]}],
      "total":1,"limit":12,"offset":0})JSON";
    romm3ds::RommPage page;
    romm3ds::RommParseError error = romm3ds::RommParseError::None;
    require(romm3ds::parseRommPage(json, page, error),
            "screenshots beyond the retained cap should still be consumed");
    require(page.entries[0].screenshots.size() == romm3ds::kMaxScreenshots,
            "screenshot list should be bounded");
    require(page.entries[0].screenshots.back().find("3.jpg") != std::string::npos,
            "the first bounded screenshots should be retained in order");
}

void test_skips_oversized_optional_romm_metadata() {
    const std::string summary(769, 's');
    const std::string platform(451, 'p');
    const std::string cover(513, 'c');
    const std::string json =
        "{\"items\":[{\"id\":42,\"name\":\"Playable\",\"fs_name_no_ext\":\"Playable\"," 
        "\"platform_display_name\":\"" + platform + "\",\"summary\":\"" + summary +
        "\",\"regions\":[\"USA\"],\"path_cover_small\":\"" + cover +
        "\",\"rom_user\":null,\"files\":[{\"id\":701,\"rom_id\":42,"
        "\"file_name\":\"Playable.cia\",\"file_size_bytes\":1234}]}],"
        "\"total\":1,\"limit\":12,\"offset\":0}";

    romm3ds::RommPage page;
    romm3ds::RommParseError error = romm3ds::RommParseError::None;
    require(romm3ds::parseRommPage(json, page, error),
            "oversized optional summary and cover should not reject the catalogue");
    require(error == romm3ds::RommParseError::None, "optional metadata parse error");
    require(page.entries.size() == 1, "supported file should remain available");
    require(page.entries[0].platform.empty() &&
                (page.entries[0].metadata_flags & romm3ds::MetadataPlatformOmitted),
            "oversized platform should be identified as client-omitted");
    require(page.entries[0].summary.empty(), "oversized summary should be discarded");
    require(page.entries[0].metadata_flags & romm3ds::MetadataSummaryOmitted,
            "oversized summary should be identified as client-omitted");
    require(page.entries[0].cover_url.empty(), "oversized cover should be discarded");
}

void test_truncates_large_metadata_lists_without_rejecting_the_page() {
    const std::string json = R"JSON({
      "items":[{"id":42,"name":"Bounded","fs_name_no_ext":"Bounded",
        "platform_display_name":"Nintendo 3DS","summary":null,"regions":[],
        "metadatum":{"genres":["g1","g2","g3","g4","g5","g6","g7","g8","g9"]},
        "path_cover_small":null,"rom_user":null,
        "files":[{"id":701,"rom_id":42,"file_name":"Bounded.cia","file_size_bytes":1234}]}],
      "total":1,"limit":12,"offset":0})JSON";
    romm3ds::RommPage page;
    romm3ds::RommParseError error = romm3ds::RommParseError::None;
    require(romm3ds::parseRommPage(json, page, error),
            "metadata beyond the display cap should be consumed, not reject the page");
    require(page.entries.size() == 1, "bounded metadata entry retained");
    require(page.entries[0].genres == "g1, g2, g3, g4, g5, g6, g7, g8",
            "only the first eight metadata values should be retained");
    require(page.entries[0].metadata_flags & romm3ds::MetadataGenresTruncated,
            "truncated metadata should be distinguishable from server absence");
}

void test_accepts_missing_and_null_optional_metadata() {
    const std::string json = R"JSON({
      "items":[{"id":42,"name":"Sparse","fs_name_no_ext":"Sparse",
        "platform_display_name":"Game Boy Advance","summary":null,"regions":null,
        "metadatum":null,"rom_user":null,
        "files":[{"id":701,"rom_id":42,"file_name":"Sparse.gba","file_size_bytes":1234}]}],
      "total":1,"limit":12,"offset":0})JSON";
    romm3ds::RommPage page;
    romm3ds::RommParseError error = romm3ds::RommParseError::None;
    require(romm3ds::parseRommPage(json, page, error),
            "null optional metadata should not reject a playable ROM");
    require(page.entries.size() == 1 && page.entries[0].summary.empty() &&
                page.entries[0].genres.empty() && page.entries[0].metadata_flags == 0,
            "server absence should remain distinct from client omission");
}

void test_marks_oversized_metadata_items_and_keeps_following_fields() {
    const std::string json =
        "{\"items\":[{\"id\":42,\"name\":\"Playable\",\"fs_name_no_ext\":\"Playable\","
        "\"platform_display_name\":\"Game Boy Advance\",\"summary\":null,"
        "\"regions\":[\"" + std::string(65, 'r') + "\",\"USA\"],"
        "\"metadatum\":{\"developers\":[\"" + std::string(65, 'd') +
        "\",\"Capcom\"],\"publishers\":[\"Nintendo\"]},\"rom_user\":null,"
        "\"files\":[{\"id\":701,\"rom_id\":42,\"file_name\":\"Playable.gba\","
        "\"file_size_bytes\":1234}]}],\"total\":1,\"limit\":12,\"offset\":0}";
    romm3ds::RommPage page;
    romm3ds::RommParseError error = romm3ds::RommParseError::None;
    require(romm3ds::parseRommPage(json, page, error),
            "oversized optional list items should be skipped without losing later metadata");
    require(page.entries[0].region == "USA" && page.entries[0].developers == "Capcom" &&
                page.entries[0].publishers == "Nintendo",
            "valid metadata after oversized values should remain available");
    require((page.entries[0].metadata_flags & romm3ds::MetadataRegionsTruncated) != 0 &&
                (page.entries[0].metadata_flags & romm3ds::MetadataDevelopersTruncated) != 0,
            "oversized optional values should be reported as omitted");
}

void test_accepts_bounded_romm_54_schema_lengths() {
    const std::string title(350, 't');
    const std::string platform(400, 'p');
    const std::string file_name = std::string(446, 'f') + ".cia";
    const std::string json =
        "{\"items\":[{\"id\":42,\"name\":\"" + title +
        "\",\"fs_name_no_ext\":\"fallback\",\"platform_display_name\":\"" + platform +
        "\",\"summary\":null,\"regions\":[],\"path_cover_small\":null,"
        "\"rom_user\":null,\"files\":[{\"id\":701,\"rom_id\":42,\"file_name\":\"" +
        file_name + "\",\"file_size_bytes\":1234}]}],\"total\":1,\"limit\":12,\"offset\":0}";

    romm3ds::RommPage page;
    romm3ds::RommParseError error = romm3ds::RommParseError::None;
    require(romm3ds::parseRommPage(json, page, error),
            "RomM 5.4 bounded title, platform, and filename lengths should parse");
    require(page.entries.size() == 1, "schema-length entry should be retained");
    require(page.entries[0].title == title, "schema-length title should be retained");
    require(page.entries[0].platform == platform, "schema-length platform should be retained");
    require(page.entries[0].file_name == file_name, "schema-length filename should be retained");
}

void test_builds_documented_urls_with_single_path_encoding() {
    const std::string url = romm3ds::rommFileContentUrl(
        "http://192.168.1.6:890/", 701, "Pokemon X (USA)+v1.cia");
    require(url == "http://192.168.1.6:890/api/roms/701/files/content/Pokemon%20X%20%28USA%29%2Bv1.cia",
            "download URL must use file id and encode filename once");
    require(romm3ds::rommCoverUrl(
                "http://host:890",
                "/assets/romm/resources/roms/7/42/cover/small.jpg?ts=2026-10-12T08:30:00") ==
                "http://host:890/assets/romm/resources/roms/7/42/cover/small.jpg?ts=2026-10-12T08:30:00",
            "RomM 5.4 cover query must remain a query instead of becoming part of the path");
    require(romm3ds::rommCoverUrl(
                "http://host:890",
                "http://host:890/assets/romm/resources/roms/7/42/cover/small.png?ts=123") ==
                "http://host:890/assets/romm/resources/roms/7/42/cover/small.png?ts=123",
            "same-origin absolute RomM cover URL should be accepted");
    require(romm3ds::rommCoverUrl("http://host:890", "http://other/cover.jpg").empty(),
            "token must never be sent to an external cover origin");
    require(romm3ds::rommCoverUrl("http://host:890", "/not-a-romm-cover/image.jpg").empty(),
            "authenticated cover requests must stay inside RomM resources");
    require(romm3ds::rommCoverUrl(
                "http://host:890", "/assets/romm/resources/roms/7/42/screenshots/0.jpg").empty(),
            "cover requests must be constrained to the RomM cover directory");
    require(romm3ds::rommCoverUrl(
                "http://host:890", "/assets/romm/resources/roms/7/42/cover/").empty(),
            "cover URL requires a resource filename");
    require(romm3ds::rommCoverUrl(
                "http://host:890", "/assets/romm/resources/roms/7/42/cover/%2e%2e/secret.jpg").empty(),
            "encoded traversal must be rejected before an authenticated request");
    require(romm3ds::rommCoverUrl(
                "http://host:890", "/assets/romm/resources/roms/7/42/cover/small.jpg\r\nX-Test: bad").empty(),
            "control characters must be rejected before an authenticated request");
    require(romm3ds::rommCatalogueUrl("http://host:890/", 12, 24, "mario kart", true) ==
                "http://host:890/api/roms?limit=12&offset=24&with_files=true&with_char_index=false&with_filter_values=false&with_rom_id_index=false&with_total=true&search_term=mario%20kart",
            "catalogue URL should be bounded and filtered");
    romm3ds::RommFilters parsed_filters;
    require(romm3ds::parseRommFilters(
                "genre=RPG; developer=GF; publisher=N; franchise=P; collection=M",
                parsed_filters),
            "bounded metadata filter text should parse documented keys");
    require(parsed_filters.genre == "RPG" &&
                parsed_filters.developer == "GF" &&
                parsed_filters.publisher == "N" &&
                parsed_filters.franchise == "P" &&
                parsed_filters.collection == "M",
            "metadata filter values should be retained");
    require(!romm3ds::parseRommFilters("company=Game Freak", parsed_filters),
            "unknown RomM filter keys must be rejected");
    require(!romm3ds::parseRommFilters("genre=RPG;genre=Adventure", parsed_filters),
            "duplicate metadata filter keys must be rejected");

    romm3ds::RommFilters filters;
    filters.genre = "Role-playing (RPG)";
    filters.developer = "Game Freak";
    filters.publisher = "Nintendo";
    filters.franchise = "Pokemon";
    filters.collection = "Pokemon main series";
    require(romm3ds::rommCatalogueUrl("http://host:890", 12, 0, "", true, filters) ==
                "http://host:890/api/roms?limit=12&offset=0&with_files=true&with_char_index=false&with_filter_values=false&with_rom_id_index=false&with_total=true&genres=Role-playing%20%28RPG%29&developers=Game%20Freak&publishers=Nintendo&franchises=Pokemon&collections=Pokemon%20main%20series",
            "documented metadata filters should use repeatable RomM 5.4 query names");
    require(romm3ds::rommMediaUrl(
                "http://host:890", 42,
                "/assets/romm/resources/roms/7/42/screenshots/0.jpg?ts=2026-10-12 08:30:00+00:00") ==
                "http://host:890/assets/romm/resources/roms/7/42/screenshots/0.jpg?ts=2026-10-12%2008:30:00%2B00:00",
            "same-rom screenshot path should be accepted and normalized");
    require(romm3ds::rommMediaUrl(
                "http://host:890", 42,
                "/assets/romm/resources/roms/7/99/screenshots/0.jpg").empty(),
            "media from another rom must be rejected");
    const std::vector<std::string> screenshot_paths = {
        "/assets/romm/resources/roms/7/42/screenshots/0.jpg",
        "http://other/assets/romm/resources/roms/7/42/screenshots/1.jpg",
        "/assets/romm/resources/roms/7/99/screenshots/2.jpg",
        "/assets/romm/resources/roms/7/42/screenshots/3.jpg"};
    const auto screenshot_urls = romm3ds::rommScreenshotUrls(
        "http://host:890", 42, screenshot_paths);
    require(screenshot_urls.size() == 2 &&
                screenshot_urls[0].find("/0.jpg") != std::string::npos &&
                screenshot_urls[1].find("/3.jpg") != std::string::npos,
            "screenshot list retains only same-origin paths associated with the selected ROM");
    require(romm3ds::rommUserPropsUrl("http://host:890/", 42) ==
                "http://host:890/api/roms/42/props",
            "user properties route should match RomM 5.4 OpenAPI");
    require(romm3ds::rommLastPlayedUrl("http://host:890", 42, true) ==
                "http://host:890/api/roms/42/props?update_last_played=true",
            "last-played action should use the documented query flag");
    require(romm3ds::rommLastPlayedUrl("http://host:890", 42, false) ==
                "http://host:890/api/roms/42/props?remove_last_played=true",
            "last-played clear should use the documented query flag");
    require(romm3ds::rommUserPropsBody(romm3ds::RommUserAction::NowPlaying, 1) ==
                "{\"now_playing\":true}", "now-playing action body");
    require(romm3ds::rommUserPropsBody(romm3ds::RommUserAction::Backlogged, 0) ==
                "{\"backlogged\":false}", "backlog action body");
    require(romm3ds::rommUserPropsBody(romm3ds::RommUserAction::StatusFinished, 0) ==
                "{\"status\":\"finished\"}", "finished status action body");
    require(romm3ds::rommUserPropsBody(romm3ds::RommUserAction::StatusCompleted100, 0) ==
                "{\"status\":\"completed_100\"}", "completed status action body");
    require(romm3ds::rommUserPropsBody(romm3ds::RommUserAction::Rating, 10) ==
                "{\"rating\":10}", "bounded rating action body");
    require(romm3ds::rommUserPropsBody(romm3ds::RommUserAction::Difficulty, 10) ==
                "{\"difficulty\":10}", "bounded difficulty action body");
    require(romm3ds::rommUserPropsBody(romm3ds::RommUserAction::Completion, 100) ==
                "{\"completion\":100}", "bounded completion action body");
    require(romm3ds::rommUserPropsBody(romm3ds::RommUserAction::Rating, 11).empty(),
            "out-of-contract rating must be rejected locally");
    require(romm3ds::rommUserPropsBody(romm3ds::RommUserAction::Difficulty, 11).empty(),
            "out-of-contract difficulty must be rejected locally");
    require(romm3ds::rommUserPropsBody(romm3ds::RommUserAction::Completion, 101).empty(),
            "out-of-contract completion must be rejected locally");
}

void test_uses_large_same_origin_cover_when_small_derivative_is_missing() {
    std::ifstream fixture("tests/fixtures/romm_5_4_large_cover_page.json", std::ios::binary);
    require(fixture.good(), "RomM 5.4 large-cover fixture should open");
    const std::string json((std::istreambuf_iterator<char>(fixture)),
                           std::istreambuf_iterator<char>());

    romm3ds::RommPage page;
    romm3ds::RommParseError error = romm3ds::RommParseError::None;
    require(romm3ds::parseRommPage(json, page, error), "RomM 5.4 large-cover page should parse");
    require(page.entries.size() == 1, "fixture should yield one supported file");
    require(page.entries[0].cover_url ==
                "/assets/romm/resources/roms/7/42/cover/big.webp?ts=2026-10-12 08:30:00+00:00",
            "path_cover_large should be used when path_cover_small is empty");
    require(romm3ds::rommCoverUrl("http://192.168.1.6:890", page.entries[0].cover_url) ==
                "http://192.168.1.6:890/assets/romm/resources/roms/7/42/cover/big.webp?ts=2026-10-12%2008:30:00%2B00:00",
            "RomM datetime query should be safely encoded on the same-origin cover URL");
}

void test_rejects_oversize_malformed_and_unsafe_data() {
    romm3ds::RommPage page;
    romm3ds::RommParseError error = romm3ds::RommParseError::None;
    require(!romm3ds::parseRommPage("{\"items\":[", page, error), "malformed page rejected");
    require(error == romm3ds::RommParseError::MalformedJson, "malformed category");
    require(!romm3ds::parseRommPage(std::string(romm3ds::kMaxRommPageBytes + 1, 'x'), page, error),
            "oversize page rejected");
    require(error == romm3ds::RommParseError::PageTooLarge, "oversize category");

    const std::string invalid_optional =
        "{\"items\":[{\"id\":42,\"name\":\"Playable\",\"fs_name_no_ext\":\"Playable\"," 
        "\"platform_display_name\":\"Nintendo 3DS\",\"summary\":\"" +
        std::string(769, 's') +
        "\\uD800\",\"regions\":[],\"path_cover_small\":null,\"rom_user\":null,"
        "\"files\":[{\"id\":701,\"rom_id\":42,\"file_name\":\"Playable.cia\","
        "\"file_size_bytes\":1234}]}],\"total\":1,\"limit\":12,\"offset\":0}";
    require(!romm3ds::parseRommPage(invalid_optional, page, error),
            "oversized optional string must still be valid JSON");
    require(error == romm3ds::RommParseError::MalformedJson,
            "malformed skipped metadata category");

    std::string unsafe = pageJson();
    const auto pos = unsafe.find("Pokemon X.cia");
    unsafe.replace(pos, std::string("Pokemon X.cia").size(), "../bad.cia");
    require(!romm3ds::parseRommPage(unsafe, page, error), "unsafe supported file rejected");
    require(error == romm3ds::RommParseError::UnsafeFilename, "unsafe filename category");
}
}

int main() {
    test_parses_bounded_romm_page_and_supported_files();
    test_caps_screenshots_and_consumes_the_remaining_array();
    test_skips_oversized_optional_romm_metadata();
    test_truncates_large_metadata_lists_without_rejecting_the_page();
    test_accepts_missing_and_null_optional_metadata();
    test_marks_oversized_metadata_items_and_keeps_following_fields();
    test_accepts_bounded_romm_54_schema_lengths();
    test_builds_documented_urls_with_single_path_encoding();
    test_uses_large_same_origin_cover_when_small_derivative_is_missing();
    test_rejects_oversize_malformed_and_unsafe_data();
    std::cout << "All RomM API tests passed\n";
    return 0;
}
