#include "save_sources.hpp"

#include <algorithm>
#include <cctype>

namespace romm3ds {
namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return value;
}

bool parseString(std::string_view value, std::string& output) {
    value = trim(value);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return false;
    output.clear();
    for (std::size_t i = 1; i + 1 < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '\\') {
            if (++i + 1 >= value.size()) return false;
            const char escaped = value[i];
            if (escaped != '\\' && escaped != '"') return false;
            output.push_back(escaped);
        } else if (static_cast<unsigned char>(ch) < 0x20 || ch == '"') {
            return false;
        } else {
            output.push_back(ch);
        }
    }
    return true;
}

bool parseBool(std::string_view value, bool& output) {
    std::string parsed;
    value = trim(value);
    if (!value.empty() && value.front() == '"') {
        if (!parseString(value, parsed)) return false;
        value = parsed;
    }
    if (value == "true") { output = true; return true; }
    if (value == "false") { output = false; return true; }
    return false;
}

bool safeSdPath(std::string_view path) {
    if (path.size() < 7 || path.substr(0, 6) != "sdmc:/" || path.back() == '/') return false;
    std::size_t start = 6;
    while (start < path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::string_view segment = path.substr(
            start, slash == std::string_view::npos ? path.size() - start : slash - start);
        if (segment.empty() || segment == "." || segment == "..") return false;
        for (const unsigned char ch : segment) {
            if (ch < 0x20 || ch == 0x7f || ch == '\\' || ch == ':') return false;
        }
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return true;
}

bool safeSegment(std::string_view value) {
    if (value.empty() || value == "." || value == "..") return false;
    for (const unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7f || ch == '/' || ch == '\\' || ch == ':') return false;
    }
    return true;
}

std::string parentDirectory(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string() : std::string(path.substr(0, slash));
}

std::string baseName(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return std::string(path.substr(slash == std::string_view::npos ? 0 : slash + 1));
}

void appendPath(std::string& path, std::string_view segment) {
    if (!path.empty() && path.back() != '/') path.push_back('/');
    path.append(segment.data(), segment.size());
}

} // namespace

bool parseRetroArchSaveConfig(std::string_view text, RetroArchSaveConfig& output,
                              RetroArchConfigError& error) {
    if (text.size() > kMaxRetroArchConfigBytes) {
        error = RetroArchConfigError::ConfigTooLarge;
        return false;
    }
    RetroArchSaveConfig parsed;
    std::size_t position = 0;
    while (position <= text.size()) {
        const std::size_t newline = text.find('\n', position);
        std::string_view line = text.substr(position,
            newline == std::string_view::npos ? text.size() - position : newline - position);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        line = trim(line);
        if (!line.empty() && line.front() != '#') {
            const std::size_t equals = line.find('=');
            if (equals != std::string_view::npos) {
                const std::string_view key = trim(line.substr(0, equals));
                const std::string_view value = trim(line.substr(equals + 1));
                if (key == "savefile_directory") {
                    std::string directory;
                    if (!parseString(value, directory)) {
                        error = RetroArchConfigError::MalformedValue;
                        return false;
                    }
                    if (directory == "default") directory.clear();
                    if (!directory.empty() && !safeSdPath(directory)) {
                        error = RetroArchConfigError::UnsafePath;
                        return false;
                    }
                    parsed.savefile_directory = std::move(directory);
                } else if (key == "savefiles_in_content_dir") {
                    if (!parseBool(value, parsed.savefiles_in_content_dir)) {
                        error = RetroArchConfigError::MalformedValue;
                        return false;
                    }
                } else if (key == "sort_savefiles_enable") {
                    if (!parseBool(value, parsed.sort_savefiles_by_core)) {
                        error = RetroArchConfigError::MalformedValue;
                        return false;
                    }
                } else if (key == "sort_savefiles_by_content_enable") {
                    if (!parseBool(value, parsed.sort_savefiles_by_content_directory)) {
                        error = RetroArchConfigError::MalformedValue;
                        return false;
                    }
                }
            }
        }
        if (newline == std::string_view::npos) break;
        position = newline + 1;
    }
    output = std::move(parsed);
    error = RetroArchConfigError::None;
    return true;
}

