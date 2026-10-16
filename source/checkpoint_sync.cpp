#include "checkpoint_sync.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <sys/stat.h>
#include <unistd.h>

namespace romm3ds {
namespace {

constexpr std::array<unsigned char, 8> kMagic = {'P', 'S', 'C', 'P', 1, '\r', '\n', 0};
constexpr std::size_t kCopyBufferSize = 32 * 1024;
constexpr unsigned kMaxDepth = 16;

struct PackageEntry {
    std::string relative_path;
    std::string source_path;
    std::uint64_t size = 0;
};

struct PackageHeader {
    std::uint64_t title_id = 0;
    std::uint64_t total_data = 0;
    std::uint16_t file_count = 0;
    std::string backup_name;
};

bool fail(CheckpointPackageError value, CheckpointPackageError& error) {
    error = value;
    return false;
}

bool safeSegment(std::string_view value) {
    if (value.empty() || value == "." || value == ".." || value.front() == '.') return false;
    for (const unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7f || ch == '/' || ch == '\\' || ch == ':') return false;
    }
    return true;
}

bool safeRelativePath(std::string_view path) {
    if (path.empty() || path.size() > kMaxCheckpointRelativePathBytes || path.front() == '/')
        return false;
    std::size_t start = 0;
    while (start < path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::string_view segment = path.substr(
            start, slash == std::string_view::npos ? path.size() - start : slash - start);
        if (!safeSegment(segment)) return false;
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return true;
}

bool regularFileNoLinks(const std::string& path, struct stat& info) {
    return ::lstat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool directoryNoLinks(const std::string& path, struct stat& info) {
    return ::lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool collectEntries(const std::string& root, const std::string& relative, unsigned depth,
                    std::vector<PackageEntry>& entries, std::uint64_t& total,
                    CheckpointPackageError& error) {
    if (depth > kMaxDepth) return fail(CheckpointPackageError::UnsafePath, error);
    const std::string directory = relative.empty() ? root : root + "/" + relative;
    DIR* handle = opendir(directory.c_str());
    if (!handle) return fail(CheckpointPackageError::OpenFailed, error);
    bool ok = true;
    while (dirent* item = readdir(handle)) {
        const std::string name = item->d_name;
        if (name == "." || name == "..") continue;
        if (!safeSegment(name)) { fail(CheckpointPackageError::UnsafePath, error); ok = false; break; }
        const std::string child_relative = relative.empty() ? name : relative + "/" + name;
        if (!safeRelativePath(child_relative)) {
            fail(CheckpointPackageError::UnsafePath, error); ok = false; break;
        }
        const std::string child = root + "/" + child_relative;
        struct stat info {};
        if (::lstat(child.c_str(), &info) != 0) {
            fail(CheckpointPackageError::IoFailed, error); ok = false; break;
        }
        if (S_ISDIR(info.st_mode)) {
            if (!collectEntries(root, child_relative, depth + 1, entries, total, error)) {
                ok = false; break;
            }
        } else if (S_ISREG(info.st_mode)) {
            if (info.st_size < 0) { fail(CheckpointPackageError::IoFailed, error); ok = false; break; }
            if (entries.size() >= kMaxCheckpointPackageEntries) {
                fail(CheckpointPackageError::TooManyEntries, error); ok = false; break;
            }
            const std::uint64_t size = static_cast<std::uint64_t>(info.st_size);
            if (size > kMaxCheckpointPackageBytes || total > kMaxCheckpointPackageBytes - size) {
                fail(CheckpointPackageError::PackageTooLarge, error); ok = false; break;
            }
            total += size;
            entries.push_back(PackageEntry{child_relative, child, size});
        } else {
            fail(CheckpointPackageError::InvalidSource, error); ok = false; break;
        }
    }
    closedir(handle);
    return ok;
}

template <typename T>
bool writeLittle(std::FILE* file, T value) {
    std::array<unsigned char, sizeof(T)> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<unsigned char>((value >> (i * 8)) & 0xff);
    return std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
}

template <typename T>
bool readLittle(std::FILE* file, T& value) {
    std::array<unsigned char, sizeof(T)> bytes{};
    if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) return false;
    value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i)
        value |= static_cast<T>(bytes[i]) << (i * 8);
    return true;
}

bool copyBytes(std::FILE* input, std::FILE* output, std::uint64_t size) {
    std::unique_ptr<unsigned char[]> buffer(new (std::nothrow) unsigned char[kCopyBufferSize]);
    if (!buffer) return false;
    std::uint64_t remaining = size;
    while (remaining != 0) {
        const std::size_t chunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, kCopyBufferSize));
        if (std::fread(buffer.get(), 1, chunk, input) != chunk) return false;
        if (output && std::fwrite(buffer.get(), 1, chunk, output) != chunk) return false;
        remaining -= chunk;
    }
    return true;
}

