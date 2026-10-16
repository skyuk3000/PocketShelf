#include "settings.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace romm3ds {
namespace {

constexpr std::string_view kBaseKey = "base_url=";
constexpr std::string_view kTokenKey = "token=";
constexpr std::string_view kTypeFilterKey = "type_filter=";
constexpr std::string_view kThemeKey = "theme=";
constexpr std::string_view kLegacyKey = "manifest_url=";

bool fail(SettingsError value, SettingsError& error) {
    error = value;
    return false;
}

bool ensureParentDirectories(const std::string& path) {
    const std::size_t final_slash = path.find_last_of('/');
    if (final_slash == std::string::npos) return true;
    std::size_t start = path.compare(0, 6, "sdmc:/") == 0 ? 6 : (path.front() == '/' ? 1 : 0);
    for (std::size_t slash = path.find('/', start); slash <= final_slash;
         slash = path.find('/', slash + 1)) {
        const std::string directory = path.substr(0, slash);
        if (!directory.empty() && ::mkdir(directory.c_str(), 0777) != 0 && errno != EEXIST) return false;
        if (slash == final_slash) break;
    }
    return true;
}

bool pathExists(const std::string& path) {
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0;
}

} // namespace

bool validateBaseUrl(std::string_view url, SettingsError& error) {
    if (url.empty()) return fail(SettingsError::EmptyUrl, error);
    if (url.size() > kMaxSettingsUrlBytes) return fail(SettingsError::UrlTooLong, error);
    if (url.compare(0, 7, "http://") != 0) return fail(SettingsError::UnsupportedScheme, error);
    const std::size_t authority_end = url.find_first_of("/?#", 7);
    const std::string_view authority = url.substr(
        7, authority_end == std::string_view::npos ? url.size() - 7 : authority_end - 7);
    if (authority.empty()) return fail(SettingsError::MissingHost, error);
    if (authority.find('@') != std::string_view::npos) return fail(SettingsError::CredentialsNotAllowed, error);
    for (const unsigned char value : url) {
        if (value <= 0x20 || value == 0x7f) return fail(SettingsError::InvalidCharacter, error);
    }
    error = SettingsError::None;
    return true;
}

bool validateToken(std::string_view token, SettingsError& error) {
    if (token.size() > kMaxSettingsTokenBytes) return fail(SettingsError::FieldTooLong, error);
    for (const unsigned char value : token) {
        if (value <= 0x20 || value == 0x7f) return fail(SettingsError::InvalidCharacter, error);
    }
    error = SettingsError::None;
    return true;
}

bool validateTypeFilter(std::string_view type_filter, SettingsError& error) {
    if (std::find(kTypeFilterChoices.begin(), kTypeFilterChoices.end(), type_filter) ==
        kTypeFilterChoices.end()) {
        return fail(SettingsError::InvalidTypeFilter, error);
    }
    error = SettingsError::None;
    return true;
}

bool validateTheme(Theme theme, SettingsError& error) {
    if (theme != Theme::Dark && theme != Theme::Light)
        return fail(SettingsError::InvalidTheme, error);
    error = SettingsError::None;
    return true;
}

const char* themeName(Theme theme) noexcept {
    return theme == Theme::Light ? "light" : "dark";
}

bool parseSettings(std::string_view contents, Settings& output, SettingsError& error) {
    if (contents.size() > kMaxSettingsFileBytes) return fail(SettingsError::FileTooLarge, error);
    Settings parsed;
    bool found_url = false;
    bool found_token = false;
    bool found_type_filter = false;
    bool found_theme = false;
    bool legacy = false;
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const std::size_t newline = contents.find('\n', offset);
        const std::size_t end = newline == std::string_view::npos ? contents.size() : newline;
        std::string_view line = contents.substr(offset, end - offset);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        offset = newline == std::string_view::npos ? contents.size() : newline + 1;
        if (line.empty() || line.front() == '#') continue;

        if (line.compare(0, kBaseKey.size(), kBaseKey) == 0) {
            if (found_url) return fail(SettingsError::DuplicateKey, error);
            const std::string_view url = line.substr(kBaseKey.size());
            if (!validateBaseUrl(url, error)) return false;
            parsed.base_url.assign(url.data(), url.size());
            found_url = true;
        } else if (line.compare(0, kTokenKey.size(), kTokenKey) == 0) {
            if (found_token) return fail(SettingsError::DuplicateKey, error);
            const std::string_view token = line.substr(kTokenKey.size());
            if (!validateToken(token, error)) return false;
            parsed.token.assign(token.data(), token.size());
            found_token = true;
        } else if (line.compare(0, kTypeFilterKey.size(), kTypeFilterKey) == 0) {
            if (found_type_filter) return fail(SettingsError::DuplicateKey, error);
            const std::string_view type_filter = line.substr(kTypeFilterKey.size());
            if (!validateTypeFilter(type_filter, error)) return false;
            parsed.type_filter.assign(type_filter.data(), type_filter.size());
            found_type_filter = true;
        } else if (line.compare(0, kThemeKey.size(), kThemeKey) == 0) {
            if (found_theme) return fail(SettingsError::DuplicateKey, error);
            const std::string_view theme = line.substr(kThemeKey.size());
            if (theme == "dark") parsed.theme = Theme::Dark;
            else if (theme == "light") parsed.theme = Theme::Light;
            else return fail(SettingsError::InvalidTheme, error);
            found_theme = true;
        } else if (line.compare(0, kLegacyKey.size(), kLegacyKey) == 0) {
            if (found_url) return fail(SettingsError::DuplicateKey, error);
            const std::string_view url = line.substr(kLegacyKey.size());
            if (!validateBaseUrl(url, error)) return false;
            const std::size_t authority_end = url.find_first_of("/?#", 7);
            parsed.base_url.assign(url.data(), authority_end == std::string_view::npos ? url.size() : authority_end);
            found_url = true;
            legacy = true;
        } else {
            return fail(SettingsError::UnknownKey, error);
        }
    }
    if (!found_url) return fail(SettingsError::MissingUrl, error);
    if (!legacy && !found_token) return fail(SettingsError::MissingField, error);
    output = std::move(parsed);
    error = SettingsError::None;
    return true;
}

