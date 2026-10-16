#include "save_data.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void test_parses_bounded_remote_inventory() {
    const std::string json = R"JSON([
      {"id":91,"rom_id":42,"file_name":"Pok\u00e9mon X.sav","file_size_bytes":32768,
       "updated_at":"2026-10-12T08:30:00Z","missing_from_fs":false,"emulator":null},
      {"id":90,"rom_id":42,"file_name":"older.sav","file_size_bytes":16384,
       "updated_at":"2026-10-11T08:30:00Z","missing_from_fs":true}
    ])JSON";
    romm3ds::SaveInventory inventory;
    romm3ds::SaveParseError error = romm3ds::SaveParseError::None;
    require(romm3ds::parseSaveInventory(json, 42, inventory, error), "save inventory parses");
    require(inventory.remote.size() == 1, "missing remote files are omitted");
    require(inventory.remote[0].id == 91 && inventory.remote[0].size == 32768,
            "save metadata is retained");
    require(inventory.remote[0].file_name == "Pok\xC3\xA9mon X.sav", "unicode save filename retained");
}

void test_retains_and_orders_every_remote_version() {
    const std::string json = R"JSON([
      {"id":1,"rom_id":42,"file_name":"PocketShelf-Checkpoint-0004000000055D00 [2026-10-11_08-00-00].pscp","file_size_bytes":10,"updated_at":"2026-10-11T08:00:00Z","missing_from_fs":false},
      {"id":3,"rom_id":42,"file_name":"PocketShelf-Checkpoint-0004000000055D00 [2026-10-13_08-00-00].pscp","file_size_bytes":30,"updated_at":"2026-10-13T08:00:00Z","missing_from_fs":false},
      {"id":2,"rom_id":42,"file_name":"PocketShelf-Checkpoint-0004000000055D00 [2026-10-12_08-00-00].pscp","file_size_bytes":20,"updated_at":"2026-10-12T08:00:00Z","missing_from_fs":false}
    ])JSON";
    romm3ds::SaveInventory inventory;
    romm3ds::SaveParseError error = romm3ds::SaveParseError::None;
    require(romm3ds::parseSaveInventory(json, 42, inventory, error),
            "all remote Checkpoint versions parse");
    require(inventory.remote.size() == 3,
            "inventory retains every bounded remote version rather than only the latest");
    require(inventory.remote[0].id == 3 && inventory.remote[1].id == 2 &&
                inventory.remote[2].id == 1,
            "remote versions are ordered newest first for selection");
}

void test_rejects_wrong_rom_unsafe_and_oversize_inventory() {
    romm3ds::SaveInventory inventory;
    romm3ds::SaveParseError error = romm3ds::SaveParseError::None;
    require(!romm3ds::parseSaveInventory(
        "[{\"id\":1,\"rom_id\":99,\"file_name\":\"ok.sav\",\"file_size_bytes\":1,"
        "\"updated_at\":\"2026-10-12T00:00:00Z\",\"missing_from_fs\":false}]",
        42, inventory, error), "wrong ROM id is rejected");
    require(error == romm3ds::SaveParseError::WrongRom, "wrong ROM error category");
    require(!romm3ds::parseSaveInventory(
        "[{\"id\":1,\"rom_id\":42,\"file_name\":\"../bad.sav\",\"file_size_bytes\":1,"
        "\"updated_at\":\"2026-10-12T00:00:00Z\",\"missing_from_fs\":false}]",
        42, inventory, error), "unsafe save filename is rejected");
    require(error == romm3ds::SaveParseError::UnsafeFilename, "unsafe filename category");
    require(!romm3ds::parseSaveInventory(
        std::string(romm3ds::kMaxSaveInventoryBytes + 1, 'x'), 42, inventory, error),
        "oversize save inventory is rejected");
    require(error == romm3ds::SaveParseError::InventoryTooLarge, "oversize category");
}