bool readHeader(std::FILE* file, PackageHeader& header) {
    std::array<unsigned char, kMagic.size()> magic{};
    if (std::fread(magic.data(), 1, magic.size(), file) != magic.size() || magic != kMagic)
        return false;
    std::uint16_t name_length = 0;
    if (!readLittle(file, header.title_id) || !readLittle(file, header.total_data) ||
        !readLittle(file, header.file_count) || !readLittle(file, name_length)) return false;
    if (header.title_id == 0 || header.file_count == 0 ||
        header.file_count > kMaxCheckpointPackageEntries || name_length == 0 || name_length > 128 ||
        header.total_data > kMaxCheckpointPackageBytes) return false;
    header.backup_name.resize(name_length);
    return std::fread(header.backup_name.data(), 1, name_length, file) == name_length &&
           safeSegment(header.backup_name);
}

bool skipAndValidateEntries(std::FILE* file, const PackageHeader& header,
                            std::vector<PackageEntry>& entries) {
    entries.clear();
    std::uint64_t total = 0;
    for (std::uint16_t i = 0; i < header.file_count; ++i) {
        std::uint16_t path_length = 0;
        std::uint64_t size = 0;
        if (!readLittle(file, path_length) || !readLittle(file, size) || path_length == 0 ||
            path_length > kMaxCheckpointRelativePathBytes || size > kMaxCheckpointPackageBytes ||
            total > kMaxCheckpointPackageBytes - size) return false;
        std::string relative(path_length, '\0');
        if (std::fread(relative.data(), 1, path_length, file) != path_length ||
            !safeRelativePath(relative)) return false;
        if (std::any_of(entries.begin(), entries.end(), [&](const PackageEntry& entry) {
                return entry.relative_path == relative;
            })) return false;
        entries.push_back(PackageEntry{relative, {}, size});
        total += size;
        if (!copyBytes(file, nullptr, size)) return false;
    }
    if (total != header.total_data) return false;
    return std::fgetc(file) == EOF && std::feof(file) != 0;
}

bool ensureDirectoriesForFile(const std::string& root, std::string_view relative) {
    std::size_t slash = relative.find('/');
    while (slash != std::string_view::npos) {
        const std::string path = root + "/" + std::string(relative.substr(0, slash));
        if (::mkdir(path.c_str(), 0777) != 0 && errno != EEXIST) return false;
        slash = relative.find('/', slash + 1);
    }
    return true;
}

bool removeTree(const std::string& path) {
    DIR* handle = opendir(path.c_str());
    if (!handle) return errno == ENOENT;
    bool ok = true;
    while (dirent* item = readdir(handle)) {
        const std::string name = item->d_name;
        if (name == "." || name == "..") continue;
        const std::string child = path + "/" + name;
        struct stat info {};
        if (::lstat(child.c_str(), &info) != 0) { ok = false; continue; }
        if (S_ISDIR(info.st_mode)) ok = removeTree(child) && ok;
        else ok = std::remove(child.c_str()) == 0 && ok;
    }
    closedir(handle);
    return ::rmdir(path.c_str()) == 0 && ok;
}

std::string parentPath(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

bool ensureParentPath(const std::string& path) {
    const std::string parent = parentPath(path);
    if (parent.empty()) return true;
    std::string current;
    std::size_t position = 0;
    if (parent.compare(0, 6, "sdmc:/") == 0) { current = "sdmc:"; position = 6; }
    else if (parent.front() == '/') { current = "/"; position = 1; }
    while (position <= parent.size()) {
        const std::size_t slash = parent.find('/', position);
        const std::string segment = parent.substr(position,
            slash == std::string::npos ? parent.size() - position : slash - position);
        if (!segment.empty()) {
            if (!current.empty() && current.back() != '/') current.push_back('/');
            current += segment;
            if (::mkdir(current.c_str(), 0777) != 0 && errno != EEXIST) return false;
        }
        if (slash == std::string::npos) break;
        position = slash + 1;
    }
    return true;
}

std::string hexTitle(std::uint64_t title_id) {
    char value[17];
    std::snprintf(value, sizeof(value), "%016llX", static_cast<unsigned long long>(title_id));
    return value;
}

} // namespace

