#include "romm_api.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <limits>
#include <utility>

namespace romm3ds {
namespace {

constexpr std::size_t kMaxTitle = kMaxCatalogTitleBytes;
constexpr std::size_t kMaxField = kMaxCatalogFieldBytes;
constexpr std::size_t kMaxSummary = kMaxCatalogSummaryBytes;
constexpr std::size_t kMaxCoverPath = kMaxCatalogUrlBytes;
constexpr std::size_t kMaxFilesPerRom = 16;
constexpr unsigned kMaxJsonDepth = 32;

bool fail(RommParseError value, RommParseError& error) {
    error = value;
    return false;
}

void appendUtf8(std::string& output, std::uint32_t value) {
    if (value <= 0x7f) output.push_back(static_cast<char>(value));
    else if (value <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (value >> 6)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (value >> 12)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (value >> 18)));
        output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
}

class JsonReader {
public:
    explicit JsonReader(std::string_view input) : input_(input) {}

    void ws() {
        while (pos_ < input_.size() &&
               (input_[pos_] == ' ' || input_[pos_] == '\t' ||
                input_[pos_] == '\r' || input_[pos_] == '\n')) ++pos_;
    }
    bool take(char value) {
        ws();
        if (pos_ >= input_.size() || input_[pos_] != value) return false;
        ++pos_;
        return true;
    }
    bool done() { ws(); return pos_ == input_.size(); }
    bool nullValue() {
        ws();
        if (input_.substr(pos_, 4) != "null") return false;
        pos_ += 4;
        return true;
    }
    bool number(std::uint64_t& output) {
        ws();
        const std::size_t start = pos_;
        while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
        if (start == pos_) return false;
        const auto converted = std::from_chars(input_.data() + start, input_.data() + pos_, output);
        return converted.ec == std::errc{} && converted.ptr == input_.data() + pos_;
    }
    bool boolean(bool& output) {
        ws();
        if (input_.substr(pos_, 4) == "true") { pos_ += 4; output = true; return true; }
        if (input_.substr(pos_, 5) == "false") { pos_ += 5; output = false; return true; }
        return false;
    }
    bool string(std::string& output, std::size_t limit) {
        ws();
        if (pos_ >= input_.size() || input_[pos_++] != '"') return false;
        output.clear();
        while (pos_ < input_.size()) {
            unsigned char ch = static_cast<unsigned char>(input_[pos_++]);
            if (ch == '"') return true;
            if (ch < 0x20) return false;
            if (ch != '\\') {
                if (output.size() >= limit) return false;
                output.push_back(static_cast<char>(ch));
                continue;
            }
            if (pos_ >= input_.size()) return false;
            const char escaped = input_[pos_++];
            switch (escaped) {
                case '"': case '\\': case '/': ch = static_cast<unsigned char>(escaped); break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'u': {
                    std::uint32_t code = 0;
                    if (!hex4(code)) return false;
                    if (code >= 0xd800 && code <= 0xdbff) {
                        if (pos_ + 6 > input_.size() || input_[pos_] != '\\' || input_[pos_ + 1] != 'u') return false;
                        pos_ += 2;
                        std::uint32_t low = 0;
                        if (!hex4(low) || low < 0xdc00 || low > 0xdfff) return false;
                        code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                    } else if (code >= 0xdc00 && code <= 0xdfff) return false;
                    const std::size_t before = output.size();
                    appendUtf8(output, code);
                    if (output.size() > limit) { output.resize(before); return false; }
                    continue;
                }
                default: return false;
            }
            if (output.size() >= limit) return false;
            output.push_back(static_cast<char>(ch));
        }
        return false;
    }
    bool nullableString(std::string& output, std::size_t limit) {
        ws();
        if (input_.substr(pos_, 4) == "null") { pos_ += 4; output.clear(); return true; }
        return string(output, limit);
    }
    bool nullableStringOrSkip(std::string& output, std::size_t limit,
                              bool* omitted = nullptr) {
        ws();
        if (omitted != nullptr) *omitted = false;
        if (input_.substr(pos_, 4) == "null") { pos_ += 4; output.clear(); return true; }
        const std::size_t start = pos_;
        if (string(output, limit)) return true;
        pos_ = start;
        output.clear();
        if (!skipString()) return false;
        if (omitted != nullptr) *omitted = true;
        return true;
    }
    bool skipString() {
        ws();
        if (pos_ >= input_.size() || input_[pos_++] != '"') return false;
        while (pos_ < input_.size()) {
            const unsigned char ch = static_cast<unsigned char>(input_[pos_++]);
            if (ch == '"') return true;
            if (ch < 0x20) return false;
            if (ch != '\\') continue;
            if (pos_ >= input_.size()) return false;
            const char escaped = input_[pos_++];
            if (escaped == '"' || escaped == '\\' || escaped == '/' || escaped == 'b' ||
                escaped == 'f' || escaped == 'n' || escaped == 'r' || escaped == 't') continue;
            if (escaped != 'u') return false;
            std::uint32_t code = 0;
            if (!hex4(code)) return false;
            if (code >= 0xd800 && code <= 0xdbff) {
                if (pos_ + 6 > input_.size() || input_[pos_] != '\\' || input_[pos_ + 1] != 'u') return false;
                pos_ += 2;
                std::uint32_t low = 0;
                if (!hex4(low) || low < 0xdc00 || low > 0xdfff) return false;
            } else if (code >= 0xdc00 && code <= 0xdfff) return false;
        }
        return false;
    }
    bool skipValue(unsigned depth = 0) {
        if (depth > kMaxJsonDepth) return false;
        ws();
        if (pos_ >= input_.size()) return false;
        if (input_[pos_] == '"') return skipString();
        if (input_[pos_] == '{') {
            ++pos_; ws();
            if (take('}')) return true;
            while (true) {
                std::string key;
                if (!string(key, 256) || !take(':') || !skipValue(depth + 1)) return false;
                if (take('}')) return true;
                if (!take(',')) return false;
            }
        }
        if (input_[pos_] == '[') {
            ++pos_; ws();
            if (take(']')) return true;
            while (true) {
                if (!skipValue(depth + 1)) return false;
                if (take(']')) return true;
                if (!take(',')) return false;
            }
        }
        const std::size_t start = pos_;
        while (pos_ < input_.size()) {
            const char ch = input_[pos_];
            if (ch == ',' || ch == '}' || ch == ']' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') break;
            ++pos_;
        }
        return pos_ > start;
    }

private:
    bool hex4(std::uint32_t& value) {
        if (pos_ + 4 > input_.size()) return false;
        value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const unsigned char ch = static_cast<unsigned char>(input_[pos_++]);
            value <<= 4;
            if (ch >= '0' && ch <= '9') value |= ch - '0';
            else if (ch >= 'a' && ch <= 'f') value |= ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') value |= ch - 'A' + 10;
            else return false;
        }
        return true;
    }