void test_builds_real_romm_54_save_urls() {
    require(romm3ds::rommSaveInventoryUrl("http://host:890/", 42) ==
                "http://host:890/api/saves?rom_id=42",
            "inventory URL matches RomM 5.4");
    require(romm3ds::rommSaveContentUrl("http://host:890", 91) ==
                "http://host:890/api/saves/91/content",
            "content URL matches RomM 5.4");
    require(romm3ds::rommSaveUploadUrl("http://host:890", 42) ==
                "http://host:890/api/saves?rom_id=42&slot=PocketShelf",
            "upload URL requests server-side versioning");
}

void test_builds_real_multipart_save_upload_body() {
    const std::uint8_t bytes[] = {0x00, 0x7f, 0xff};
    std::vector<std::uint8_t> body;
    std::string content_type;
    require(romm3ds::buildSaveUploadBody("game.sav", bytes, sizeof(bytes), body, content_type),
            "multipart save body builds");
    require(content_type == "multipart/form-data; boundary=PocketShelf3DSBoundary7F4A",
            "multipart content type includes stable boundary");
    const std::string text(reinterpret_cast<const char*>(body.data()), body.size());
    require(text.find("name=\"saveFile\"; filename=\"game.sav\"") != std::string::npos,
            "RomM saveFile field and filename are present");
    const auto payload = text.find("\r\n\r\n");
    require(payload != std::string::npos && payload + 7 <= body.size(),
            "multipart payload separator is present");
    require(body[payload + 4] == 0x00 && body[payload + 5] == 0x7f &&
                body[payload + 6] == 0xff,
            "binary save bytes are preserved");
}

void test_rejects_unsafe_or_oversize_save_uploads() {
    const std::uint8_t byte = 0x5a;
    std::vector<std::uint8_t> body{1, 2, 3};
    std::string content_type = "stale";
    require(!romm3ds::buildSaveUploadBody("../game.sav", &byte, 1, body, content_type),
            "unsafe multipart filename is rejected");
    require(body.empty() && content_type.empty(), "failed upload build clears outputs");

    require(!romm3ds::buildSaveUploadBody("game.sav", nullptr, 1, body, content_type),
            "non-empty upload requires data");
    require(!romm3ds::buildSaveUploadBody("game.sav", &byte,
                                          romm3ds::kMaxSaveUploadBytes + 1,
                                          body, content_type),
            "oversize upload is rejected before reading data");
    require(!romm3ds::buildSaveUploadBody(std::string(230, 'a') + ".sav", &byte, 1,
                                          body, content_type),
            "filename too long for RomM slot versioning is rejected");
}

void test_accepts_empty_and_maximum_size_save_uploads() {
    std::vector<std::uint8_t> body;
    std::string content_type;
    require(romm3ds::buildSaveUploadBody("empty.sav", nullptr, 0, body, content_type),
            "empty save upload is supported");

    std::vector<std::uint8_t> maximum(
        static_cast<std::size_t>(romm3ds::kMaxSaveUploadBytes), 0xa5);
    require(romm3ds::buildSaveUploadBody("maximum.sav", maximum.data(), maximum.size(),
                                         body, content_type),
            "maximum bounded save upload is supported");
    const std::string closing = "\r\n--PocketShelf3DSBoundary7F4A--\r\n";
    require(body.size() > maximum.size() &&
                std::equal(closing.rbegin(), closing.rend(), body.rbegin()),
            "maximum upload has a complete closing boundary");
}

void test_builds_upload_body_directly_from_file() {
    char root_template[] = "/tmp/pocketshelf-upload-body-XXXXXX";
    char* root = mkdtemp(root_template);
    require(root != nullptr, "upload temporary root created");
    const std::string path = std::string(root) + "/save.bin";
    const std::uint8_t expected[] = {0x00, 0x7f, 0xff, 0x42};
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(expected), sizeof(expected));
    }

    std::vector<std::uint8_t> body;
    std::string content_type;
    require(romm3ds::buildSaveUploadBodyFromFile(
                "game.sav", path, sizeof(expected), body, content_type),
            "multipart upload body reads directly from its source file");
    const std::string text(reinterpret_cast<const char*>(body.data()), body.size());
    const auto payload = text.find("\r\n\r\n");
    require(payload != std::string::npos && payload + 4 + sizeof(expected) <= body.size(),
            "direct file upload payload separator exists");
    require(std::equal(expected, expected + sizeof(expected), body.begin() + payload + 4),
            "direct file upload preserves payload bytes");

    require(!romm3ds::buildSaveUploadBodyFromFile(
                "game.sav", path, sizeof(expected) - 1, body, content_type),
            "source files larger than the inspected size are rejected");
    require(body.empty() && content_type.empty(), "failed direct file build clears outputs");
    std::remove(path.c_str());
    ::rmdir(root);
}