std::uint32_t checkpointUniqueId(std::uint64_t title_id) noexcept {
    return static_cast<std::uint32_t>(title_id) >> 8;
}

bool parseCheckpointTitleFolder(std::string_view name, std::uint32_t& unique_id) noexcept {
    unique_id = 0;
    if (name.size() < 9 || name.substr(0, 2) != "0x") return false;
    const std::size_t space = name.find(' ', 2);
    if (space == std::string_view::npos || space < 3 || space > 10 ||
        !safeSegment(name.substr(space + 1))) return false;
    std::uint64_t parsed = 0;
    for (std::size_t i = 2; i < space; ++i) {
        const unsigned char ch = static_cast<unsigned char>(name[i]);
        unsigned value = 0;
        if (ch >= '0' && ch <= '9') value = ch - '0';
        else if (ch >= 'a' && ch <= 'f') value = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') value = ch - 'A' + 10;
        else return false;
        parsed = (parsed << 4) | value;
        if (parsed > std::numeric_limits<std::uint32_t>::max()) return false;
    }
    unique_id = static_cast<std::uint32_t>(parsed);
    return true;
}

bool findCheckpointTitleDirectory(const std::string& saves_root, std::uint64_t title_id,
                                  std::string& output) {
    output.clear();
    DIR* handle = opendir(saves_root.c_str());
    if (!handle) return false;
    const std::uint32_t expected = checkpointUniqueId(title_id);
    std::size_t seen = 0;
    bool ambiguous = false;
    while (dirent* item = readdir(handle)) {
        const std::string name = item->d_name;
        if (name == "." || name == "..") continue;
        if (++seen > kMaxCheckpointTitleDirectories) { output.clear(); ambiguous = true; break; }
        std::uint32_t unique = 0;
        if (!parseCheckpointTitleFolder(name, unique) || unique != expected) continue;
        const std::string candidate = saves_root + "/" + name;
        struct stat info {};
        if (!directoryNoLinks(candidate, info)) continue;
        if (!output.empty()) { output.clear(); ambiguous = true; break; }
        output = candidate;
    }
    closedir(handle);
    return !ambiguous && !output.empty();
}

bool scanCheckpointBackups(const std::string& title_directory,
                           std::vector<CheckpointBackup>& output) {
    output.clear();
    DIR* handle = opendir(title_directory.c_str());
    if (!handle) return false;
    while (dirent* item = readdir(handle)) {
        const std::string name = item->d_name;
        if (name == "." || name == ".." || !safeSegment(name) ||
            name.find(".pocketshelf-") == 0) continue;
        const std::string path = title_directory + "/" + name;
        struct stat info {};
        if (!directoryNoLinks(path, info)) continue;
        if (output.size() >= kMaxCheckpointBackupsPerTitle) {
            closedir(handle); output.clear(); return false;
        }
        output.push_back(CheckpointBackup{name, path, static_cast<std::int64_t>(info.st_mtime)});
    }
    closedir(handle);
    std::sort(output.begin(), output.end(), [](const auto& left, const auto& right) {
        if (left.modified != right.modified) return left.modified > right.modified;
        return left.name > right.name;
    });
    return true;
}

