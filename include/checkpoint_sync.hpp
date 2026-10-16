#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace romm3ds {

inline constexpr char kCheckpointSavesRoot[] = "sdmc:/3ds/Checkpoint/saves";
inline constexpr std::size_t kMaxCheckpointTitleDirectories = 128;
inline constexpr std::size_t kMaxCheckpointBackupsPerTitle = 64;
inline constexpr std::size_t kMaxCheckpointPackageEntries = 128;
inline constexpr std::size_t kMaxCheckpointRelativePathBytes = 240;
inline constexpr std::uint64_t kMaxCheckpointPackageBytes = 512 * 1024;
inline constexpr char kCheckpointPackageExtension[] = ".pscp";

struct CheckpointBackup {
    std::string name;
    std::string path;
    std::int64_t modified = 0;
};

enum class CheckpointPackageError {
    None,
    OpenFailed,
    InvalidSource,
    UnsafePath,
    TooManyEntries,
    PackageTooLarge,
    MalformedPackage,
    WrongTitle,
    EmptyBackup,
    IoFailed,
    ActivateFailed,
};

std::uint32_t checkpointUniqueId(std::uint64_t title_id) noexcept;
bool parseCheckpointTitleFolder(std::string_view name, std::uint32_t& unique_id) noexcept;
bool findCheckpointTitleDirectory(const std::string& saves_root, std::uint64_t title_id,
                                  std::string& output);
bool scanCheckpointBackups(const std::string& title_directory,
                           std::vector<CheckpointBackup>& output);

bool createCheckpointPackage(const std::string& backup_directory,
                             std::string_view backup_name,
                             std::uint64_t title_id,
                             const std::string& output_path,
                             CheckpointPackageError& error);
bool restoreCheckpointPackage(const std::string& package_path,
                              const std::string& title_directory,
                              std::uint64_t expected_title_id,
                              std::string_view restore_name,
                              std::string& restored_path,
                              CheckpointPackageError& error);
const char* checkpointPackageErrorMessage(CheckpointPackageError error) noexcept;

std::string checkpointPackageFilename(std::uint64_t title_id);
bool isCheckpointPackageFilename(std::string_view file_name,
                                 std::uint64_t title_id) noexcept;
std::string checkpointBindingPath(std::string_view root, std::uint64_t rom_id);
bool saveCheckpointBindingAtomic(const std::string& path, std::uint64_t title_id);
bool loadCheckpointBinding(const std::string& path, std::uint64_t& title_id);

} // namespace romm3ds
