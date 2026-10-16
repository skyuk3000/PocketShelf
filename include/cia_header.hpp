#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace romm3ds {

enum class CiaHeaderStatus {
    NeedMore,
    Valid,
    Invalid,
};

enum class CiaStreamSizeStatus {
    InProgress,
    Complete,
    Truncated,
    Oversized,
};

enum class CiaInstallDecision {
    InstallNew,
    RequireOverwriteConfirmation,
    InstallOverwrite,
    RefuseUnverified,
    RefuseTitleMismatch,
};

struct CiaHeaderInfo {
    std::uint64_t total_size = 0;
};

namespace cia_header_detail {
inline std::uint16_t read16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(data[1]) << 8;
}

inline std::uint32_t read32(const std::uint8_t* data) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(data[i]) << (8 * i);
    return value;
}

inline std::uint64_t read64(const std::uint8_t* data) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(data[i]) << (8 * i);
    return value;
}

inline bool add(std::uint64_t left, std::uint64_t right, std::uint64_t& output) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) return false;
    output = left + right;
    return true;
}

inline bool align64(std::uint64_t value, std::uint64_t& output) {
    std::uint64_t adjusted = 0;
    if (!add(value, 0x3F, adjusted)) return false;
    output = adjusted & ~std::uint64_t{0x3F};
    return true;
}
} // namespace cia_header_detail

inline CiaHeaderStatus inspectCiaHeader(const std::uint8_t* data, std::size_t size,
                                        CiaHeaderInfo& output) {
    output = {};
    if (size < 0x20) return CiaHeaderStatus::NeedMore;
    if (data == nullptr) return CiaHeaderStatus::Invalid;

    const std::uint32_t header_size = cia_header_detail::read32(data + 0x00);
    const std::uint16_t type = cia_header_detail::read16(data + 0x04);
    const std::uint16_t version = cia_header_detail::read16(data + 0x06);
    const std::uint32_t cert_size = cia_header_detail::read32(data + 0x08);
    const std::uint32_t ticket_size = cia_header_detail::read32(data + 0x0C);
    const std::uint32_t tmd_size = cia_header_detail::read32(data + 0x10);
    const std::uint32_t meta_size = cia_header_detail::read32(data + 0x14);
    const std::uint64_t content_size = cia_header_detail::read64(data + 0x18);

    if (header_size != 0x2020 || type != 0 || version != 0 || cert_size == 0 ||
        ticket_size == 0 || tmd_size == 0 || content_size == 0) {
        return CiaHeaderStatus::Invalid;
    }

    std::uint64_t offset = 0;
    if (!cia_header_detail::align64(header_size, offset) ||
        !cia_header_detail::add(offset, cert_size, offset) ||
        !cia_header_detail::align64(offset, offset) ||
        !cia_header_detail::add(offset, ticket_size, offset) ||
        !cia_header_detail::align64(offset, offset) ||
        !cia_header_detail::add(offset, tmd_size, offset) ||
        !cia_header_detail::align64(offset, offset) ||
        !cia_header_detail::add(offset, content_size, offset)) {
        return CiaHeaderStatus::Invalid;
    }
    if (meta_size != 0) {
        if (!cia_header_detail::align64(offset, offset) ||
            !cia_header_detail::add(offset, meta_size, offset)) {
            return CiaHeaderStatus::Invalid;
        }
    }

    output.total_size = offset;
    return CiaHeaderStatus::Valid;
}

inline CiaStreamSizeStatus classifyCiaStreamSize(std::uint64_t expected_size,
                                                 std::uint64_t received_size,
                                                 bool transfer_complete) noexcept {
    if (received_size > expected_size) return CiaStreamSizeStatus::Oversized;
    if (!transfer_complete) return CiaStreamSizeStatus::InProgress;
    return received_size == expected_size ? CiaStreamSizeStatus::Complete :
                                            CiaStreamSizeStatus::Truncated;
}

class CiaStreamAccounting {
public:
    explicit CiaStreamAccounting(std::uint64_t expected_size) noexcept
        : expected_size_(expected_size) {}

    CiaStreamSizeStatus accept(std::uint64_t actual_bytes,
                               bool transfer_complete) noexcept {
        if (actual_bytes > std::numeric_limits<std::uint64_t>::max() - received_size_) {
            received_size_ = std::numeric_limits<std::uint64_t>::max();
            return CiaStreamSizeStatus::Oversized;
        }
        received_size_ += actual_bytes;
        return classifyCiaStreamSize(expected_size_, received_size_, transfer_complete);
    }

    std::uint64_t received() const noexcept { return received_size_; }
    std::uint64_t expected() const noexcept { return expected_size_; }

private:
    std::uint64_t expected_size_ = 0;
    std::uint64_t received_size_ = 0;
};

inline CiaInstallDecision ciaInstallDecision(bool inventory_verified,
                                             bool title_installed,
                                             bool overwrite_confirmed,
                                             bool staged_cia_validated,
                                             bool title_id_matches) noexcept {
    if (!inventory_verified || !staged_cia_validated) {
        return CiaInstallDecision::RefuseUnverified;
    }
    if (!title_id_matches) return CiaInstallDecision::RefuseTitleMismatch;
    if (!title_installed) return CiaInstallDecision::InstallNew;
    return overwrite_confirmed ? CiaInstallDecision::InstallOverwrite :
                                 CiaInstallDecision::RequireOverwriteConfirmation;
}

} // namespace romm3ds
