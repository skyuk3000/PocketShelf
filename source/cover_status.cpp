#include "cover_status.hpp"

#include <cstdio>

namespace romm3ds::ui {

CoverStatus classifyCoverBody(std::string_view bytes) noexcept {
    if (bytes.empty()) return CoverStatus::EmptyBody;
    static constexpr char png[] = "\x89PNG\r\n\x1a\n";
    if (bytes.size() >= sizeof(png) - 1 && bytes.compare(0, sizeof(png) - 1, png) == 0) {
        return CoverStatus::ReadyToDecode;
    }
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xd8 &&
        static_cast<unsigned char>(bytes[2]) == 0xff) {
        return CoverStatus::ReadyToDecode;
    }
    return CoverStatus::UnsupportedBody;
}

std::string coverStatusText(CoverStatus status, unsigned http_status, std::size_t byte_count) {
    char buffer[64];
    switch (status) {
        case CoverStatus::NoPath: return "Cover: no RomM path";
        case CoverStatus::RequestFailed: return "Cover: request failed";
        case CoverStatus::HttpError:
            std::snprintf(buffer, sizeof(buffer), "Cover: HTTP %u", http_status);
            return buffer;
        case CoverStatus::EmptyBody: return "Cover: empty response";
        case CoverStatus::UnsupportedBody:
            std::snprintf(buffer, sizeof(buffer), "Cover: not PNG/JPEG (%lu B)",
                          static_cast<unsigned long>(byte_count));
            return buffer;
        case CoverStatus::DecodeFailed:
            std::snprintf(buffer, sizeof(buffer), "Cover: decode failed (%lu B)",
                          static_cast<unsigned long>(byte_count));
            return buffer;
        case CoverStatus::ReadyToDecode: return "Cover: decoding";
        case CoverStatus::Ready: return "Cover ready";
    }
    return "Cover: unknown error";
}

} // namespace romm3ds::ui