bool createCheckpointPackage(const std::string& backup_directory,
                             std::string_view backup_name,
                             std::uint64_t title_id,
                             const std::string& output_path,
                             CheckpointPackageError& error) {
    error = CheckpointPackageError::None;
    if (title_id == 0 || backup_name.size() > 128 || !safeSegment(backup_name))
        return fail(CheckpointPackageError::UnsafePath, error);
    struct stat root_info {};
    if (!directoryNoLinks(backup_directory, root_info))
        return fail(CheckpointPackageError::InvalidSource, error);
    std::vector<PackageEntry> entries;
    std::uint64_t total = 0;
    if (!collectEntries(backup_directory, {}, 0, entries, total, error)) return false;
    if (entries.empty()) return fail(CheckpointPackageError::EmptyBackup, error);
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.relative_path < right.relative_path;
    });
    std::uint64_t package_size = kMagic.size() + 8 + 8 + 2 + 2 + backup_name.size();
    for (const auto& entry : entries) {
        const std::uint64_t overhead = 2 + 8 + entry.relative_path.size();
        if (package_size > kMaxCheckpointPackageBytes - overhead ||
            package_size + overhead > kMaxCheckpointPackageBytes - entry.size)
            return fail(CheckpointPackageError::PackageTooLarge, error);
        package_size += overhead + entry.size;
    }
    std::FILE* output = std::fopen(output_path.c_str(), "wb");
    if (!output) return fail(CheckpointPackageError::OpenFailed, error);
    bool ok = std::fwrite(kMagic.data(), 1, kMagic.size(), output) == kMagic.size() &&
              writeLittle(output, title_id) && writeLittle(output, total) &&
              writeLittle(output, static_cast<std::uint16_t>(entries.size())) &&
              writeLittle(output, static_cast<std::uint16_t>(backup_name.size())) &&
              std::fwrite(backup_name.data(), 1, backup_name.size(), output) == backup_name.size();
    for (const auto& entry : entries) {
        if (!ok) break;
        ok = writeLittle(output, static_cast<std::uint16_t>(entry.relative_path.size())) &&
             writeLittle(output, entry.size) &&
             std::fwrite(entry.relative_path.data(), 1, entry.relative_path.size(), output) ==
                 entry.relative_path.size();
        std::FILE* input = ok ? std::fopen(entry.source_path.c_str(), "rb") : nullptr;
        if (!input) { ok = false; break; }
        ok = copyBytes(input, output, entry.size) && std::fgetc(input) == EOF &&
             std::feof(input) != 0;
        ok = std::fclose(input) == 0 && ok;
    }
    ok = std::fflush(output) == 0 && ok;
    ok = std::fclose(output) == 0 && ok;
    if (!ok) { std::remove(output_path.c_str()); return fail(CheckpointPackageError::IoFailed, error); }
    return true;
}

bool restoreCheckpointPackage(const std::string& package_path,
                              const std::string& title_directory,
                              std::uint64_t expected_title_id,
                              std::string_view restore_name,
                              std::string& restored_path,
                              CheckpointPackageError& error) {
    restored_path.clear(); error = CheckpointPackageError::None;
    if (!safeSegment(restore_name)) return fail(CheckpointPackageError::UnsafePath, error);
    struct stat package_info {}, title_info {};
    if (!regularFileNoLinks(package_path, package_info) || package_info.st_size < 0 ||
        static_cast<std::uint64_t>(package_info.st_size) > kMaxCheckpointPackageBytes)
        return fail(CheckpointPackageError::PackageTooLarge, error);
    if (!directoryNoLinks(title_directory, title_info))
        return fail(CheckpointPackageError::InvalidSource, error);
    std::FILE* file = std::fopen(package_path.c_str(), "rb");
    if (!file) return fail(CheckpointPackageError::OpenFailed, error);
    PackageHeader header;
    std::vector<PackageEntry> entries;
    bool valid = readHeader(file, header);
    if (valid && header.title_id != expected_title_id) {
        std::fclose(file); return fail(CheckpointPackageError::WrongTitle, error);
    }
    valid = valid && skipAndValidateEntries(file, header, entries);
    std::fclose(file);
    if (!valid) return fail(CheckpointPackageError::MalformedPackage, error);

    std::string target = title_directory + "/" + std::string(restore_name);
    const std::string target_base = target;
    struct stat existing {};
    for (unsigned suffix = 1; ::lstat(target.c_str(), &existing) == 0; ++suffix) {
        if (suffix >= 1000) return fail(CheckpointPackageError::ActivateFailed, error);
        target = target_base + "-" + std::to_string(suffix);
    }
    const std::string temporary = title_directory + "/.pocketshelf-restore.tmp";
    removeTree(temporary);
    if (::mkdir(temporary.c_str(), 0777) != 0)
        return fail(CheckpointPackageError::OpenFailed, error);

    file = std::fopen(package_path.c_str(), "rb");
    PackageHeader second;
    bool ok = file && readHeader(file, second) && second.title_id == expected_title_id &&
              second.file_count == entries.size();
    for (std::size_t i = 0; ok && i < entries.size(); ++i) {
        std::uint16_t path_length = 0;
        std::uint64_t size = 0;
        ok = readLittle(file, path_length) && readLittle(file, size) &&
             path_length == entries[i].relative_path.size() && size == entries[i].size;
        std::string relative(path_length, '\0');
        ok = ok && std::fread(relative.data(), 1, path_length, file) == path_length &&
             relative == entries[i].relative_path && ensureDirectoriesForFile(temporary, relative);
        const std::string destination = temporary + "/" + relative;
        std::FILE* output = ok ? std::fopen(destination.c_str(), "wb") : nullptr;
        if (!output) { ok = false; break; }
        ok = copyBytes(file, output, size) && std::fflush(output) == 0;
        ok = std::fclose(output) == 0 && ok;
    }
    if (file) ok = std::fclose(file) == 0 && ok;
    if (!ok) { removeTree(temporary); return fail(CheckpointPackageError::IoFailed, error); }
    if (std::rename(temporary.c_str(), target.c_str()) != 0) {
        removeTree(temporary); return fail(CheckpointPackageError::ActivateFailed, error);
    }
    restored_path = target;
    return true;
}

