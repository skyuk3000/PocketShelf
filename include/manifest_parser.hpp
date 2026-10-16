#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace romm3ds {

constexpr std::size_t kMaxManifestEntries = 512;
constexpr std::size_t kMaxTitleBytes = 96;
constexpr std::size_t kMaxUrlBytes = 512;
constexpr std::size_t kMaxLineBytes = 640;

struct ManifestEntry {
    std::string title;
    std::string url;
};

enum class ParseError {
    None,
    EmbeddedNul,
    LineTooLong,
    MalformedLine,
    EmptyTitle,
    TitleTooLong,
    EmptyUrl,
    UrlTooLong,
    UnsupportedScheme,
    TooManyEntries,
};

class ManifestParser {
public:
    bool feed(std::string_view chunk);
    bool finish();

    const std::vector<ManifestEntry>& entries() const noexcept { return entries_; }
    ParseError error() const noexcept { return error_; }
    std::size_t errorLine() const noexcept { return error_line_; }
    const char* errorMessage() const noexcept;

private:
    bool parseLine();
    bool fail(ParseError error);

    std::vector<ManifestEntry> entries_;
    std::string line_;
    ParseError error_ = ParseError::None;
    std::size_t line_number_ = 1;
    std::size_t error_line_ = 0;
    bool finished_ = false;
};

} // namespace romm3ds