    std::string_view input_;
    std::size_t pos_ = 0;
};

struct RomFile {
    std::uint64_t id = 0;
    std::uint64_t rom_id = 0;
    std::uint64_t size = 0;
    std::string name;
};

bool parseStringArray(JsonReader& reader, std::string& output, bool& truncated) {
    truncated = false;
    if (reader.nullValue()) { output.clear(); return true; }
    if (!reader.take('[')) return false;
    output.clear();
    if (reader.take(']')) return true;
    unsigned retained = 0;
    bool at_capacity = false;
    while (true) {
        std::string value;
        bool omitted = false;
        if (!reader.nullableStringOrSkip(value, 64, &omitted)) return false;
        truncated = truncated || omitted;
        if (!at_capacity && retained < 8 && !value.empty()) {
            const std::size_t separator = output.empty() ? 0 : 2;
            if (output.size() + separator + value.size() <= kMaxField) {
                if (separator != 0) output += ", ";
                output += value;
                ++retained;
            } else {
                at_capacity = true;
                truncated = true;
            }
        } else if (!value.empty()) truncated = true;
        if (reader.take(']')) return true;
        if (!reader.take(',')) return false;
    }
}

bool parseMetadata(JsonReader& reader, std::string& genres, std::string& franchises,
                   std::string& collections, std::string& publishers,
                   std::string& developers, std::uint16_t& flags) {
    if (reader.nullValue()) return true;
    if (!reader.take('{')) return false;
    if (reader.take('}')) return true;
    while (true) {
        std::string key;
        if (!reader.string(key, 64) || !reader.take(':')) return false;
        bool truncated = false;
        if (key == "genres") {
            if (!parseStringArray(reader, genres, truncated)) return false;
            if (truncated) flags |= MetadataGenresTruncated;
        } else if (key == "franchises") {
            if (!parseStringArray(reader, franchises, truncated)) return false;
            if (truncated) flags |= MetadataFranchisesTruncated;
        } else if (key == "collections") {
            if (!parseStringArray(reader, collections, truncated)) return false;
            if (truncated) flags |= MetadataCollectionsTruncated;
        } else if (key == "publishers") {
            if (!parseStringArray(reader, publishers, truncated)) return false;
            if (truncated) flags |= MetadataPublishersTruncated;
        } else if (key == "developers") {
            if (!parseStringArray(reader, developers, truncated)) return false;
            if (truncated) flags |= MetadataDevelopersTruncated;
        }
        else if (!reader.skipValue()) return false;
        if (reader.take('}')) return true;
        if (!reader.take(',')) return false;
    }
}

bool parseMedia(JsonReader& reader, std::vector<std::string>& media) {
    if (!reader.take('[')) return false;
    media.clear();
    if (reader.take(']')) return true;
    while (true) {
        std::string value;
        if (!reader.nullableStringOrSkip(value, kMaxCoverPath)) return false;
        if (media.size() < kMaxScreenshots && !value.empty()) media.push_back(std::move(value));
        if (reader.take(']')) return true;
        if (!reader.take(',')) return false;
    }
}

bool parseRomUser(JsonReader& reader, std::string& status, std::uint8_t& rating,
                  std::uint8_t& difficulty, std::uint8_t& completion,
                  bool& now_playing, bool& backlogged, bool& has_last_played,
                  bool& supplied) {
    supplied = false;
    if (reader.nullValue()) { status.clear(); return true; }
    if (!reader.take('{')) return false;
    supplied = true;
    if (reader.take('}')) return true;
    while (true) {
        std::string key;
        if (!reader.string(key, 64) || !reader.take(':')) return false;
        if (key == "status") {
            if (!reader.nullableString(status, 32)) return false;
        } else if (key == "rating" || key == "difficulty" || key == "completion") {
            std::uint64_t value = 0;
            if (!reader.number(value)) return false;
            const std::uint64_t maximum = key == "completion" ? 100 : 10;
            if (value > maximum) return false;
            if (key == "rating") rating = static_cast<std::uint8_t>(value);
            else if (key == "difficulty") difficulty = static_cast<std::uint8_t>(value);
            else completion = static_cast<std::uint8_t>(value);
        } else if (key == "now_playing") {
            if (!reader.boolean(now_playing)) return false;
        } else if (key == "backlogged") {
            if (!reader.boolean(backlogged)) return false;
        } else if (key == "last_played") {
            if (reader.nullValue()) has_last_played = false;
            else {
                std::string value;
                if (!reader.string(value, 64)) return false;
                has_last_played = !value.empty();
            }
        } else if (!reader.skipValue()) return false;
        if (reader.take('}')) return true;
        if (!reader.take(',')) return false;
    }
}

bool parseFile(JsonReader& reader, RomFile& file) {
    if (!reader.take('{')) return false;
    bool id = false, rom = false, name = false, size = false;
    if (reader.take('}')) return false;
    while (true) {
        std::string key;
        if (!reader.string(key, 64) || !reader.take(':')) return false;
        if (key == "id") { if (!reader.number(file.id)) return false; id = true; }
        else if (key == "rom_id") { if (!reader.number(file.rom_id)) return false; rom = true; }
        else if (key == "file_name") { if (!reader.string(file.name, kMaxField)) return false; name = true; }
        else if (key == "file_size_bytes") { if (!reader.number(file.size)) return false; size = true; }
        else if (!reader.skipValue()) return false;
        if (reader.take('}')) break;
        if (!reader.take(',')) return false;
    }
    return id && rom && name && size;
}

bool parseFiles(JsonReader& reader, std::vector<RomFile>& files) {
    if (!reader.take('[')) return false;
    files.clear();
    if (reader.take(']')) return true;
    while (true) {
        if (files.size() >= kMaxFilesPerRom) return false;
        RomFile file;
        if (!parseFile(reader, file)) return false;
        files.push_back(std::move(file));
        if (reader.take(']')) return true;
        if (!reader.take(',')) return false;
    }
}

std::string extensionOf(std::string_view name) {
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 == name.size()) return {};
    std::string extension(name.substr(dot + 1));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension;
}