const char* checkpointPackageErrorMessage(CheckpointPackageError error) noexcept {
    switch (error) {
        case CheckpointPackageError::None: return "no error";
        case CheckpointPackageError::OpenFailed: return "could not open Checkpoint data";
        case CheckpointPackageError::InvalidSource: return "Checkpoint source is not a safe directory";
        case CheckpointPackageError::UnsafePath: return "Checkpoint data contains an unsafe path";
        case CheckpointPackageError::TooManyEntries: return "Checkpoint backup exceeds 128 files";
        case CheckpointPackageError::PackageTooLarge: return "Checkpoint package exceeds 512 KiB";
        case CheckpointPackageError::MalformedPackage: return "malformed PocketShelf Checkpoint package";
        case CheckpointPackageError::WrongTitle: return "Checkpoint package belongs to another title";
        case CheckpointPackageError::EmptyBackup: return "Checkpoint backup contains no files";
        case CheckpointPackageError::IoFailed: return "Checkpoint package I/O failed";
        case CheckpointPackageError::ActivateFailed: return "could not activate restored Checkpoint backup";
    }
    return "Checkpoint package error";
}

std::string checkpointPackageFilename(std::uint64_t title_id) {
    return "PocketShelf-Checkpoint-" + hexTitle(title_id) + kCheckpointPackageExtension;
}

bool isCheckpointPackageFilename(std::string_view file_name,
                                 std::uint64_t title_id) noexcept {
    const std::string prefix = "PocketShelf-Checkpoint-" + hexTitle(title_id);
    return file_name.size() >= prefix.size() + std::strlen(kCheckpointPackageExtension) &&
           file_name.substr(0, prefix.size()) == prefix &&
           file_name.substr(file_name.size() - std::strlen(kCheckpointPackageExtension)) ==
               kCheckpointPackageExtension;
}

std::string checkpointBindingPath(std::string_view root, std::uint64_t rom_id) {
    while (!root.empty() && root.back() == '/') root.remove_suffix(1);
    return std::string(root) + "/" + std::to_string(rom_id) + ".titleid";
}

bool saveCheckpointBindingAtomic(const std::string& path, std::uint64_t title_id) {
    if (title_id == 0 || !ensureParentPath(path)) return false;
    const std::string temporary = path + ".tmp";
    std::FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) return false;
    const std::string contents = hexTitle(title_id) + "\n";
    bool ok = std::fwrite(contents.data(), 1, contents.size(), file) == contents.size() &&
              std::fflush(file) == 0;
    ok = std::fclose(file) == 0 && ok;
    if (!ok || std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str()); return false;
    }
    return true;
}

bool loadCheckpointBinding(const std::string& path, std::uint64_t& title_id) {
    title_id = 0;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::string value((std::istreambuf_iterator<char>(input)), {});
    if (value.size() != 17 || value.back() != '\n') return false;
    value.pop_back();
    std::uint64_t parsed = 0;
    for (const unsigned char ch : value) {
        unsigned digit = 0;
        if (ch >= '0' && ch <= '9') digit = ch - '0';
        else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
        else return false;
        parsed = (parsed << 4) | digit;
    }
    if (parsed == 0) return false;
    title_id = parsed;
    return true;
}

} // namespace romm3ds
