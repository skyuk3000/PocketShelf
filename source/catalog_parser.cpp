#include "catalog_parser.hpp"

#include <array>
#include <charconv>
#include <utility>

namespace romm3ds {
namespace {
constexpr std::size_t kMaxTitle = kMaxCatalogTitleBytes;
constexpr std::size_t kMaxField = kMaxCatalogFieldBytes;
constexpr std::size_t kMaxSummary = kMaxCatalogSummaryBytes;
constexpr std::size_t kMaxUrl = kMaxCatalogUrlBytes;

class JsonReader {
public:
    explicit JsonReader(std::string_view input) : input_(input) {}
    void ws() { while (pos_ < input_.size() && (input_[pos_] == ' ' || input_[pos_] == '\t' || input_[pos_] == '\r')) ++pos_; }
    bool take(char value) { ws(); if (pos_ >= input_.size() || input_[pos_] != value) return false; ++pos_; return true; }
    bool done() { ws(); return pos_ == input_.size(); }
    bool string(std::string& output, std::size_t limit) {
        ws();
        if (pos_ >= input_.size() || input_[pos_++] != '"') return false;
        output.clear();
        while (pos_ < input_.size()) {
            unsigned char ch = static_cast<unsigned char>(input_[pos_++]);
            if (ch == '"') return true;
            if (ch < 0x20) return false;
            if (ch == '\\') {
                if (pos_ >= input_.size()) return false;
                const char escaped = input_[pos_++];
                switch (escaped) {
                    case '"': case '\\': case '/': ch = static_cast<unsigned char>(escaped); break;
                    case 'b': ch = '\b'; break; case 'f': ch = '\f'; break;
                    case 'n': ch = '\n'; break; case 'r': ch = '\r'; break; case 't': ch = '\t'; break;
                    default: return false; // Bridge emits UTF-8 directly; \u is deliberately unsupported.
                }
            }
            if (output.size() >= limit) return false;
            output.push_back(static_cast<char>(ch));
        }
        return false;
    }
    bool stringOrSkip(std::string& output, std::size_t limit, bool& omitted) {
        omitted = false;
        const std::size_t start = pos_;
        if (string(output, limit)) return true;
        pos_ = start;
        output.clear();
        if (!skipString()) return false;
        omitted = true;
        return true;
    }
    bool number(std::uint64_t& output) {
        ws(); const std::size_t start = pos_;
        while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
        if (start == pos_) return false;
        const auto result = std::from_chars(input_.data() + start, input_.data() + pos_, output);
        return result.ec == std::errc{};
    }
    bool boolean(bool& output) {
        ws();
        if (input_.substr(pos_, 4) == "true") { pos_ += 4; output = true; return true; }
        if (input_.substr(pos_, 5) == "false") { pos_ += 5; output = false; return true; }
        return false;
    }
    bool skip() {
        ws(); if (pos_ >= input_.size()) return false;
        if (input_[pos_] == '"') { std::string ignored; return string(ignored, kMaxCatalogLineBytes); }
        if (input_[pos_] == '{' || input_[pos_] == '[') {
            const char open = input_[pos_++], close = open == '{' ? '}' : ']'; int depth = 1; bool quoted = false, escape = false;
            while (pos_ < input_.size()) {
                const char ch = input_[pos_++];
                if (quoted) { if (escape) escape = false; else if (ch == '\\') escape = true; else if (ch == '"') quoted = false; continue; }
                if (ch == '"') quoted = true; else if (ch == open) ++depth; else if (ch == close && --depth == 0) return true;
            }
            return false;
        }
        const std::size_t start = pos_;
        while (pos_ < input_.size() && input_[pos_] != ',' && input_[pos_] != '}') ++pos_;
        return pos_ > start;
    }
    bool regions(std::string& output, bool& truncated) {
        truncated = false;
        if (!take('[')) return false;
        output.clear(); ws(); if (take(']')) return true;
        unsigned retained = 0;
        while (true) {
            std::string value;
            bool omitted = false;
            if (!stringOrSkip(value, 64, omitted)) return false;
            truncated = truncated || omitted;
            if (!value.empty() && retained < 8) {
                if (!output.empty()) output += ", ";
                output += value;
                ++retained;
            } else if (!value.empty()) truncated = true;
            ws(); if (take(']')) return true;
            if (!take(',')) return false;
        }
    }
    bool strings(std::vector<std::string>& output, std::size_t item_limit,
                 std::size_t string_limit) {
        if (!take('[')) return false;
        output.clear(); ws(); if (take(']')) return true;
        while (true) {
            std::string value;
            if (!string(value, string_limit)) return false;
            if (output.size() < item_limit && !value.empty()) output.push_back(std::move(value));
            ws(); if (take(']')) return true;
            if (!take(',')) return false;
        }
    }
private:
    bool skipString() {
        ws();
        if (pos_ >= input_.size() || input_[pos_++] != '"') return false;
        while (pos_ < input_.size()) {
            const unsigned char ch = static_cast<unsigned char>(input_[pos_++]);
            if (ch == '"') return true;
            if (ch < 0x20) return false;
            if (ch == '\\') {
                if (pos_ >= input_.size()) return false;
                const char escaped = input_[pos_++];
                if (escaped != '"' && escaped != '\\' && escaped != '/' && escaped != 'b' &&
                    escaped != 'f' && escaped != 'n' && escaped != 'r' && escaped != 't') return false;
            }
        }
        return false;
    }
    std::string_view input_;
    std::size_t pos_ = 0;
};

bool supportedType(std::string_view type) {
    constexpr std::array<std::string_view, 26> values = {"cia","3ds","3dsx","nds","gba","gb","gbc","nes","smc","sfc","n64","z64","v64","bin","cue","iso","chd","pbp","gg","sms","gen","md","32x","pce","ws","wsc"};
    for (auto value : values) if (type == value) return true;
    return false;
}
} // namespace

bool isSafeDownloadFilename(std::string_view value) noexcept {
    if (value.empty() || value == "." || value == ".." || value.find('/') != std::string_view::npos || value.find('\\') != std::string_view::npos) return false;
    for (unsigned char ch : value) if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}

std::string downloadDirectoryForType(std::string_view type) {
    if (type == "3ds" || type == "3dsx" || type == "cia") return "sdmc:/3ds/PocketShelf/downloads/3ds";
    return "sdmc:/roms/" + std::string(type);
}

bool CatalogParser::fail(CatalogParseError error) { error_ = error; error_line_ = line_number_; return false; }

bool CatalogParser::parseLine() {
    if (!line_.empty() && line_.back() == '\r') line_.pop_back();
    if (line_.empty()) return true;
    JsonReader reader(line_); CatalogEntry entry;
    bool rom = false, file = false, title = false, name = false, type = false, action = false, url = false;
    if (!reader.take('{')) return fail(CatalogParseError::MalformedJson);
    bool first = true;
    while (true) {
        if (reader.take('}')) break;
        if (!first && !reader.take(',')) return fail(CatalogParseError::MalformedJson);
        first = false;
        std::string key;
        if (!reader.string(key, 32) || !reader.take(':')) return fail(CatalogParseError::MalformedJson);
        if (key == "rom_id") { if (!reader.number(entry.rom_id)) return fail(CatalogParseError::MalformedJson); rom = true; }
        else if (key == "file_id") { if (!reader.number(entry.file_id)) return fail(CatalogParseError::MalformedJson); file = true; }
        else if (key == "size") { if (!reader.number(entry.size)) return fail(CatalogParseError::MalformedJson); }
        else if (key == "favorite") { if (!reader.boolean(entry.favorite)) return fail(CatalogParseError::MalformedJson); }
        else if (key == "title") { if (!reader.string(entry.title, kMaxTitle)) return fail(CatalogParseError::FieldTooLong); title = true; }
        else if (key == "platform") { bool omitted = false; if (!reader.stringOrSkip(entry.platform, kMaxField, omitted)) return fail(CatalogParseError::MalformedJson); if (omitted) entry.metadata_flags |= MetadataPlatformOmitted; }
        else if (key == "regions") { bool truncated = false; if (!reader.regions(entry.region, truncated)) return fail(CatalogParseError::MalformedJson); if (truncated) entry.metadata_flags |= MetadataRegionsTruncated; }
        else if (key == "file_name") { if (!reader.string(entry.file_name, kMaxField)) return fail(CatalogParseError::FieldTooLong); name = true; }
        else if (key == "type") { if (!reader.string(entry.type, 16)) return fail(CatalogParseError::FieldTooLong); type = true; }
        else if (key == "summary") { bool omitted = false; if (!reader.stringOrSkip(entry.summary, kMaxSummary, omitted)) return fail(CatalogParseError::MalformedJson); if (omitted) entry.metadata_flags |= MetadataSummaryOmitted; }
        else if (key == "status") { if (!reader.string(entry.status, 32)) return fail(CatalogParseError::FieldTooLong); }
        else if (key == "genres") { bool omitted = false; if (!reader.stringOrSkip(entry.genres, kMaxField, omitted)) return fail(CatalogParseError::MalformedJson); if (omitted) entry.metadata_flags |= MetadataGenresTruncated; }
        else if (key == "franchises") { bool omitted = false; if (!reader.stringOrSkip(entry.franchises, kMaxField, omitted)) return fail(CatalogParseError::MalformedJson); if (omitted) entry.metadata_flags |= MetadataFranchisesTruncated; }
        else if (key == "collections") { bool omitted = false; if (!reader.stringOrSkip(entry.collections, kMaxField, omitted)) return fail(CatalogParseError::MalformedJson); if (omitted) entry.metadata_flags |= MetadataCollectionsTruncated; }
        else if (key == "publishers") { bool omitted = false; if (!reader.stringOrSkip(entry.publishers, kMaxField, omitted)) return fail(CatalogParseError::MalformedJson); if (omitted) entry.metadata_flags |= MetadataPublishersTruncated; }
        else if (key == "developers") { bool omitted = false; if (!reader.stringOrSkip(entry.developers, kMaxField, omitted)) return fail(CatalogParseError::MalformedJson); if (omitted) entry.metadata_flags |= MetadataDevelopersTruncated; }
        else if (key == "metadata_flags") { std::uint64_t value = 0; if (!reader.number(value) || value > kAllMetadataFlags) return fail(CatalogParseError::MalformedJson); entry.metadata_flags |= static_cast<std::uint16_t>(value); }
        else if (key == "rating" || key == "difficulty" || key == "completion") {
            std::uint64_t value = 0;
            if (!reader.number(value) || value > (key == "completion" ? 100U : 10U))
                return fail(CatalogParseError::MalformedJson);
            if (key == "rating") entry.rating = static_cast<std::uint8_t>(value);
            else if (key == "difficulty") entry.difficulty = static_cast<std::uint8_t>(value);
            else entry.completion = static_cast<std::uint8_t>(value);
        }
        else if (key == "now_playing") { if (!reader.boolean(entry.now_playing)) return fail(CatalogParseError::MalformedJson); }
        else if (key == "backlogged") { if (!reader.boolean(entry.backlogged)) return fail(CatalogParseError::MalformedJson); }
        else if (key == "has_last_played") { if (!reader.boolean(entry.has_last_played)) return fail(CatalogParseError::MalformedJson); }
        else if (key == "download_url") { if (!reader.string(entry.download_url, kMaxUrl)) return fail(CatalogParseError::FieldTooLong); url = true; }
        else if (key == "cover_url") { if (!reader.string(entry.cover_url, kMaxUrl)) return fail(CatalogParseError::FieldTooLong); }
        else if (key == "screenshots") { if (!reader.strings(entry.screenshots, kMaxScreenshots, kMaxUrl)) return fail(CatalogParseError::FieldTooLong); }
        else if (key == "action") { std::string value; if (!reader.string(value, 16)) return fail(CatalogParseError::MalformedJson); if (value == "install_cia") entry.action = CatalogAction::InstallCia; else if (value == "download") entry.action = CatalogAction::Download; else return fail(CatalogParseError::InvalidAction); action = true; }
        else if (!reader.skip()) return fail(CatalogParseError::MalformedJson);
    }
    if (!reader.done()) return fail(CatalogParseError::MalformedJson);
    if (!(rom && file && title && name && type && action && url) || entry.title.empty()) return fail(CatalogParseError::MissingField);
    if (!isSafeDownloadFilename(entry.file_name)) return fail(CatalogParseError::UnsafeFilename);
    if (!supportedType(entry.type)) return fail(CatalogParseError::UnsafeFilename);
    if (entry.download_url.compare(0, 7, "http://") != 0) return fail(CatalogParseError::UnsupportedUrl);
    if (!entry.cover_url.empty() && entry.cover_url.compare(0, 7, "http://") != 0) return fail(CatalogParseError::UnsupportedUrl);
    if ((entry.type == "cia") != (entry.action == CatalogAction::InstallCia)) return fail(CatalogParseError::InvalidAction);
    if (entries_.size() >= kMaxCatalogEntries) return fail(CatalogParseError::TooManyEntries);
    entries_.push_back(std::move(entry)); line_.clear(); return true;
}

bool CatalogParser::feed(std::string_view chunk) {
    if (error_ != CatalogParseError::None || finished_) return false;
    for (char ch : chunk) {
        if (ch == '\0') return fail(CatalogParseError::EmbeddedNul);
        if (ch == '\n') { if (!parseLine()) return false; ++line_number_; }
        else { if (line_.size() >= kMaxCatalogLineBytes) return fail(CatalogParseError::LineTooLong); line_.push_back(ch); }
    }
    return true;
}

bool CatalogParser::finish() { if (error_ != CatalogParseError::None || finished_) return false; finished_ = true; return line_.empty() || parseLine(); }

const char* CatalogParser::errorMessage() const noexcept {
    switch (error_) {
        case CatalogParseError::None: return "no error"; case CatalogParseError::EmbeddedNul: return "embedded NUL byte";
        case CatalogParseError::LineTooLong: return "catalog line exceeds 12288 bytes"; case CatalogParseError::MalformedJson: return "malformed NDJSON record";
        case CatalogParseError::MissingField: return "catalog record is missing a required field"; case CatalogParseError::FieldTooLong: return "catalog field exceeds its byte limit";
        case CatalogParseError::TooManyEntries: return "catalog exceeds 512 entries"; case CatalogParseError::UnsupportedUrl: return "download URL must begin with http://";
        case CatalogParseError::UnsafeFilename: return "unsafe or unsupported filename"; case CatalogParseError::InvalidAction: return "file type and action do not match";
    }
    return "catalog parse error";
}
} // namespace romm3ds