bool supportedType(std::string_view type) {
    constexpr std::array<std::string_view, 26> values = {"cia","3ds","3dsx","nds","gba","gb","gbc","nes","smc","sfc","n64","z64","v64","bin","cue","iso","chd","pbp","gg","sms","gen","md","32x","pce","ws","wsc"};
    return std::find(values.begin(), values.end(), type) != values.end();
}

bool parseRom(JsonReader& reader, std::vector<CatalogEntry>& entries, RommParseError& error) {
    if (!reader.take('{')) return fail(RommParseError::MalformedJson, error);
    std::uint64_t rom_id = 0;
    std::string title, fallback_title, platform, summary, regions;
    std::string cover_small, cover_large, cover_url, status;
    std::vector<std::string> screenshots;
    std::string genres, franchises, collections, publishers, developers;
    std::uint8_t rating = 0, difficulty = 0, completion = 0;
    bool now_playing = false, backlogged = false, has_last_played = false;
    bool user_supplied = false;
    std::uint16_t metadata_flags = 0;
    std::vector<RomFile> files;
    bool found_id = false;
    if (reader.take('}')) return fail(RommParseError::MissingField, error);
    while (true) {
        std::string key;
        if (!reader.string(key, 64) || !reader.take(':')) return fail(RommParseError::MalformedJson, error);
        if (key == "id") { if (!reader.number(rom_id)) return fail(RommParseError::MalformedJson, error); found_id = true; }
        else if (key == "name") { if (!reader.nullableString(title, kMaxTitle)) return fail(RommParseError::FieldTooLong, error); }
        else if (key == "fs_name_no_ext") { if (!reader.string(fallback_title, kMaxTitle)) return fail(RommParseError::FieldTooLong, error); }
        else if (key == "platform_display_name") {
            bool omitted = false;
            if (!reader.nullableStringOrSkip(platform, kMaxField, &omitted)) return fail(RommParseError::MalformedJson, error);
            if (omitted) metadata_flags |= MetadataPlatformOmitted;
        }
        else if (key == "summary") {
            bool omitted = false;
            if (!reader.nullableStringOrSkip(summary, kMaxSummary, &omitted)) return fail(RommParseError::MalformedJson, error);
            if (omitted) metadata_flags |= MetadataSummaryOmitted;
        }
        else if (key == "regions") {
            bool truncated = false;
            if (!parseStringArray(reader, regions, truncated)) return fail(RommParseError::FieldTooLong, error);
            if (truncated) metadata_flags |= MetadataRegionsTruncated;
        }
        else if (key == "metadatum") { if (!parseMetadata(reader, genres, franchises, collections, publishers, developers, metadata_flags)) return fail(RommParseError::FieldTooLong, error); }
        else if (key == "path_cover_small") { if (!reader.nullableStringOrSkip(cover_small, kMaxCoverPath)) return fail(RommParseError::MalformedJson, error); }
        else if (key == "path_cover_large") { if (!reader.nullableStringOrSkip(cover_large, kMaxCoverPath)) return fail(RommParseError::MalformedJson, error); }
        else if (key == "url_cover") { if (!reader.nullableStringOrSkip(cover_url, kMaxCoverPath)) return fail(RommParseError::MalformedJson, error); }
        else if (key == "merged_screenshots") { if (!parseMedia(reader, screenshots)) return fail(RommParseError::MalformedJson, error); }
        else if (key == "rom_user") { if (!parseRomUser(reader, status, rating, difficulty, completion, now_playing, backlogged, has_last_played, user_supplied)) return fail(RommParseError::MalformedJson, error); }
        else if (key == "files") { if (!parseFiles(reader, files)) return fail(RommParseError::MalformedJson, error); }
        else if (!reader.skipValue()) return fail(RommParseError::MalformedJson, error);
        if (reader.take('}')) break;
        if (!reader.take(',')) return fail(RommParseError::MalformedJson, error);
    }
    if (!found_id) return fail(RommParseError::MissingField, error);
    if (title.empty()) title = fallback_title;
    if (title.empty()) return fail(RommParseError::MissingField, error);
    for (const RomFile& file : files) {
        const std::string type = extensionOf(file.name);
        if (!supportedType(type)) continue;
        if (!isSafeDownloadFilename(file.name)) return fail(RommParseError::UnsafeFilename, error);
        if (file.rom_id != rom_id || file.id == 0) return fail(RommParseError::MissingField, error);
        if (entries.size() >= kMaxRommPageEntries) return fail(RommParseError::TooManyEntries, error);
        CatalogEntry entry;
        entry.rom_id = rom_id;
        entry.file_id = file.id;
        entry.size = file.size;
        entry.title = title;
        entry.platform = platform;
        entry.region = regions;
        entry.file_name = file.name;
        entry.type = type;
        entry.summary = summary;
        entry.status = status;
        entry.genres = genres;
        entry.franchises = franchises;
        entry.collections = collections;
        entry.publishers = publishers;
        entry.developers = developers;
        entry.screenshots = screenshots;
        entry.rating = rating;
        entry.difficulty = difficulty;
        entry.completion = completion;
        entry.now_playing = now_playing;
        entry.backlogged = backlogged;
        entry.has_last_played = has_last_played;
        entry.metadata_flags = metadata_flags |
            (user_supplied ? MetadataUserSupplied : 0);
        entry.cover_url = !cover_small.empty() ? cover_small :
                          (!cover_large.empty() ? cover_large : cover_url);
        entry.action = type == "cia" ? CatalogAction::InstallCia : CatalogAction::Download;
        entries.push_back(std::move(entry));
    }
    return true;
}

