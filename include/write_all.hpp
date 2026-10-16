#pragma once

#include <cstdint>

namespace romm3ds {

enum class WriteAllStatus {
    Complete,
    WriteError,
    InvalidProgress,
};

struct WriteAllOutcome {
    WriteAllStatus status;
    std::int32_t result;
};

template <typename WriteFn>
WriteAllOutcome writeAll(const std::uint8_t* data, std::uint32_t size,
                         std::uint64_t offset, WriteFn&& write) {
    std::uint32_t consumed = 0;
    std::int32_t result = 0;
    while (consumed < size) {
        const std::uint32_t remaining = size - consumed;
        std::uint32_t written = 0;
        result = write(&written, offset, data + consumed, remaining);
        if (result < 0) return {WriteAllStatus::WriteError, result};
        if (written == 0 || written > remaining) {
            return {WriteAllStatus::InvalidProgress, result};
        }
        consumed += written;
        offset += written;
    }
    return {WriteAllStatus::Complete, result};
}

} // namespace romm3ds
