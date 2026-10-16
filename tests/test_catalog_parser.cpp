#include "catalog_parser.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

std::string record(const std::string& title = "Mario") {
    return "{\"rom_id\":42,\"title\":\"" + title +
        "\",\"platform\":\"n3ds\",\"regions\":[\"USA\",\"Europe\"],"
        "\"file_id\":7,\"file_name\":\"Mario.cia\",\"type\":\"cia\","
        "\"size\":123,\"summary\":\"Kart game\",\"favorite\":true,"
        "\"status\":\"completed\",\"action\":\"install_cia\","
        "\"cover_url\":\"http://bridge/v1/covers/42\","
        "\"download_url\":\"http://bridge/v1/files/7/download?name=Mario.cia\"}\n";
}

void test_parses_typed_record_across_chunks() {
    romm3ds::CatalogParser parser;
    const std::string input = record();
    require(parser.feed(std::string_view(input.data(), 17)), "first chunk");
    require(parser.feed(std::string_view(input.data() + 17, input.size() - 17)), "second chunk");
    require(parser.finish(), "finish");
    require(parser.entries().size() == 1, "one entry");
    const auto& entry = parser.entries()[0];
    require(entry.rom_id == 42 && entry.file_id == 7, "ids");
    require(entry.title == "Mario" && entry.platform == "n3ds", "metadata");
    require(entry.region == "USA, Europe", "regions");
    require(entry.type == "cia" && entry.action == romm3ds::CatalogAction::InstallCia, "action");
    require(entry.size == 123 && entry.favorite, "typed values");
    require(entry.cover_url == "http://bridge/v1/covers/42", "bounded cover URL");
}

void test_rejects_malformed_missing_fields_and_limits() {
    romm3ds::CatalogParser malformed;
    require(!malformed.feed("{bad}\n"), "malformed JSON rejected");
    require(malformed.error() == romm3ds::CatalogParseError::MalformedJson, "malformed error");

    romm3ds::CatalogParser missing;
    require(!missing.feed("{\"title\":\"No file\"}\n"), "missing required rejected");
    require(missing.error() == romm3ds::CatalogParseError::MissingField, "missing error");

    romm3ds::CatalogParser long_line;
    require(!long_line.feed(std::string(romm3ds::kMaxCatalogLineBytes + 1, 'x')),
            "oversized line rejected");
    require(long_line.error() == romm3ds::CatalogParseError::LineTooLong, "line limit error");

    romm3ds::CatalogParser traversal;
    std::string unsafe = record();
    const auto pos = unsafe.find("Mario.cia");
    unsafe.replace(pos, 9, "../bad.gba");
    require(!traversal.feed(unsafe), "unsafe filename rejected");
    require(traversal.error() == romm3ds::CatalogParseError::UnsafeFilename, "filename error");
}

void test_accepts_download_types_and_unterminated_final_line() {
    std::string input = record("Zelda");
    input.pop_back();
    auto replace = [&](const std::string& from, const std::string& to) {
        std::size_t pos = 0;
        while ((pos = input.find(from, pos)) != std::string::npos) {
            input.replace(pos, from.size(), to); pos += to.size();
        }
    };
    replace("Mario.cia", "Zelda.gba");
    replace("\"cia\"", "\"gba\"");
    replace("\"install_cia\"", "\"download\"");
    romm3ds::CatalogParser parser;
    require(parser.feed(input), "feed final line");
    require(parser.finish(), "unterminated final line");
    require(parser.entries()[0].action == romm3ds::CatalogAction::Download, "download action");
}

void test_discards_oversized_optional_cache_metadata_with_a_visible_reason() {
    std::string input = record();
    const auto summary = input.find("Kart game");
    input.replace(summary, std::string("Kart game").size(), std::string(769, 's'));
    romm3ds::CatalogParser parser;
    require(parser.feed(input) && parser.finish(),
            "oversized optional cache metadata should not hide the playable record");
    require(parser.entries()[0].summary.empty(), "oversized cache summary is discarded");
    require(parser.entries()[0].metadata_flags & romm3ds::MetadataSummaryOmitted,
            "cache parser records why the summary is absent");
}
}

int main() {
    test_parses_typed_record_across_chunks();
    test_rejects_malformed_missing_fields_and_limits();
    test_accepts_download_types_and_unterminated_final_line();
    test_discards_oversized_optional_cache_metadata_with_a_visible_reason();
    std::cout << "All catalog parser tests passed\n";
    return 0;
}