const char* retroArchConfigErrorMessage(RetroArchConfigError error) noexcept {
    switch (error) {
        case RetroArchConfigError::None: return "no error";
        case RetroArchConfigError::ConfigTooLarge: return "RetroArch config exceeds 64 KiB";
        case RetroArchConfigError::MalformedValue: return "RetroArch save setting is malformed";
        case RetroArchConfigError::UnsafePath: return "RetroArch save path is outside safe SD storage";
    }
    return "RetroArch config error";
}

bool resolveRetroArchSavePath(const RetroArchSaveConfig& config,
                              std::string_view content_path,
                              std::string_view core_library_name,
                              std::string& output) {
    output.clear();
    if (!safeSdPath(content_path)) return false;
    if (config.sort_savefiles_by_core && !safeSegment(core_library_name)) return false;

    std::string content_name = baseName(content_path);
    const std::size_t extension = content_name.rfind('.');
    if (extension != std::string::npos) content_name.resize(extension);
    if (!safeSegment(content_name)) return false;

    const std::string content_directory = parentDirectory(content_path);
    std::string path = config.savefiles_in_content_dir ? content_directory :
        (config.savefile_directory.empty() ? kRetroArchDefaultSaveDirectory :
                                             config.savefile_directory);
    if (!safeSdPath(path)) return false;

    if (config.sort_savefiles_by_content_directory) {
        const std::string directory_name = baseName(content_directory);
        if (!safeSegment(directory_name)) return false;
        appendPath(path, directory_name);
    }
    if (config.sort_savefiles_by_core) appendPath(path, core_library_name);
    appendPath(path, content_name + ".srm");
    output = std::move(path);
    return true;
}

bool resolveRomAdjacentSavePath(std::string_view content_path,
                                std::string_view catalog_type,
                                std::string& output) {
    output.clear();
    if (!safeSdPath(content_path) || catalog_type.empty()) return false;
    std::string type(catalog_type);
    std::transform(type.begin(), type.end(), type.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (type != "gba" && type != "gb" && type != "gbc" && type != "nds") return false;

    const std::size_t slash = content_path.rfind('/');
    const std::size_t dot = content_path.rfind('.');
    if (dot == std::string_view::npos || dot <= slash + 1 || dot + 1 >= content_path.size())
        return false;
    std::string extension(content_path.substr(dot + 1));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (extension != type) return false;
    output.assign(content_path.substr(0, dot));
    output += ".sav";
    return safeSdPath(output);
}

bool isRomAdjacentSaveVersion(std::string_view local_file_name,
                              std::string_view remote_file_name) noexcept {
    if (local_file_name.size() < 5 ||
        local_file_name.substr(local_file_name.size() - 4) != ".sav") return false;
    if (remote_file_name == local_file_name) return true;
    const std::string_view stem = local_file_name.substr(0, local_file_name.size() - 4);
    if (remote_file_name.size() != stem.size() + 26 ||
        remote_file_name.substr(0, stem.size()) != stem ||
        remote_file_name.substr(remote_file_name.size() - 4) != ".sav") return false;
    const std::string_view tag = remote_file_name.substr(stem.size(), 22);
    if (tag.front() != ' ' || tag[1] != '[' || tag[21] != ']') return false;
    constexpr char separators[] = "----_--:--:--";
    (void)separators;
    for (std::size_t index = 2; index < 21; ++index) {
        const char ch = tag[index];
        const bool separator = index == 6 || index == 9 || index == 12 ||
                               index == 15 || index == 18;
        const char expected = index == 12 ? '_' : '-';
        if (separator ? ch != expected : (ch < '0' || ch > '9')) return false;
    }
    return true;
}

} // namespace romm3ds
