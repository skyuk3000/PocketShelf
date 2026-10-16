#pragma once

#include "catalog_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace romm3ds {

inline constexpr std::size_t kMaxRommPageBytes = 768 * 1024;
inline constexpr std::size_t kMaxRommPageEntries = 64;
inline constexpr std::size_t kRommPageSize = 12;

enum class RommParseError {
    None,
    PageTooLarge,
    MalformedJson,
    MissingField,
    FieldTooLong,
    TooManyEntries,
    UnsafeFilename,
};

struct RommPage {
    std::vector<CatalogEntry> entries;
    std::size_t item_count = 0;
    std::uint64_t total = 0;
    std::uint64_t limit = 0;
    std::uint64_t offset = 0;
};

struct RommFilters {
    std::string genre;
    std::string developer;
    std::string publisher;
    std::string franchise;
    std::string collection;
};

enum class RommUserAction {
    NowPlaying,
    Backlogged,
    StatusClear,
    StatusIncomplete,
    StatusFinished,
    StatusCompleted100,
    StatusRetired,
    StatusNeverPlaying,
    Rating,
    Difficulty,
    Completion,
};

bool parseRommPage(std::string_view json, RommPage& output, RommParseError& error);
const char* rommParseErrorMessage(RommParseError error) noexcept;

bool parseRommFilters(std::string_view text, RommFilters& output);
std::string rommCatalogueUrl(std::string_view base_url, std::size_t limit,
                             std::size_t offset, std::string_view search,
                             bool include_total, const RommFilters& filters = {});
std::string rommFileContentUrl(std::string_view base_url, std::uint64_t file_id,
                               std::string_view file_name);
std::string rommCoverUrl(std::string_view base_url, std::string_view cover_path);
std::string rommMediaUrl(std::string_view base_url, std::uint64_t rom_id,
                         std::string_view media_path);
std::vector<std::string> rommScreenshotUrls(
    std::string_view base_url, std::uint64_t rom_id,
    const std::vector<std::string>& media_paths);
std::string rommUserPropsUrl(std::string_view base_url, std::uint64_t rom_id);
std::string rommLastPlayedUrl(std::string_view base_url, std::uint64_t rom_id,
                              bool played);
std::string rommUserPropsBody(RommUserAction action, unsigned value);

} // namespace romm3ds
