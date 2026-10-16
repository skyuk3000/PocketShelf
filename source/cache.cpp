#include "cache.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

namespace romm3ds {
namespace {

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

void appendEscaped(std::string& output, const std::string& value) {
    output.push_back('"');
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (ch < 0x20) {
                    output += "\\u00";
                    output.push_back(hex[ch >> 4]);
                    output.push_back(hex[ch & 15]);
                } else output.push_back(static_cast<char>(ch));
        }
    }
    output.push_back('"');
}

void appendField(std::string& output, const char* key, const std::string& value) {
    output += ",\"";
    output += key;
    output += "\":";
    appendEscaped(output, value);
}

void appendScreenshots(std::string& output, const std::vector<std::string>& screenshots) {
    output += ",\"screenshots\":[";
    const std::size_t count = std::min(screenshots.size(), kMaxScreenshots);
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) output.push_back(',');
        appendEscaped(output, screenshots[index]);
    }
    output.push_back(']');
}

std::string serialize(const CatalogEntry& entry) {
    std::string line = "{\"rom_id\":" + std::to_string(entry.rom_id) +
                       ",\"file_id\":" + std::to_string(entry.file_id) +
                       ",\"size\":" + std::to_string(entry.size) +
                       ",\"favorite\":" + (entry.favorite ? "true" : "false");
    appendField(line, "title", entry.title);
    appendField(line, "platform", entry.platform);
    line += ",\"regions\":[";
    appendEscaped(line, entry.region);
    line += ']';
    appendField(line, "file_name", entry.file_name);
    appendField(line, "type", entry.type);
    appendField(line, "summary", entry.summary);
    appendField(line, "status", entry.status);
    appendField(line, "genres", entry.genres);
    appendField(line, "franchises", entry.franchises);
    appendField(line, "collections", entry.collections);
    appendField(line, "publishers", entry.publishers);
    appendField(line, "developers", entry.developers);
    line += ",\"rating\":" + std::to_string(entry.rating);
    line += ",\"difficulty\":" + std::to_string(entry.difficulty);
    line += ",\"completion\":" + std::to_string(entry.completion);
    line += ",\"now_playing\":" + std::string(entry.now_playing ? "true" : "false");
    line += ",\"backlogged\":" + std::string(entry.backlogged ? "true" : "false");
    line += ",\"has_last_played\":" + std::string(entry.has_last_played ? "true" : "false");
    line += ",\"metadata_flags\":" + std::to_string(entry.metadata_flags);
    appendField(line, "action", entry.action == CatalogAction::InstallCia ? "install_cia" : "download");
    appendField(line, "cover_url", entry.cover_url);
    appendScreenshots(line, entry.screenshots);
    appendField(line, "download_url", entry.download_url);
    line += "}\n";
    return line;
}

} // namespace

bool saveCatalogCache(const std::string& path, const std::vector<CatalogEntry>& entries) {
    if (entries.size() > kMaxCatalogEntries || !ensureParentDirectories(path)) return false;
    std::string contents;
    for (const CatalogEntry& entry : entries) {
        const std::string line = serialize(entry);
        if (line.size() > kMaxCatalogLineBytes || contents.size() + line.size() > kMaxCatalogCacheBytes) return false;
        contents += line;
    }
    const std::string temporary = path + ".tmp";
    std::remove(temporary.c_str());
    FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) return false;
    const bool wrote = std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    const bool synced = wrote && std::fflush(file) == 0 && fsync(fileno(file)) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!wrote || !synced || !closed) { std::remove(temporary.c_str()); return false; }
    const std::string backup = path + ".bak";
    std::remove(backup.c_str());
    struct stat info {};
    if (::stat(path.c_str(), &info) == 0) {
        if (std::rename(path.c_str(), backup.c_str()) != 0) {
            std::remove(temporary.c_str());
            return false;
        }
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::rename(backup.c_str(), path.c_str());
        std::remove(temporary.c_str());
        return false;
    }
    std::remove(backup.c_str());
    return true;
}

bool loadCatalogCache(const std::string& path, std::vector<CatalogEntry>& entries) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::string contents;
    char buffer[4096];
    while (true) {
        const std::size_t count = std::fread(buffer, 1, sizeof(buffer), file);
        if (contents.size() + count > kMaxCatalogCacheBytes) { std::fclose(file); return false; }
        contents.append(buffer, count);
        if (count < sizeof(buffer)) {
            const bool ok = std::feof(file) != 0 && std::ferror(file) == 0;
            std::fclose(file);
            if (!ok) return false;
            break;
        }
    }
    CatalogParser parser;
    if (!parser.feed(contents) || !parser.finish()) return false;
    entries = parser.entries();
    return true;
}

std::string coverCachePath(const std::string& directory, std::uint64_t file_id) {
    if (file_id == 0) return {};
    return directory + "/" + std::to_string(file_id) + ".img";
}

std::string screenshotCachePath(const std::string& directory, std::uint64_t rom_id,
                                std::size_t index, std::string_view validated_url) {
    if (rom_id == 0 || validated_url.empty() || index >= kMaxScreenshots) return {};
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char ch : validated_url) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    char name[80];
    std::snprintf(name, sizeof(name), "/s%llu-%lu-%016llx.img",
                  static_cast<unsigned long long>(rom_id),
                  static_cast<unsigned long>(index),
                  static_cast<unsigned long long>(hash));
    return directory + name;
}

} // namespace romm3ds