bool parseItems(JsonReader& reader, std::vector<CatalogEntry>& entries, std::size_t& item_count,
                RommParseError& error) {
    if (!reader.take('[')) return fail(RommParseError::MalformedJson, error);
    entries.clear();
    item_count = 0;
    if (reader.take(']')) return true;
    while (true) {
        if (!parseRom(reader, entries, error)) return false;
        ++item_count;
        if (reader.take(']')) return true;
        if (!reader.take(',')) return fail(RommParseError::MalformedJson, error);
    }
}

std::string trimBase(std::string_view base) {
    while (!base.empty() && base.back() == '/') base.remove_suffix(1);
    return std::string(base);
}

bool unreserved(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

std::string encode(std::string_view value, bool preserve_slash_and_percent) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string output;
    output.reserve(value.size() + 16);
    for (unsigned char ch : value) {
        if (unreserved(ch) || (preserve_slash_and_percent && (ch == '/' || ch == '%'))) output.push_back(static_cast<char>(ch));
        else {
            output.push_back('%');
            output.push_back(hex[ch >> 4]);
            output.push_back(hex[ch & 15]);
        }
    }
    return output;
}

} // namespace

bool parseRommPage(std::string_view json, RommPage& output, RommParseError& error) {
    if (json.size() > kMaxRommPageBytes) return fail(RommParseError::PageTooLarge, error);
    JsonReader reader(json);
    RommPage parsed;
    bool items = false, total = false, limit = false, offset = false;
    if (!reader.take('{')) return fail(RommParseError::MalformedJson, error);
    if (reader.take('}')) return fail(RommParseError::MissingField, error);
    while (true) {
        std::string key;
        if (!reader.string(key, 64) || !reader.take(':')) return fail(RommParseError::MalformedJson, error);
        if (key == "items") { if (!parseItems(reader, parsed.entries, parsed.item_count, error)) return false; items = true; }
        else if (key == "total") {
            if (reader.nullValue()) parsed.total = 0;
            else if (!reader.number(parsed.total)) return fail(RommParseError::MalformedJson, error);
            total = true;
        } else if (key == "limit") { if (!reader.number(parsed.limit)) return fail(RommParseError::MalformedJson, error); limit = true; }
        else if (key == "offset") { if (!reader.number(parsed.offset)) return fail(RommParseError::MalformedJson, error); offset = true; }
        else if (!reader.skipValue()) return fail(RommParseError::MalformedJson, error);
        if (reader.take('}')) break;
        if (!reader.take(',')) return fail(RommParseError::MalformedJson, error);
    }
    if (!reader.done()) return fail(RommParseError::MalformedJson, error);
    if (!(items && total && limit && offset) || parsed.limit == 0) return fail(RommParseError::MissingField, error);
    output = std::move(parsed);
    error = RommParseError::None;
    return true;
}