bool loadSettings(const std::string& path, Settings& output, SettingsError& error) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return fail(errno == ENOENT ? SettingsError::NotFound : SettingsError::OpenFailed, error);
    char buffer[kMaxSettingsFileBytes + 1];
    const std::size_t length = std::fread(buffer, 1, sizeof(buffer), file);
    const bool read_error = std::ferror(file) != 0;
    std::fclose(file);
    if (read_error) return fail(SettingsError::ReadFailed, error);
    if (length > kMaxSettingsFileBytes) return fail(SettingsError::FileTooLarge, error);
    Settings parsed;
    if (!parseSettings(std::string_view(buffer, length), parsed, error)) return false;
    output = std::move(parsed);
    return true;
}

bool saveSettingsAtomic(const std::string& path, const Settings& settings, SettingsError& error) {
    if (!validateBaseUrl(settings.base_url, error) || !validateToken(settings.token, error) ||
        !validateTypeFilter(settings.type_filter, error) || !validateTheme(settings.theme, error)) return false;
    if (!ensureParentDirectories(path)) return fail(SettingsError::OpenFailed, error);
    const std::string temporary_path = path + ".tmp";
    const std::string backup_path = path + ".bak";
    std::remove(temporary_path.c_str());
    FILE* file = std::fopen(temporary_path.c_str(), "wb");
    if (file == nullptr) return fail(SettingsError::OpenFailed, error);

    const std::string contents = "# PocketShelf settings v4 - token is stored on SD\nbase_url=" +
                                 settings.base_url + "\ntoken=" + settings.token +
                                 "\ntype_filter=" + settings.type_filter +
                                 "\ntheme=" + themeName(settings.theme) + "\n";
    const bool wrote = std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    bool synced = false;
    if (wrote && std::fflush(file) == 0) synced = fsync(fileno(file)) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!wrote || !closed) {
        std::remove(temporary_path.c_str());
        return fail(SettingsError::WriteFailed, error);
    }
    if (!synced) {
        std::remove(temporary_path.c_str());
        return fail(SettingsError::SyncFailed, error);
    }
    if (!pathExists(path)) {
        if (std::rename(temporary_path.c_str(), path.c_str()) == 0) {
            error = SettingsError::None;
            return true;
        }
        std::remove(temporary_path.c_str());
        return fail(SettingsError::RenameFailed, error);
    }
    errno = 0;
    if (std::remove(backup_path.c_str()) != 0 && errno != ENOENT) {
        std::remove(temporary_path.c_str());
        return fail(SettingsError::RenameFailed, error);
    }
    if (std::rename(path.c_str(), backup_path.c_str()) != 0) {
        std::remove(temporary_path.c_str());
        return fail(SettingsError::RenameFailed, error);
    }
    if (std::rename(temporary_path.c_str(), path.c_str()) != 0) {
        const int replace_errno = errno;
        std::rename(backup_path.c_str(), path.c_str());
        std::remove(temporary_path.c_str());
        errno = replace_errno;
        return fail(SettingsError::RenameFailed, error);
    }
    std::remove(backup_path.c_str());
    error = SettingsError::None;
    return true;
}

const char* settingsErrorMessage(SettingsError error) noexcept {
    switch (error) {
        case SettingsError::None: return "no error";
        case SettingsError::NotFound: return "settings file not found";
        case SettingsError::OpenFailed: return "could not open settings file";
        case SettingsError::ReadFailed: return "could not read settings file";
        case SettingsError::FileTooLarge: return "settings file is too large";
        case SettingsError::MissingUrl: return "RomM base URL is missing";
        case SettingsError::EmptyUrl: return "RomM base URL cannot be empty";
        case SettingsError::UrlTooLong: return "RomM base URL is too long";
        case SettingsError::UnsupportedScheme: return "URL must start with http://";
        case SettingsError::MissingHost: return "URL must include a host";
        case SettingsError::CredentialsNotAllowed: return "URL credentials are not allowed";
        case SettingsError::InvalidCharacter: return "setting contains spaces or control characters";
        case SettingsError::UnknownKey: return "settings file contains an unknown key";
        case SettingsError::DuplicateKey: return "settings key appears more than once";
        case SettingsError::MissingField: return "settings token key is missing";
        case SettingsError::FieldTooLong: return "token is too long";
        case SettingsError::InvalidTypeFilter: return "unsupported file-type filter";
        case SettingsError::InvalidTheme: return "unsupported theme";
        case SettingsError::WriteFailed: return "could not write settings file";
        case SettingsError::SyncFailed: return "could not sync settings to SD";
        case SettingsError::RenameFailed: return "could not replace settings file";
    }
    return "settings error";
}

} // namespace romm3ds
