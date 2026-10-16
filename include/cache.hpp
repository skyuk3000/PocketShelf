#pragma once

#include "catalog_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace romm3ds {

inline constexpr std::size_t kMaxCatalogCacheBytes = 2 * 1024 * 1024;
inline constexpr std::size_t kMaxCoverCacheFiles = 32;
inline constexpr std::size_t kMaxCoverCacheBytes = 8 * 1024 * 1024;

bool saveCatalogCache(const std::string& path, const std::vector<CatalogEntry>& entries);
bool loadCatalogCache(const std::string& path, std::vector<CatalogEntry>& entries);
std::string coverCachePath(const std::string& directory, std::uint64_t file_id);
std::string screenshotCachePath(const std::string& directory, std::uint64_t rom_id,
                                std::size_t index, std::string_view validated_url);

} // namespace romm3ds
