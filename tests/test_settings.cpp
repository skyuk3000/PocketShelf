#include "settings.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

using romm3ds::Settings;
using romm3ds::SettingsError;
using romm3ds::Theme;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string tempPath() {
    return "/tmp/pocketshelf-settings-" + std::to_string(static_cast<long long>(getpid())) + ".ini";
}

void test_parses_direct_romm_settings() {
    Settings settings;
    SettingsError error = SettingsError::None;
    require(romm3ds::parseSettings("# PocketShelf v2\r\nbase_url=http://romm.lan:8080\r\ntoken=secret-value\r\n",
                                   settings, error),
            "valid settings should parse");
    require(settings.base_url == "http://romm.lan:8080", "base URL should match");
    require(settings.token == "secret-value", "token should match");
    require(settings.type_filter.empty(), "older settings should default to all file types");
    require(settings.theme == Theme::Dark, "older settings should default to dark mode");
    require(error == SettingsError::None, "valid settings should have no error");
}

void test_rejects_unsafe_or_malformed_settings() {
    Settings settings;
    SettingsError error = SettingsError::None;
    require(!romm3ds::parseSettings("base_url=https://romm\ntoken=x\n", settings, error),
            "HTTPS is outside the current client contract");
    require(error == SettingsError::UnsupportedScheme, "scheme error expected");
    require(!romm3ds::parseSettings("base_url=http://\ntoken=x\n", settings, error),
            "host is required");
    require(error == SettingsError::MissingHost, "host error expected");
    require(!romm3ds::parseSettings("base_url=http://user:pass@romm\ntoken=x\n", settings, error),
            "credentials must not be persisted");
    require(error == SettingsError::CredentialsNotAllowed, "credential error expected");
    require(!romm3ds::parseSettings("base_url=http://romm/a b\ntoken=x\n", settings, error),
            "whitespace must be rejected");
    require(error == SettingsError::InvalidCharacter, "character error expected");
    require(!romm3ds::parseSettings("unknown=value\n", settings, error),
            "unknown keys should not be silently accepted");
    require(error == SettingsError::UnknownKey, "unknown-key error expected");
    require(!romm3ds::parseSettings("token=x\n", settings, error),
            "base URL is required");
    require(error == SettingsError::MissingUrl, "missing URL error expected");
}

void test_enforces_bounds_and_duplicate_key() {
    Settings settings;
    SettingsError error = SettingsError::None;
    const std::string longest = "http://h/" + std::string(503, 'a');
    require(longest.size() == romm3ds::kMaxSettingsUrlBytes, "test URL should hit limit");
    require(romm3ds::validateBaseUrl(longest, error), "512-byte URL should be valid");
    require(!romm3ds::validateBaseUrl(longest + "a", error), "513-byte URL should fail");
    require(error == SettingsError::UrlTooLong, "length error expected");
    require(!romm3ds::parseSettings("base_url=http://one/a\nbase_url=http://two/b\ntoken=x\n",
                                    settings, error),
            "duplicate keys should fail");
    require(error == SettingsError::DuplicateKey, "duplicate error expected");
}

void test_atomic_save_round_trip_and_replace() {
    const std::string path = tempPath();
    const std::string temporary = path + ".tmp";
    std::remove(path.c_str());
    std::remove(temporary.c_str());

    Settings first{"http://romm.lan", "token-one", "cia", Theme::Light};
    SettingsError error = SettingsError::None;
    require(romm3ds::saveSettingsAtomic(path, first, error), "first save should succeed");
    require(std::ifstream(temporary).good() == false, "temporary file should be removed after save");

    Settings loaded;
    require(romm3ds::loadSettings(path, loaded, error), "saved settings should load");
    require(loaded.base_url == first.base_url && loaded.token == first.token &&
                loaded.type_filter == first.type_filter && loaded.theme == Theme::Light,
            "round trip should preserve URL, token, type filter, and theme");

    Settings replacement{"http://192.168.1.40:8080", "token-two", "3ds", Theme::Dark};
    require(romm3ds::saveSettingsAtomic(path, replacement, error), "replacement save should succeed");
    require(romm3ds::loadSettings(path, loaded, error), "replacement should load");
    require(loaded.base_url == replacement.base_url && loaded.token == replacement.token &&
                loaded.type_filter == replacement.type_filter,
            "atomic replace should update direct settings");
    require(std::ifstream(path + ".bak").good() == false, "backup should be removed after replace");
    std::remove(path.c_str());
}

