#include "checkpoint_sync.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void makeDir(const std::string& path) {
    require(::mkdir(path.c_str(), 0700) == 0, "directory created");
}

void test_parses_checkpoint_title_folders_from_authoritative_layout() {
    std::uint32_t unique = 0;
    require(romm3ds::parseCheckpointTitleFolder("0x0055D Pokemon X", unique),
            "Checkpoint title folder parses");
    require(unique == 0x55D, "Checkpoint prefix is title low id shifted by eight");
    require(romm3ds::checkpointUniqueId(0x0004000000055D00ULL) == 0x55D,
            "title id maps to Checkpoint unique id");
    require(!romm3ds::parseCheckpointTitleFolder("0055D Pokemon X", unique),
            "missing Checkpoint marker rejected");
    require(!romm3ds::parseCheckpointTitleFolder("0x0055D", unique),
            "missing title description rejected");
    require(!romm3ds::parseCheckpointTitleFolder("0x0055D ../bad", unique),
            "unsafe title folder rejected");
    require(romm3ds::checkpointPackageFilename(0x0004000000055D00ULL) ==
                "PocketShelf-Checkpoint-0004000000055D00.pscp",
            "Checkpoint package filename embeds exact title ID");
    require(romm3ds::isCheckpointPackageFilename(
                "PocketShelf-Checkpoint-0004000000055D00 [2026-10-13_10-00-00].pscp",
                0x0004000000055D00ULL),
            "RomM slot-versioned Checkpoint package is recognized");
    require(!romm3ds::isCheckpointPackageFilename(
                "PocketShelf-Checkpoint-0004000000123400 [2026-10-13_10-00-00].pscp",
                0x0004000000055D00ULL),
            "another title's package is rejected");
}

void test_discovers_only_matching_checkpoint_title_and_backup_directories() {
    char root_template[] = "/tmp/pocketshelf-checkpoint-scan-XXXXXX";
    char* root = mkdtemp(root_template);
    require(root != nullptr, "temporary root created");
    const std::string matching = std::string(root) + "/0x0055D Pokemon X";
    const std::string other = std::string(root) + "/0x12345 Other";
    makeDir(matching); makeDir(other);
    makeDir(matching + "/20261012-083000");
    makeDir(matching + "/20261013-090000");
    std::ofstream(matching + "/not-a-backup", std::ios::binary) << "x";

    std::string title_directory;
    require(romm3ds::findCheckpointTitleDirectory(root, 0x0004000000055D00ULL,
                                                   title_directory),
            "matching Checkpoint title directory found");
    require(title_directory == matching, "exact matching title directory retained");

    std::vector<romm3ds::CheckpointBackup> backups;
    require(romm3ds::scanCheckpointBackups(title_directory, backups),
            "Checkpoint backup directories scan");
    require(backups.size() == 2, "only backup directories retained");
    require(backups[0].name == "20261013-090000", "newest lexical backup first");

    std::remove((matching + "/not-a-backup").c_str());
    ::rmdir((matching + "/20261012-083000").c_str());
    ::rmdir((matching + "/20261013-090000").c_str());
    ::rmdir(matching.c_str()); ::rmdir(other.c_str()); ::rmdir(root);
}

void test_packages_and_restores_checkpoint_tree_with_bounds() {
    char root_template[] = "/tmp/pocketshelf-checkpoint-package-XXXXXX";
    char* root = mkdtemp(root_template);
    require(root != nullptr, "package temporary root created");
    const std::string source = std::string(root) + "/source";
    const std::string nested = source + "/nested";
    const std::string title = std::string(root) + "/0x0055D Pokemon X";
    makeDir(source); makeDir(nested); makeDir(title);
    std::ofstream(source + "/main", std::ios::binary) << "SAVE";
    std::ofstream(nested + "/config.dat", std::ios::binary) << "CFG";
    const std::string package = std::string(root) + "/backup.pscp.tmp";

    romm3ds::CheckpointPackageError error = romm3ds::CheckpointPackageError::None;
    require(romm3ds::createCheckpointPackage(source, "20261013-090000",
                                              0x0004000000055D00ULL, package, error),
            "bounded Checkpoint package created");
    struct stat info {};
    require(::stat(package.c_str(), &info) == 0 && info.st_size > 0,
            "package exists on disk");

    std::string restored;
    require(romm3ds::restoreCheckpointPackage(package, title,
                                               0x0004000000055D00ULL,
                                               "20261013-100000", restored, error),
            "Checkpoint package restored into backup folder");
    require(restored == title + "/20261013-100000", "requested safe restore name used");
    std::ifstream main_file(restored + "/main", std::ios::binary);
    std::string main_contents((std::istreambuf_iterator<char>(main_file)), {});
    require(main_contents == "SAVE", "root save file restored");
    std::ifstream nested_file(restored + "/nested/config.dat", std::ios::binary);
    std::string nested_contents((std::istreambuf_iterator<char>(nested_file)), {});
    require(nested_contents == "CFG", "nested save file restored");

    std::string restored_again;
    require(romm3ds::restoreCheckpointPackage(package, title,
                                               0x0004000000055D00ULL,
                                               "20261013-100000", restored_again, error),
            "restore name collision gets a new backup directory");
    require(restored_again == title + "/20261013-100000-1",
            "existing Checkpoint backup is never overwritten");

    std::remove((restored_again + "/nested/config.dat").c_str());
    std::remove((restored_again + "/main").c_str());
    ::rmdir((restored_again + "/nested").c_str()); ::rmdir(restored_again.c_str());
    std::remove((restored + "/nested/config.dat").c_str());
    std::remove((restored + "/main").c_str());
    ::rmdir((restored + "/nested").c_str()); ::rmdir(restored.c_str());
    std::remove(package.c_str());
    std::remove((source + "/nested/config.dat").c_str());
    std::remove((source + "/main").c_str());
    ::rmdir(nested.c_str()); ::rmdir(source.c_str()); ::rmdir(title.c_str()); ::rmdir(root);
}