const char* rommParseErrorMessage(RommParseError error) noexcept {
    switch (error) {
        case RommParseError::None: return "no error";
        case RommParseError::PageTooLarge: return "RomM page exceeds 768 KiB";
        case RommParseError::MalformedJson: return "malformed RomM page JSON";
        case RommParseError::MissingField: return "RomM page is missing required data";
        case RommParseError::FieldTooLong: return "RomM catalogue field exceeds its limit";
        case RommParseError::TooManyEntries: return "RomM page contains too many files";
        case RommParseError::UnsafeFilename: return "RomM returned an unsafe filename";
    }
    return "RomM parse error";
}

bool parseRommFilters(std::string_view text, RommFilters& output) {
    output = {};
    if (text.size() > 96) return false;
    const auto trim = [](std::string_view value) {
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
        return value;
    };
    text = trim(text);
    if (text.empty() || text == "clear") return true;

    unsigned terms = 0;
    while (!text.empty()) {
        const std::size_t separator = text.find(';');
        const std::string_view term = trim(text.substr(0, separator));
        if (term.empty() || ++terms > 5) return false;
        const std::size_t equals = term.find('=');
        if (equals == std::string_view::npos || equals == 0) return false;
        const std::string_view key = trim(term.substr(0, equals));
        const std::string_view value = trim(term.substr(equals + 1));
        if (value.empty() || value.size() > 64 ||
            std::any_of(value.begin(), value.end(), [](unsigned char ch) { return ch < 0x20 || ch == 0x7f; }))
            return false;

        std::string* target = nullptr;
        if (key == "genre") target = &output.genre;
        else if (key == "developer") target = &output.developer;
        else if (key == "publisher") target = &output.publisher;
        else if (key == "franchise") target = &output.franchise;
        else if (key == "collection") target = &output.collection;
        else return false;
        if (!target->empty()) return false;
        target->assign(value.data(), value.size());

        if (separator == std::string_view::npos) break;
        text.remove_prefix(separator + 1);
        text = trim(text);
    }
    return true;
}