void test_save_creates_parent_directories_and_preserves_old_file_on_failed_replace() {
    const std::string root = "/tmp/pocketshelf-settings-dir-" +
                             std::to_string(static_cast<long long>(getpid()));
    const std::string path = root + "/nested/settings.ini";
    const std::string cleanup = "rm -rf '" + root + "'";
    std::system(cleanup.c_str());

    SettingsError error = SettingsError::None;
    Settings first{"http://first", "first-token", ""};
    require(romm3ds::saveSettingsAtomic(path, first, error),
            "save should create missing parent directories");

    require(::mkdir((path + ".bak").c_str(), 0700) == 0,
            "test should reserve backup path as a directory");
    {
        std::ofstream blocker(path + ".bak/blocker");
        blocker << "keep";
    }
    Settings second{"http://second", "second-token", ""};
    require(!romm3ds::saveSettingsAtomic(path, second, error),
            "replace should fail safely when backup path cannot be prepared");
    require(error == SettingsError::RenameFailed, "replace failure should be reported");

    Settings loaded;
    require(romm3ds::loadSettings(path, loaded, error), "old settings should remain readable");
    require(loaded.base_url == first.base_url && loaded.token == first.token,
            "failed replacement must preserve the old settings file");
    std::system(cleanup.c_str());
}

void test_missing_file_reports_not_found_without_mutating_output() {
    Settings settings{"http://unchanged", "unchanged-token", ""};
    SettingsError error = SettingsError::None;
    require(!romm3ds::loadSettings(tempPath() + ".missing", settings, error),
            "missing file should report failure");
    require(error == SettingsError::NotFound, "not-found error expected");
    require(settings.base_url == "http://unchanged" && settings.token == "unchanged-token",
            "failed load should not mutate output");
}

void test_migrates_legacy_manifest_url_to_origin() {
    Settings settings;
    SettingsError error = SettingsError::None;
    require(romm3ds::parseSettings("# PocketShelf settings v1\nmanifest_url=http://romm.lan:8080/v1/manifest.txt\n",
                                   settings, error), "legacy settings should migrate");
    require(settings.base_url == "http://romm.lan:8080", "migration keeps only origin");
    require(settings.token.empty(), "migration cannot invent a token");
    require(settings.type_filter.empty(), "migration defaults to all file types");
}

void test_parses_and_validates_type_filter() {
    Settings settings;
    SettingsError error = SettingsError::None;
    require(romm3ds::parseSettings(
                "base_url=http://romm\ntoken=x\ntype_filter=gba\n", settings, error),
            "known type filter should parse");
    require(settings.type_filter == "gba", "type filter should be retained");
    require(!romm3ds::parseSettings(
                "base_url=http://romm\ntoken=x\ntype_filter=pdf\n", settings, error),
            "unsupported type filter should be rejected");
    require(error == SettingsError::InvalidTypeFilter, "type filter error expected");
}

void test_parses_and_validates_theme() {
    Settings settings;
    SettingsError error = SettingsError::None;
    require(romm3ds::parseSettings(
                "base_url=http://romm\ntoken=x\ntheme=light\n", settings, error),
            "light theme should parse");
    require(settings.theme == Theme::Light, "light theme should be retained");
    require(!romm3ds::parseSettings(
                "base_url=http://romm\ntoken=x\ntheme=purple\n", settings, error),
            "unknown theme should be rejected");
    require(error == SettingsError::InvalidTheme, "theme error expected");
}

} // namespace

int main() {
    test_parses_direct_romm_settings();
    test_rejects_unsafe_or_malformed_settings();
    test_enforces_bounds_and_duplicate_key();
    test_atomic_save_round_trip_and_replace();
    test_save_creates_parent_directories_and_preserves_old_file_on_failed_replace();
    test_missing_file_reports_not_found_without_mutating_output();
    test_migrates_legacy_manifest_url_to_origin();
    test_parses_and_validates_type_filter();
    test_parses_and_validates_theme();
    std::cout << "All settings tests passed\n";
    return 0;
}