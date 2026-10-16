#include "text_layout.hpp"
#include "catalog_parser.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}

int main() {
    using romm3ds::ui::wrapText;

    expect(wrapText("one two three four", 7, 3) ==
               std::vector<std::string>({"one two", "three", "four"}),
           "wraps at word boundaries");
    expect(wrapText("abcdefghij", 4, 3) ==
               std::vector<std::string>({"abcd", "efgh", "ij"}),
           "splits an overlong word");
    expect(wrapText("one two three four five", 7, 2) ==
               std::vector<std::string>({"one two", "three…"}),
           "marks truncated output on the final bounded line");
    expect(wrapText("  one\n two   three ", 9, 3) ==
               std::vector<std::string>({"one two", "three"}),
           "normalizes whitespace");

    romm3ds::CatalogEntry entry;
    entry.platform = "Game Boy Advance";
    entry.region = "USA";
    entry.genres = "RPG, Adventure";
    entry.developers = "Capcom";
    entry.summary = std::string(500, 's');
    entry.metadata_flags = romm3ds::MetadataGenresTruncated;
    entry.status = "finished";
    entry.rating = 8;
    const auto info = romm3ds::ui::buildInfoLines(entry, "gba", "Not downloaded");
    expect(info.size() > 1 && info[0].kind == romm3ds::ui::InfoLineKind::Section &&
               info[1].kind == romm3ds::ui::InfoLineKind::Field &&
               info[1].label == "Platform" && info[1].value == "Game Boy Advance",
           "Info starts with an aligned platform field");
    bool saw_summary = false;
    bool saw_truncation = false;
    bool saw_cached_user = false;
    std::size_t summary_lines = 0;
    for (const auto& line : info) {
        if (line.kind == romm3ds::ui::InfoLineKind::Section && line.value == "SUMMARY")
            saw_summary = true;
        else if (saw_summary && line.kind == romm3ds::ui::InfoLineKind::Body)
            ++summary_lines;
        if (line.label == "Genres" && line.value.find("more in RomM") != std::string::npos)
            saw_truncation = true;
        if (line.label == "User" && line.value.find("finished") != std::string::npos)
            saw_cached_user = true;
    }
    expect(saw_summary && summary_lines <= romm3ds::ui::kInfoSummaryLines,
           "summary is a distinct bounded section");
    expect(saw_truncation, "truncated metadata is explained in the grid");
    expect(saw_cached_user, "user values from an older cache remain visible");

    romm3ds::CatalogEntry sparse;
    sparse.metadata_flags = romm3ds::MetadataSummaryOmitted;
    const auto sparse_info = romm3ds::ui::buildInfoLines(sparse, "gba", "Not downloaded");
    bool missing_genres = false;
    bool omitted_summary = false;
    for (const auto& line : sparse_info) {
        if (line.label == "Genres" && line.value == "Not supplied by RomM") missing_genres = true;
        if (line.kind == romm3ds::ui::InfoLineKind::Body &&
            line.value == "Too long for PocketShelf; omitted.") omitted_summary = true;
    }
    expect(missing_genres && omitted_summary,
           "server absence and client omission have different bounded explanations");

    std::cout << "Text layout tests passed\n";
    return EXIT_SUCCESS;
}
