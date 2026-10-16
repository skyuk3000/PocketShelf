#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace romm3ds {

inline constexpr std::size_t kMaxSettingsUrlBytes = 512;
inline constexpr std::size_t kMaxSettingsTokenBytes = 512;
inline constexpr std::size_t kMaxSettingsFileBytes = 1280;
inline constexpr std::array<std::string_view, 10> kTypeFilterChoices = {
    "", "cia", "3ds", "3dsx", "nds", "gba", "gb", "gbc", "nes", "sfc",
};

enum class Theme {
    Dark,
    Light,
};

struct Settings {
    std::string base_url;
    std::string token;
    std::string type_filter;
    Theme theme = Theme::Dark;
};

enum class SettingsError {
    None,
    NotFound,
    OpenFailed,
    ReadFailed,
    FileTooLarge,
    MissingUrl,
    EmptyUrl,
    UrlTooLong,
    UnsupportedScheme,
    MissingHost,
    CredentialsNotAllowed,
    InvalidCharacter,
    UnknownKey,
    DuplicateKey,
    MissingField,
    FieldTooLong,
    InvalidTypeFilter,
    InvalidTheme,
    WriteFailed,
    SyncFailed,
    RenameFailed,
};

bool validateBaseUrl(std::string_view url, SettingsError& error);
bool validateToken(std::string_view token, SettingsError& error);
bool validateTypeFilter(std::string_view type_filter, SettingsError& error);
bool validateTheme(Theme theme, SettingsError& error);
const char* themeName(Theme theme) noexcept;
bool parseSettings(std::string_view contents, Settings& output, SettingsError& error);
bool loadSettings(const std::string& path, Settings& output, SettingsError& error);
bool saveSettingsAtomic(const std::string& path, const Settings& settings, SettingsError& error);
const char* settingsErrorMessage(SettingsError error) noexcept;

} // namespace romm3ds