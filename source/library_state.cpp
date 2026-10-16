#include "library_state.hpp"

#include <charconv>
#include <cstdio>
#include <sys/stat.h>

namespace romm3ds {

LocalPayloadState inspectLocalPayload(const std::string& path) noexcept {
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return {};
    return {true, static_cast<std::uint64_t>(info.st_size),
            static_cast<std::int64_t>(info.st_mtime)};
}

std::string installReceiptPath(std::string_view root, std::uint64_t file_id) {
    while (!root.empty() && root.back() == '/') root.remove_suffix(1);
    char id[32];
    std::snprintf(id, sizeof(id), "%llu", static_cast<unsigned long long>(file_id));
    return std::string(root) + "/" + id + ".titleid";
}

bool saveInstallReceiptAtomic(const std::string& path, std::uint64_t title_id) {
    char contents[18];
    const int length = std::snprintf(contents, sizeof(contents), "%016llX\n",
                                     static_cast<unsigned long long>(title_id));
    if (length != 17) return false;
    const std::string temporary = path + ".tmp";
    FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) return false;
    const bool wrote = std::fwrite(contents, 1, 17, file) == 17;
    const bool flushed = wrote && std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!(wrote && flushed && closed)) {
        std::remove(temporary.c_str());
        return false;
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}

bool loadInstallReceipt(const std::string& path, std::uint64_t& title_id) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    char contents[19]{};
    const std::size_t length = std::fread(contents, 1, sizeof(contents), file);
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    if (!ok || length != 17 || contents[16] != '\n') return false;
    std::uint64_t parsed = 0;
    const auto converted = std::from_chars(contents, contents + 16, parsed, 16);
    if (converted.ec != std::errc{} || converted.ptr != contents + 16) return false;
    title_id = parsed;
    return true;
}

} // namespace romm3ds
