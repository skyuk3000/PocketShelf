#include "text_layout.hpp"
#include "ui_limits.hpp"

#include <algorithm>
#include <cctype>

namespace romm3ds::ui {
namespace {

std::size_t glyphBytes(std::string_view text, std::size_t offset) {
    const unsigned char ch = static_cast<unsigned char>(text[offset]);
    if ((ch & 0x80U) == 0) return 1;
    if ((ch & 0xE0U) == 0xC0U && offset + 1 < text.size()) return 2;
    if ((ch & 0xF0U) == 0xE0U && offset + 2 < text.size()) return 3;
    if ((ch & 0xF8U) == 0xF0U && offset + 3 < text.size()) return 4;
    return 1;
}

std::size_t glyphCount(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t offset = 0; offset < text.size(); ++count) {
        offset += glyphBytes(text, offset);
    }
    return count;
}

std::string takeGlyphs(std::string_view text, std::size_t count, std::size_t& offset) {
    const std::size_t start = offset;
    std::size_t taken = 0;
    while (offset < text.size() && taken < count) {
        offset += glyphBytes(text, offset);
        ++taken;
    }
    return std::string(text.substr(start, offset - start));
}

void appendEllipsis(std::string& line, std::size_t max_columns) {
    if (max_columns == 0) return;
    while (!line.empty() && glyphCount(line) >= max_columns) {
        std::size_t offset = 0;
        std::size_t previous = 0;
        while (offset < line.size()) {
            previous = offset;
            offset += glyphBytes(line, offset);
        }
        line.erase(previous);
    }
    line += "…";
}

} // namespace

std::vector<std::string> wrapText(std::string_view text, std::size_t max_columns,
                                  std::size_t max_lines) {
    std::vector<std::string> lines;
    if (max_columns == 0 || max_lines == 0) return lines;

    std::vector<std::string> words;
    std::string word;
    for (unsigned char ch : text) {
        if (std::isspace(ch)) {
            if (!word.empty()) {
                words.push_back(std::move(word));
                word.clear();
            }
        } else {
            word.push_back(static_cast<char>(ch));
        }
    }
    if (!word.empty()) words.push_back(std::move(word));

    bool truncated = false;
    for (std::size_t word_index = 0; word_index < words.size() && !truncated; ++word_index) {
        std::string_view remaining = words[word_index];
        std::size_t offset = 0;
        while (offset < remaining.size()) {
            if (lines.empty() || glyphCount(lines.back()) >= max_columns) {
                if (lines.size() >= max_lines) { truncated = true; break; }
                lines.emplace_back();
            }
            std::string& line = lines.back();
            const std::size_t used = glyphCount(line);
            const std::size_t separator = line.empty() ? 0 : 1;
            const std::size_t available = max_columns - used;
            if (separator && available <= separator) {
                if (lines.size() >= max_lines) { truncated = true; break; }
                lines.emplace_back();
                continue;
            }
            const std::size_t remaining_glyphs = glyphCount(remaining.substr(offset));
            const std::size_t room = available - separator;
            if (separator && remaining_glyphs <= room) line.push_back(' ');
            if (remaining_glyphs <= room) {
                line.append(remaining.substr(offset));
                offset = remaining.size();
            } else if (!line.empty()) {
                if (lines.size() >= max_lines) { truncated = true; break; }
                lines.emplace_back();
            } else {
                line += takeGlyphs(remaining, room, offset);
                if (offset < remaining.size() && lines.size() >= max_lines) {
                    truncated = true;
                    break;
                }
            }
        }
    }
    if (truncated && !lines.empty()) appendEllipsis(lines.back(), max_columns);
    return lines;
}

std::vector<InfoLine> buildInfoLines(const CatalogEntry& entry,
                                     std::string_view type,
                                     std::string_view download_state) {
    std::vector<InfoLine> output;
    output.reserve(32);
    output.push_back({InfoLineKind::Section, {}, "METADATA"});

    const auto append_field = [&output](std::string label, std::string value,
                                        bool truncated = false) {
        if (value.empty()) value = "Not supplied by RomM";
        if (truncated) {
            if (!value.empty() && value != "Not supplied by RomM") value += " - more in RomM";
            else value = "Too long for PocketShelf; omitted";
        }
        const auto lines = wrapText(value, kDetailsMetadataValueColumns, 4);
        if (lines.empty()) {
            output.push_back({InfoLineKind::Field, std::move(label), "Not supplied by RomM"});
            return;
        }
        output.push_back({InfoLineKind::Field, std::move(label), lines.front()});
        for (std::size_t index = 1; index < lines.size(); ++index)
            output.push_back({InfoLineKind::Continuation, {}, lines[index]});
    };

    append_field("Platform", entry.platform,
                 entry.metadata_flags & MetadataPlatformOmitted);
    append_field("Region", entry.region,
                 entry.metadata_flags & MetadataRegionsTruncated);
    append_field("Genres", entry.genres,
                 entry.metadata_flags & MetadataGenresTruncated);
    append_field("Developer", entry.developers,
                 entry.metadata_flags & MetadataDevelopersTruncated);
    append_field("Publisher", entry.publishers,
                 entry.metadata_flags & MetadataPublishersTruncated);
    append_field("Franchise", entry.franchises,
                 entry.metadata_flags & MetadataFranchisesTruncated);
    append_field("Collection", entry.collections,
                 entry.metadata_flags & MetadataCollectionsTruncated);

    std::string user;
    const bool user_supplied = (entry.metadata_flags & MetadataUserSupplied) ||
        !entry.status.empty() || entry.rating != 0 || entry.difficulty != 0 ||
        entry.completion != 0 || entry.now_playing || entry.backlogged ||
        entry.has_last_played;
    if (user_supplied) {
        if (!entry.status.empty()) user = entry.status;
        if (entry.rating != 0) user += (user.empty() ? "" : " | ") +
            std::string("rating ") + std::to_string(entry.rating) + "/10";
        if (entry.difficulty != 0) user += (user.empty() ? "" : " | ") +
            std::string("difficulty ") + std::to_string(entry.difficulty) + "/10";
        if (entry.completion != 0) user += (user.empty() ? "" : " | ") +
            std::to_string(entry.completion) + "% complete";
        if (entry.now_playing) user += (user.empty() ? "" : " | ") + std::string("now playing");
        if (entry.backlogged) user += (user.empty() ? "" : " | ") + std::string("backlogged");
        if (entry.has_last_played) user += (user.empty() ? "" : " | ") + std::string("played before");
        if (user.empty()) user = "No user values set";
    }
    append_field("User", user);
    append_field("File type", std::string(type));
    append_field("On device", std::string(download_state));

    output.push_back({InfoLineKind::Section, {}, "SUMMARY"});
    if (entry.metadata_flags & MetadataSummaryOmitted) {
        output.push_back({InfoLineKind::Body, {}, "Too long for PocketShelf; omitted."});
    } else if (entry.summary.empty()) {
        output.push_back({InfoLineKind::Body, {}, "Not supplied by RomM."});
    } else {
        const auto summary = wrapText(entry.summary, kDetailsMetadataColumns, kInfoSummaryLines);
        for (const auto& line : summary)
            output.push_back({InfoLineKind::Body, {}, line});
    }
    return output;
}

} // namespace romm3ds::ui