std::string rommCatalogueUrl(std::string_view base_url, std::size_t limit,
                             std::size_t offset, std::string_view search,
                             bool include_total, const RommFilters& filters) {
    char page[64];
    std::snprintf(page, sizeof(page), "?limit=%lu&offset=%lu", static_cast<unsigned long>(limit),
                  static_cast<unsigned long>(offset));
    std::string url = trimBase(base_url) + "/api/roms" + page;
    url += "&with_files=true&with_char_index=false&with_filter_values=false&with_rom_id_index=false&with_total=";
    url += include_total ? "true" : "false";
    if (!search.empty()) url += "&search_term=" + encode(search, false);
    if (!filters.genre.empty()) url += "&genres=" + encode(filters.genre, false);
    if (!filters.developer.empty()) url += "&developers=" + encode(filters.developer, false);
    if (!filters.publisher.empty()) url += "&publishers=" + encode(filters.publisher, false);
    if (!filters.franchise.empty()) url += "&franchises=" + encode(filters.franchise, false);
    if (!filters.collection.empty()) url += "&collections=" + encode(filters.collection, false);
    return url;
}

std::string rommFileContentUrl(std::string_view base_url, std::uint64_t file_id,
                               std::string_view file_name) {
    char id[32];
    std::snprintf(id, sizeof(id), "%llu", static_cast<unsigned long long>(file_id));
    return trimBase(base_url) + "/api/roms/" + id + "/files/content/" + encode(file_name, false);
}

