#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace romm3ds {

inline constexpr std::size_t kMaxSaveInventoryBytes = 128 * 1024;
inline constexpr std::size_t kMaxRemoteSavesPerRom = 32;
inline constexpr std::size_t kMaxLocalSavesPerRom = 16;
inline constexpr std::uint64_t kMaxSaveUploadBytes = 512 * 1024;
inline constexpr char kSaveUploadSlot[] = "PocketShelf";

struct RemoteSaveFile {
    std::uint64_t id = 0;
    std::uint64_t rom_id = 0;
    std::uint64_t size = 0;
    std::string file_name;
    std::string updated_at;
};

struct LocalSaveFile {
    std::uint64_t size = 0;
    std::int64_t modified = 0;
    std::string file_name;
    std::string path;
};

struct SaveInventory {
    std::vector<RemoteSaveFile> remote;
};

enum class SaveParseError {
    None,
    InventoryTooLarge,
    MalformedJson,
    MissingField,
    FieldTooLong,
    TooManyEntries,
    UnsafeFilename,
    WrongRom,
};

bool parseSaveInventory(std::string_view json, std::uint64_t expected_rom_id,
                        SaveInventory& output, SaveParseError& error);
const char* saveParseErrorMessage(SaveParseError error) noexcept;

std::string rommSaveInventoryUrl(std::string_view base_url, std::uint64_t rom_id);
std::string rommSaveContentUrl(std::string_view base_url, std::uint64_t save_id);
std::string rommSaveUploadUrl(std::string_view base_url, std::uint64_t rom_id);
bool buildSaveUploadBody(std::string_view file_name, const std::uint8_t* data,
                         std::size_t size, std::vector<std::uint8_t>& body,
                         std::string& content_type);
bool buildSaveUploadBodyFromFile(std::string_view file_name, const std::string& path,
                                 std::size_t size, std::vector<std::uint8_t>& body,
                                 std::string& content_type);

bool isSafeSaveFilename(std::string_view value) noexcept;
bool isSafeVersionedUploadFilename(std::string_view value) noexcept;
std::string managedSaveDirectory(std::string_view root, std::uint64_t rom_id);
bool scanLocalSaveDirectory(const std::string& directory,
                            std::vector<LocalSaveFile>& output);
std::string availableBackupPath(const std::string& target, std::string_view timestamp);

} // namespace romm3ds
