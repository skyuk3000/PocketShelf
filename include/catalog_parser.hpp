#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace romm3ds {

inline constexpr std::size_t kMaxCatalogEntries = 512;
inline constexpr std::size_t kMaxCatalogLineBytes = 12288;
inline constexpr std::size_t kMaxScreenshots = 4;
inline constexpr std::size_t kMaxCatalogTitleBytes = 450;
inline constexpr std::size_t kMaxCatalogFieldBytes = 450;
inline constexpr std::size_t kMaxCatalogSummaryBytes = 768;
inline constexpr std::size_t kMaxCatalogUrlBytes = 512;

enum class CatalogAction { InstallCia, Download };

enum MetadataFlag : std::uint16_t {
    MetadataSummaryOmitted = 1U << 0,
    MetadataRegionsTruncated = 1U << 1,
    MetadataGenresTruncated = 1U << 2,
    MetadataDevelopersTruncated = 1U << 3,
    MetadataPublishersTruncated = 1U << 4,
    MetadataFranchisesTruncated = 1U << 5,
    MetadataCollectionsTruncated = 1U << 6,
    MetadataUserSupplied = 1U << 7,
    MetadataPlatformOmitted = 1U << 8,
};

inline constexpr std::uint16_t kAllMetadataFlags =
    MetadataSummaryOmitted | MetadataRegionsTruncated | MetadataGenresTruncated |
    MetadataDevelopersTruncated | MetadataPublishersTruncated |
    MetadataFranchisesTruncated | MetadataCollectionsTruncated |
    MetadataUserSupplied | MetadataPlatformOmitted;

struct CatalogEntry {
    std::uint64_t rom_id = 0;
    std::uint64_t file_id = 0;
    std::uint64_t size = 0;
    std::string title;
    std::string platform;
    std::string region;
    std::string file_name;
    std::string type;
    std::string summary;
    std::string status;
    std::string genres;
    std::string franchises;
    std::string collections;
    std::string publishers;
    std::string developers;
    std::string download_url;
    std::string cover_url;
    std::vector<std::string> screenshots;
    std::uint8_t rating = 0;
    std::uint8_t difficulty = 0;
    std::uint8_t completion = 0;
    bool favorite = false;
    bool now_playing = false;
    bool backlogged = false;
    bool has_last_played = false;
    std::uint16_t metadata_flags = 0;
    CatalogAction action = CatalogAction::Download;
};

enum class CatalogParseError {
    None, EmbeddedNul, LineTooLong, MalformedJson, MissingField, FieldTooLong,
    TooManyEntries, UnsupportedUrl, UnsafeFilename, InvalidAction,
};

class CatalogParser {
public:
    bool feed(std::string_view chunk);
    bool finish();
    const std::vector<CatalogEntry>& entries() const noexcept { return entries_; }
    CatalogParseError error() const noexcept { return error_; }
    std::size_t errorLine() const noexcept { return error_line_; }
    const char* errorMessage() const noexcept;
private:
    bool parseLine();
    bool fail(CatalogParseError error);
    std::vector<CatalogEntry> entries_;
    std::string line_;
    CatalogParseError error_ = CatalogParseError::None;
    std::size_t line_number_ = 1;
    std::size_t error_line_ = 0;
    bool finished_ = false;
};

bool isSafeDownloadFilename(std::string_view value) noexcept;
std::string downloadDirectoryForType(std::string_view type);

} // namespace romm3ds
