#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace romm3ds::ui {

enum class CoverStatus {
    NoPath,
    RequestFailed,
    HttpError,
    EmptyBody,
    UnsupportedBody,
    DecodeFailed,
    ReadyToDecode,
    Ready,
};

CoverStatus classifyCoverBody(std::string_view bytes) noexcept;
std::string coverStatusText(CoverStatus status, unsigned http_status = 0,
                            std::size_t byte_count = 0);

} // namespace romm3ds::ui
