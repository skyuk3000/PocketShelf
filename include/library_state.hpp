#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace romm3ds {

struct LocalPayloadState {
    bool present = false;
    std::uint64_t size = 0;
    std::int64_t modified = 0;
};

LocalPayloadState inspectLocalPayload(const std::string& path) noexcept;
std::string installReceiptPath(std::string_view root, std::uint64_t file_id);
bool saveInstallReceiptAtomic(const std::string& path, std::uint64_t title_id);
bool loadInstallReceipt(const std::string& path, std::uint64_t& title_id);

} // namespace romm3ds
