#include "save_data.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

namespace romm3ds {
namespace {

constexpr std::size_t kMaxSaveFilename = 255;
constexpr std::size_t kMaxTimestamp = 64;
constexpr unsigned kMaxJsonDepth = 24;
constexpr char kSaveUploadBoundary[] = "PocketShelf3DSBoundary7F4A";

bool fail(SaveParseError value, SaveParseError& error) {
    error = value;
    return false;
}

void appendUtf8(std::string& output, std::uint32_t value) {
    if (value <= 0x7f) output.push_back(static_cast<char>(value));
    else if (value <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (value >> 6)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (value >> 12)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (value >> 18)));
        output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
}

class JsonReader {
public:
    explicit JsonReader(std::string_view input) : input_(input) {}

    void whitespace() {
        while (position_ < input_.size()) {
            const char ch = input_[position_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
            ++position_;
        }
    }

    bool take(char expected) {
        whitespace();
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    bool done() {
        whitespace();
        return position_ == input_.size();
    }

    bool number(std::uint64_t& output) {
        whitespace();
        const std::size_t start = position_;
        while (position_ < input_.size() && input_[position_] >= '0' &&
               input_[position_] <= '9') ++position_;
        if (start == position_) return false;
        const auto converted = std::from_chars(input_.data() + start,
                                                input_.data() + position_, output);
        return converted.ec == std::errc{} && converted.ptr == input_.data() + position_;
    }

    bool boolean(bool& output) {
        whitespace();
        if (input_.substr(position_, 4) == "true") {
            position_ += 4; output = true; return true;
        }
        if (input_.substr(position_, 5) == "false") {
            position_ += 5; output = false; return true;
        }
        return false;
    }

    bool string(std::string& output, std::size_t limit) {
        whitespace();
        if (position_ >= input_.size() || input_[position_++] != '"') return false;
        output.clear();
        while (position_ < input_.size()) {
            unsigned char ch = static_cast<unsigned char>(input_[position_++]);
            if (ch == '"') return true;
            if (ch < 0x20) return false;
            if (ch == '\\') {
                if (position_ >= input_.size()) return false;
                const char escaped = input_[position_++];
                switch (escaped) {
                    case '"': case '\\': case '/': ch = static_cast<unsigned char>(escaped); break;
                    case 'b': ch = '\b'; break;
                    case 'f': ch = '\f'; break;
                    case 'n': ch = '\n'; break;
                    case 'r': ch = '\r'; break;
                    case 't': ch = '\t'; break;
                    case 'u': {
                        std::uint32_t code = 0;
                        if (!hex4(code)) return false;
                        if (code >= 0xd800 && code <= 0xdbff) {
                            if (position_ + 6 > input_.size() || input_[position_] != '\\' ||
                                input_[position_ + 1] != 'u') return false;
                            position_ += 2;
                            std::uint32_t low = 0;
                            if (!hex4(low) || low < 0xdc00 || low > 0xdfff) return false;
                            code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                        } else if (code >= 0xdc00 && code <= 0xdfff) return false;
                        const std::size_t before = output.size();
                        appendUtf8(output, code);
                        if (output.size() > limit) { output.resize(before); return false; }
                        continue;
                    }
                    default: return false;
                }
            }
            if (output.size() >= limit) return false;
            output.push_back(static_cast<char>(ch));
        }
        return false;
    }

    bool skip(unsigned depth = 0) {
        if (depth > kMaxJsonDepth) return false;
        whitespace();
        if (position_ >= input_.size()) return false;
        if (input_[position_] == '"') {
            std::string ignored;
            return string(ignored, kMaxSaveInventoryBytes);
        }
        if (input_[position_] == '{') {
            ++position_;
            if (take('}')) return true;
            while (true) {
                std::string key;
                if (!string(key, 128) || !take(':') || !skip(depth + 1)) return false;
                if (take('}')) return true;
                if (!take(',')) return false;
            }
        }
        if (input_[position_] == '[') {
            ++position_;
            if (take(']')) return true;
            while (true) {
                if (!skip(depth + 1)) return false;
                if (take(']')) return true;
                if (!take(',')) return false;
            }
        }
        const std::size_t start = position_;
        while (position_ < input_.size()) {
            const char ch = input_[position_];
            if (ch == ',' || ch == '}' || ch == ']' || ch == ' ' || ch == '\t' ||
                ch == '\r' || ch == '\n') break;
            ++position_;
        }
        return position_ > start;
    }

private:
    bool hex4(std::uint32_t& output) {
        if (position_ + 4 > input_.size()) return false;
        output = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const unsigned char ch = static_cast<unsigned char>(input_[position_++]);
            output <<= 4;
            if (ch >= '0' && ch <= '9') output |= ch - '0';
            else if (ch >= 'a' && ch <= 'f') output |= ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') output |= ch - 'A' + 10;
            else return false;
        }
        return true;
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

bool parseSave(JsonReader& reader, std::uint64_t expected_rom_id,
               RemoteSaveFile& output, bool& missing, SaveParseError& error) {
    if (!reader.take('{')) return fail(SaveParseError::MalformedJson, error);
    bool found_id = false, found_rom = false, found_name = false;
    bool found_size = false, found_updated = false, found_missing = false;
    if (reader.take('}')) return fail(SaveParseError::MissingField, error);
    while (true) {
        std::string key;
        if (!reader.string(key, 64) || !reader.take(':'))
            return fail(SaveParseError::MalformedJson, error);
        if (key == "id") {
            if (!reader.number(output.id)) return fail(SaveParseError::MalformedJson, error);
            found_id = true;
        } else if (key == "rom_id") {
            if (!reader.number(output.rom_id)) return fail(SaveParseError::MalformedJson, error);
            found_rom = true;
        } else if (key == "file_name") {
            if (!reader.string(output.file_name, kMaxSaveFilename))
                return fail(SaveParseError::FieldTooLong, error);
            found_name = true;
        } else if (key == "file_size_bytes") {
            if (!reader.number(output.size)) return fail(SaveParseError::MalformedJson, error);
            found_size = true;
        } else if (key == "updated_at") {
            if (!reader.string(output.updated_at, kMaxTimestamp))
                return fail(SaveParseError::FieldTooLong, error);
            found_updated = true;
        } else if (key == "missing_from_fs") {
            if (!reader.boolean(missing)) return fail(SaveParseError::MalformedJson, error);
            found_missing = true;
        } else if (!reader.skip()) {
            return fail(SaveParseError::MalformedJson, error);
        }
        if (reader.take('}')) break;
        if (!reader.take(',')) return fail(SaveParseError::MalformedJson, error);
    }
    if (!(found_id && found_rom && found_name && found_size && found_updated && found_missing) ||
        output.id == 0) return fail(SaveParseError::MissingField, error);
    if (output.rom_id != expected_rom_id) return fail(SaveParseError::WrongRom, error);
    if (!isSafeSaveFilename(output.file_name))
        return fail(SaveParseError::UnsafeFilename, error);
    return true;
}

std::string trimBase(std::string_view base) {
    while (!base.empty() && base.back() == '/') base.remove_suffix(1);
    return std::string(base);
}

std::string numeric(std::uint64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    return buffer;
}

bool regularFile(const std::string& path, struct stat& info) {
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

} // namespace

bool parseSaveInventory(std::string_view json, std::uint64_t expected_rom_id,
                        SaveInventory& output, SaveParseError& error) {
    if (json.size() > kMaxSaveInventoryBytes)
        return fail(SaveParseError::InventoryTooLarge, error);
    JsonReader reader(json);
    SaveInventory parsed;
    if (!reader.take('[')) return fail(SaveParseError::MalformedJson, error);
    if (!reader.take(']')) {
        while (true) {
            RemoteSaveFile save;
            bool missing = false;
            if (!parseSave(reader, expected_rom_id, save, missing, error)) return false;
            if (!missing) {
                if (parsed.remote.size() >= kMaxRemoteSavesPerRom)
                    return fail(SaveParseError::TooManyEntries, error);
                parsed.remote.push_back(std::move(save));
            }
            if (reader.take(']')) break;
            if (!reader.take(',')) return fail(SaveParseError::MalformedJson, error);
        }
    }
    if (!reader.done()) return fail(SaveParseError::MalformedJson, error);
    std::sort(parsed.remote.begin(), parsed.remote.end(), [](const auto& left, const auto& right) {
        if (left.updated_at != right.updated_at) return left.updated_at > right.updated_at;
        return left.id > right.id;
    });
    output = std::move(parsed);
    error = SaveParseError::None;
    return true;
}

const char* saveParseErrorMessage(SaveParseError error) noexcept {
    switch (error) {
        case SaveParseError::None: return "no error";
        case SaveParseError::InventoryTooLarge: return "save inventory exceeds 128 KiB";
        case SaveParseError::MalformedJson: return "malformed save inventory JSON";
        case SaveParseError::MissingField: return "save inventory is missing required data";
        case SaveParseError::FieldTooLong: return "save inventory field exceeds its limit";
        case SaveParseError::TooManyEntries: return "save inventory exceeds 32 files";
        case SaveParseError::UnsafeFilename: return "RomM returned an unsafe save filename";
        case SaveParseError::WrongRom: return "RomM returned a save for another ROM";
    }
    return "save inventory parse error";
}

std::string rommSaveInventoryUrl(std::string_view base_url, std::uint64_t rom_id) {
    return trimBase(base_url) + "/api/saves?rom_id=" + numeric(rom_id);
}

std::string rommSaveContentUrl(std::string_view base_url, std::uint64_t save_id) {
    return trimBase(base_url) + "/api/saves/" + numeric(save_id) + "/content";
}

std::string rommSaveUploadUrl(std::string_view base_url, std::uint64_t rom_id) {
    return trimBase(base_url) + "/api/saves?rom_id=" + numeric(rom_id) +
           "&slot=" + kSaveUploadSlot;
}

bool buildSaveUploadBody(std::string_view file_name, const std::uint8_t* data,
                         std::size_t size, std::vector<std::uint8_t>& body,
                         std::string& content_type) {
    body.clear();
    content_type.clear();
    if (!isSafeVersionedUploadFilename(file_name) || size > kMaxSaveUploadBytes ||
        (size != 0 && data == nullptr)) return false;

    const std::string prefix = std::string("--") + kSaveUploadBoundary +
        "\r\nContent-Disposition: form-data; name=\"saveFile\"; filename=\"" +
        std::string(file_name) +
        "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
    const std::string suffix = std::string("\r\n--") + kSaveUploadBoundary + "--\r\n";
    body.reserve(prefix.size() + size + suffix.size());
    body.insert(body.end(), prefix.begin(), prefix.end());
    if (size != 0) body.insert(body.end(), data, data + size);
    body.insert(body.end(), suffix.begin(), suffix.end());
    content_type = std::string("multipart/form-data; boundary=") + kSaveUploadBoundary;
    return true;
}

bool buildSaveUploadBodyFromFile(std::string_view file_name, const std::string& path,
                                 std::size_t size, std::vector<std::uint8_t>& body,
                                 std::string& content_type) {
    body.clear();
    content_type.clear();
    if (!isSafeVersionedUploadFilename(file_name) || size > kMaxSaveUploadBytes) return false;
    const std::string prefix = std::string("--") + kSaveUploadBoundary +
        "\r\nContent-Disposition: form-data; name=\"saveFile\"; filename=\"" +
        std::string(file_name) +
        "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
    const std::string suffix = std::string("\r\n--") + kSaveUploadBoundary + "--\r\n";
    body.resize(prefix.size() + size + suffix.size());
    std::copy(prefix.begin(), prefix.end(), body.begin());
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) { body.clear(); return false; }
    const bool read = size == 0 ||
        std::fread(body.data() + prefix.size(), 1, size, file) == size;
    const bool at_end = std::fgetc(file) == EOF && std::feof(file) != 0;
    const bool closed = std::fclose(file) == 0;
    if (!read || !at_end || !closed) {
        body.clear();
        return false;
    }
    std::copy(suffix.begin(), suffix.end(), body.begin() + prefix.size() + size);
    content_type = std::string("multipart/form-data; boundary=") + kSaveUploadBoundary;
    return true;
}

bool isSafeSaveFilename(std::string_view value) noexcept {
    if (value.empty() || value.size() > kMaxSaveFilename || value.front() == '.' ||
        value == "." || value == "..") return false;
    for (const unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7f || ch == '/' || ch == '\\' || ch == ':' ||
            ch == '"') return false;
    }
    return true;
}

bool isSafeVersionedUploadFilename(std::string_view value) noexcept {
    // RomM's named-slot upload inserts " [YYYY-MM-DD_HH-MM-SS]" (22 bytes).
    return value.size() <= kMaxSaveFilename - 22 && isSafeSaveFilename(value);
}

std::string managedSaveDirectory(std::string_view root, std::uint64_t rom_id) {
    while (!root.empty() && root.back() == '/') root.remove_suffix(1);
    return std::string(root) + "/" + numeric(rom_id);
}

bool scanLocalSaveDirectory(const std::string& directory,
                            std::vector<LocalSaveFile>& output) {
    output.clear();
    DIR* handle = opendir(directory.c_str());
    if (!handle) return false;
    while (dirent* entry = readdir(handle)) {
        const std::string name = entry->d_name;
        if (!isSafeSaveFilename(name) || name.find(".bak-") != std::string::npos ||
            (name.size() >= 13 && name.compare(name.size() - 13, 13, ".download.tmp") == 0)) continue;
        const std::string path = directory + "/" + name;
        struct stat info {};
        if (!regularFile(path, info)) continue;
        if (output.size() >= kMaxLocalSavesPerRom) {
            closedir(handle);
            output.clear();
            return false;
        }
        output.push_back(LocalSaveFile{static_cast<std::uint64_t>(info.st_size),
                                       static_cast<std::int64_t>(info.st_mtime), name, path});
    }
    closedir(handle);
    std::sort(output.begin(), output.end(), [](const auto& left, const auto& right) {
        return left.file_name < right.file_name;
    });
    return true;
}

std::string availableBackupPath(const std::string& target, std::string_view timestamp) {
    const std::string base = target + ".bak-" + std::string(timestamp);
    struct stat info {};
    if (::stat(base.c_str(), &info) != 0) return base;
    for (unsigned index = 1; index < 1000; ++index) {
        const std::string candidate = base + "-" + std::to_string(index);
        if (::stat(candidate.c_str(), &info) != 0) return candidate;
    }
    return base + "-overflow";
}

} // namespace romm3ds