void test_managed_paths_and_local_inventory_are_safe() {
    char root_template[] = "/tmp/pocketshelf-save-test-XXXXXX";
    char* root = mkdtemp(root_template);
    require(root != nullptr, "temporary root created");
    const std::string directory = romm3ds::managedSaveDirectory(root, 42);
    require(directory == std::string(root) + "/42", "managed directory uses numeric RomM id");
    require(::mkdir(directory.c_str(), 0700) == 0, "game save directory created");
    std::ofstream(directory + "/z-last.sav", std::ios::binary) << "BBBB";
    std::ofstream(directory + "/a-first.srm", std::ios::binary) << "AAA";
    std::ofstream(directory + "/.download.tmp", std::ios::binary) << "partial";
    std::ofstream(directory + "/a-first.srm.bak-20261012-083000", std::ios::binary) << "backup";
    ::mkdir((directory + "/nested").c_str(), 0700);

    std::vector<romm3ds::LocalSaveFile> files;
    require(romm3ds::scanLocalSaveDirectory(directory, files), "local inventory scans");
    require(files.size() == 2, "temporary files and directories are omitted");
    require(files[0].file_name == "a-first.srm" && files[0].size == 3,
            "local saves are sorted and sized");
    require(files[1].file_name == "z-last.sav" && files[1].size == 4,
            "second local save retained");

    require(romm3ds::isSafeSaveFilename("game.sav"), "plain save filename accepted");
    require(!romm3ds::isSafeSaveFilename("../game.sav"), "traversal rejected");
    require(!romm3ds::isSafeSaveFilename("dir/game.sav"), "nested paths rejected");
    require(!romm3ds::isSafeSaveFilename("bad\"name.sav"), "multipart header quotes rejected");
    require(!romm3ds::isSafeSaveFilename(".download.tmp"), "internal temp name rejected");
    require(romm3ds::isSafeVersionedUploadFilename(std::string(229, 'a') + ".sav"),
            "filename leaves room for RomM's version tag");
    require(!romm3ds::isSafeVersionedUploadFilename(std::string(230, 'a') + ".sav"),
            "filename that RomM cannot version is rejected");

    std::remove((directory + "/z-last.sav").c_str());
    std::remove((directory + "/a-first.srm").c_str());
    std::remove((directory + "/.download.tmp").c_str());
    std::remove((directory + "/a-first.srm.bak-20261012-083000").c_str());
    ::rmdir((directory + "/nested").c_str());
    ::rmdir(directory.c_str());
    ::rmdir(root);
}

void test_backup_names_do_not_overwrite_existing_backups() {
    char root_template[] = "/tmp/pocketshelf-backup-test-XXXXXX";
    char* root = mkdtemp(root_template);
    require(root != nullptr, "backup temporary root created");
    const std::string target = std::string(root) + "/game.sav";
    std::ofstream(target + ".bak-20261012-083000", std::ios::binary) << "old";
    require(romm3ds::availableBackupPath(target, "20261012-083000") ==
                target + ".bak-20261012-083000-1",
            "existing backup is never overwritten");
    std::remove((target + ".bak-20261012-083000").c_str());
    ::rmdir(root);
}
}

int main() {
    test_parses_bounded_remote_inventory();
    test_retains_and_orders_every_remote_version();
    test_rejects_wrong_rom_unsafe_and_oversize_inventory();
    test_builds_real_romm_54_save_urls();
    test_builds_real_multipart_save_upload_body();
    test_rejects_unsafe_or_oversize_save_uploads();
    test_accepts_empty_and_maximum_size_save_uploads();
    test_builds_upload_body_directly_from_file();
    test_managed_paths_and_local_inventory_are_safe();
    test_backup_names_do_not_overwrite_existing_backups();
    std::cout << "All save data tests passed\n";
    return 0;
}