void test_rejects_wrong_title_traversal_and_oversize_packages() {
    char root_template[] = "/tmp/pocketshelf-checkpoint-reject-XXXXXX";
    char* root = mkdtemp(root_template);
    require(root != nullptr, "reject temporary root created");
    const std::string source = std::string(root) + "/source";
    const std::string title = std::string(root) + "/0x0055D Pokemon X";
    makeDir(source); makeDir(title);
    std::ofstream(source + "/main", std::ios::binary) << "SAVE";
    const std::string package = std::string(root) + "/backup.pscp.tmp";
    romm3ds::CheckpointPackageError error = romm3ds::CheckpointPackageError::None;
    require(romm3ds::createCheckpointPackage(source, "backup",
                                              0x0004000000055D00ULL, package, error),
            "small package created");
    std::string restored;
    require(!romm3ds::restoreCheckpointPackage(package, title,
                                                0x0004000000123400ULL,
                                                "restore", restored, error),
            "package bound to another title rejected");
    require(error == romm3ds::CheckpointPackageError::WrongTitle,
            "wrong-title error retained");
    require(!romm3ds::restoreCheckpointPackage(package, title,
                                                0x0004000000055D00ULL,
                                                "../restore", restored, error),
            "unsafe restore name rejected");
    require(!romm3ds::createCheckpointPackage(source, std::string(129, 'a'),
                                               0x0004000000055D00ULL, package, error),
            "backup name too long for package header rejected");
    require(error == romm3ds::CheckpointPackageError::UnsafePath,
            "long backup name error retained");

    std::ofstream huge(source + "/huge", std::ios::binary);
    std::string block(4096, 'x');
    for (std::size_t written = 0; written <= romm3ds::kMaxCheckpointPackageBytes;
         written += block.size()) huge.write(block.data(), block.size());
    huge.close();
    require(!romm3ds::createCheckpointPackage(source, "backup",
                                               0x0004000000055D00ULL, package, error),
            "oversize backup rejected before upload");
    require(error == romm3ds::CheckpointPackageError::PackageTooLarge,
            "oversize package error retained");

    std::remove((source + "/huge").c_str()); std::remove((source + "/main").c_str());
    std::remove(package.c_str()); ::rmdir(source.c_str()); ::rmdir(title.c_str()); ::rmdir(root);
}

void test_checkpoint_binding_is_atomic_and_strict() {
    char root_template[] = "/tmp/pocketshelf-checkpoint-binding-XXXXXX";
    char* root = mkdtemp(root_template);
    require(root != nullptr, "binding temporary root created");
    const std::string path = std::string(root) + "/42.titleid";
    require(romm3ds::saveCheckpointBindingAtomic(path, 0x0004000000055D00ULL),
            "binding saved atomically");
    std::uint64_t title_id = 0;
    require(romm3ds::loadCheckpointBinding(path, title_id), "binding loads");
    require(title_id == 0x0004000000055D00ULL, "binding title id retained");
    std::ofstream(path, std::ios::binary | std::ios::trunc) << "../bad\n";
    require(!romm3ds::loadCheckpointBinding(path, title_id), "malformed binding rejected");
    std::remove(path.c_str()); ::rmdir(root);
}
}

int main() {
    test_parses_checkpoint_title_folders_from_authoritative_layout();
    test_discovers_only_matching_checkpoint_title_and_backup_directories();
    test_packages_and_restores_checkpoint_tree_with_bounds();
    test_rejects_wrong_title_traversal_and_oversize_packages();
    test_checkpoint_binding_is_atomic_and_strict();
    std::cout << "All Checkpoint sync tests passed\n";
    return 0;
}