std::string rommCoverUrl(std::string_view base_url, std::string_view cover_path) {
    if (cover_path.empty()) return {};
    const std::string base = trimBase(base_url);
    if (cover_path.compare(0, 7, "http://") == 0) {
        if (cover_path.size() <= base.size() || cover_path.compare(0, base.size(), base) != 0 ||
            cover_path[base.size()] != '/') return {};
        cover_path.remove_prefix(base.size());
    }
    constexpr std::string_view prefix = "/assets/romm/resources/roms/";
    if (cover_path.compare(0, prefix.size(), prefix) != 0 ||
        cover_path.find('#') != std::string_view::npos ||
        cover_path.find("/../") != std::string_view::npos) return {};
    const std::string_view resource = cover_path.substr(prefix.size());
    const std::size_t platform_end = resource.find('/');
    const std::size_t rom_end = platform_end == std::string_view::npos ?
        std::string_view::npos : resource.find('/', platform_end + 1);
    const auto numeric = [](std::string_view value) {
        return !value.empty() && std::all_of(value.begin(), value.end(),
            [](unsigned char ch) { return ch >= '0' && ch <= '9'; });
    };
    if (platform_end == std::string_view::npos || rom_end == std::string_view::npos ||
        !numeric(resource.substr(0, platform_end)) ||
        !numeric(resource.substr(platform_end + 1, rom_end - platform_end - 1)) ||
        resource.compare(rom_end, 7, "/cover/") != 0) return {};
    const std::string_view cover_resource = resource.substr(rom_end + 7);
    const std::size_t query = cover_resource.find('?');
    const std::string_view file_name = cover_resource.substr(0, query);
    if (file_name.empty() || file_name.find('/') != std::string_view::npos ||
        file_name == "." || file_name == "..") return {};
    std::string lowered(cover_path);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (lowered.find("%2e") != std::string::npos ||
        lowered.find("%2f") != std::string::npos ||
        lowered.find("%5c") != std::string::npos) return {};
    const std::size_t path_query = cover_path.find('?');
    for (std::size_t index = 0; index < cover_path.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(cover_path[index]);
        if (ch < 0x20 || ch == 0x7f ||
            (ch == ' ' && (path_query == std::string_view::npos || index < path_query))) return {};
    }
    // RomM 5.4 formats updated_at through Python's datetime string form, so
    // the cache-busting query can contain a raw space and '+' timezone sign.
    // Keep it as a query, but encode those characters before httpc sees it.
    std::string normalized;
    normalized.reserve(base.size() + cover_path.size() + 8);
    normalized += base;
    for (std::size_t index = 0; index < cover_path.size(); ++index) {
        const char ch = cover_path[index];
        if (path_query != std::string_view::npos && index > path_query && ch == ' ') normalized += "%20";
        else if (path_query != std::string_view::npos && index > path_query && ch == '+') normalized += "%2B";
        else normalized.push_back(ch);
    }
    return normalized;
}

