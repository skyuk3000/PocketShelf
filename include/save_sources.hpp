#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace romm3ds {

inline constexpr std::size_t kMaxRetroArchConfigBytes = 64 * 1024;
inline constexpr char kRetroArchConfigPath[] = "sdmc:/retroarch/retroarch.cfg";
inline constexpr char kRetroArchDefaultSaveDirectory[] = "sdmc:/retroarch/cores/savefiles";

struct RetroArchSaveConfig {
    // Empty means the 3DS frontend default above.
    std::string savefile_directory;
    bool savefiles_in_content_dir = false;
    bool sort_savefiles_by_core = false;
    bool sort_savefiles_by_content_directory = false;
};

enum class RetroArchConfigError {
    None,
    ConfigTooLarge,
    MalformedValue,
    UnsafePath,
};

bool parseRetroArchSaveConfig(std::string_view text, RetroArchSaveConfig& output,
                              RetroArchConfigError& error);
const char* retroArchConfigErrorMessage(RetroArchConfigError error) noexcept;

// Resolves RetroArch's ordinary battery-save path (.srm). The core_library_name
// is RetroArch's runtime library_name, not a guessed core filename. It may be
// empty only when per-core sorting is disabled.
bool resolveRetroArchSavePath(const RetroArchSaveConfig& config,
                              std::string_view content_path,
                              std::string_view core_library_name,
                              std::string& output);

// Resolves a conservative emulator battery-save binding beside the exact ROM.
// Only formats with an explicitly supported .sav convention are accepted.
bool resolveRomAdjacentSavePath(std::string_view content_path,
                                std::string_view catalog_type,
                                std::string& output);
bool isRomAdjacentSaveVersion(std::string_view local_file_name,
                              std::string_view remote_file_name) noexcept;

} // namespace romm3ds