std::string rommMediaUrl(std::string_view base_url, std::uint64_t rom_id,
                         std::string_view media_path) {
    if (media_path.empty() || rom_id == 0) return {};
    const std::string base = trimBase(base_url);
    if (media_path.compare(0, 7, "http://") == 0) {
        if (media_path.size() <= base.size() || media_path.compare(0, base.size(), base) != 0 ||
            media_path[base.size()] != '/') return {};
        media_path.remove_prefix(base.size());
    }
    constexpr std::string_view prefix = "/assets/romm/resources/roms/";
    if (media_path.compare(0, prefix.size(), prefix) != 0 ||
        media_path.find('#') != std::string_view::npos ||
        media_path.find("/../") != std::string_view::npos) return {};
    const std::string_view resource = media_path.substr(prefix.size());
    const std::size_t platform_end = resource.find('/');
    const std::size_t rom_end = platform_end == std::string_view::npos ?
        std::string_view::npos : resource.find('/', platform_end + 1);
    if (platform_end == std::string_view::npos || rom_end == std::string_view::npos) return {};
    const auto numeric = [](std::string_view value) {
        return !value.empty() && std::all_of(value.begin(), value.end(),
            [](unsigned char ch) { return ch >= '0' && ch <= '9'; });
    };
    std::uint64_t path_rom_id = 0;
    const std::string_view rom_text = resource.substr(platform_end + 1, rom_end - platform_end - 1);
    const auto converted = std::from_chars(rom_text.data(), rom_text.data() + rom_text.size(), path_rom_id);
    if (!numeric(resource.substr(0, platform_end)) || converted.ec != std::errc{} ||
        converted.ptr != rom_text.data() + rom_text.size() || path_rom_id != rom_id ||
        resource.compare(rom_end, 13, "/screenshots/") != 0) return {};
    const std::string_view screenshot = resource.substr(rom_end + 13);
    const std::size_t query = screenshot.find('?');
    const std::string_view file_name = screenshot.substr(0, query);
    if (file_name.empty() || file_name.find('/') != std::string_view::npos ||
        file_name == "." || file_name == "..") return {};
    std::string lowered(media_path);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (lowered.find("%2e") != std::string::npos || lowered.find("%2f") != std::string::npos ||
        lowered.find("%5c") != std::string::npos) return {};
    const std::size_t path_query = media_path.find('?');
    std::string normalized = base;
    for (std::size_t index = 0; index < media_path.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(media_path[index]);
        if (ch < 0x20 || ch == 0x7f ||
            (ch == ' ' && (path_query == std::string_view::npos || index < path_query))) return {};
        if (path_query != std::string_view::npos && index > path_query && ch == ' ') normalized += "%20";
        else if (path_query != std::string_view::npos && index > path_query && ch == '+') normalized += "%2B";
        else normalized.push_back(static_cast<char>(ch));
    }
    return normalized;
}

std::vector<std::string> rommScreenshotUrls(
    std::string_view base_url, std::uint64_t rom_id,
    const std::vector<std::string>& media_paths) {
    std::vector<std::string> output;
    output.reserve(std::min(media_paths.size(), kMaxScreenshots));
    for (const auto& path : media_paths) {
        if (output.size() >= kMaxScreenshots) break;
        std::string url = rommMediaUrl(base_url, rom_id, path);
        if (!url.empty()) output.push_back(std::move(url));
    }
    return output;
}

std::string rommUserPropsUrl(std::string_view base_url, std::uint64_t rom_id) {
    if (rom_id == 0) return {};
    return trimBase(base_url) + "/api/roms/" + std::to_string(rom_id) + "/props";
}

std::string rommLastPlayedUrl(std::string_view base_url, std::uint64_t rom_id, bool played) {
    const std::string url = rommUserPropsUrl(base_url, rom_id);
    if (url.empty()) return {};
    return url + (played ? "?update_last_played=true" : "?remove_last_played=true");
}

std::string rommUserPropsBody(RommUserAction action, unsigned value) {
    switch (action) {
        case RommUserAction::NowPlaying:
            if (value > 1) return {};
            return value ? "{\"now_playing\":true}" : "{\"now_playing\":false}";
        case RommUserAction::Backlogged:
            if (value > 1) return {};
            return value ? "{\"backlogged\":true}" : "{\"backlogged\":false}";
        case RommUserAction::StatusClear: return "{\"status\":null}";
        case RommUserAction::StatusIncomplete: return "{\"status\":\"incomplete\"}";
        case RommUserAction::StatusFinished: return "{\"status\":\"finished\"}";
        case RommUserAction::StatusCompleted100: return "{\"status\":\"completed_100\"}";
        case RommUserAction::StatusRetired: return "{\"status\":\"retired\"}";
        case RommUserAction::StatusNeverPlaying: return "{\"status\":\"never_playing\"}";
        case RommUserAction::Rating:
            if (value > 10) return {};
            return "{\"rating\":" + std::to_string(value) + "}";
        case RommUserAction::Difficulty:
            if (value > 10) return {};
            return "{\"difficulty\":" + std::to_string(value) + "}";
        case RommUserAction::Completion:
            if (value > 100) return {};
            return "{\"completion\":" + std::to_string(value) + "}";
    }
    return {};
}

} // namespace romm3ds
