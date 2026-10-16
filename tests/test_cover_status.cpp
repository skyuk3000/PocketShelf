#include "cover_status.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using romm3ds::ui::CoverStatus;
    using romm3ds::ui::classifyCoverBody;
    using romm3ds::ui::coverStatusText;

    require(classifyCoverBody({}) == CoverStatus::EmptyBody,
            "empty successful response is distinct");
    const char png[] = "\x89PNG\r\n\x1a\nrest";
    require(classifyCoverBody(std::string_view(png, sizeof(png) - 1)) == CoverStatus::ReadyToDecode,
            "PNG signature reaches decoder");
    const char jpeg[] = "\xff\xd8\xff\xe0rest";
    require(classifyCoverBody(std::string_view(jpeg, sizeof(jpeg) - 1)) == CoverStatus::ReadyToDecode,
            "JPEG signature reaches decoder");
    require(classifyCoverBody("<html>error</html>") == CoverStatus::UnsupportedBody,
            "non-image success body is diagnosed before decode");
    require(std::string_view(coverStatusText(CoverStatus::NoPath, 0, 0)) == "Cover: no RomM path",
            "missing path diagnostic");
    require(std::string_view(coverStatusText(CoverStatus::HttpError, 403, 0)) == "Cover: HTTP 403",
            "HTTP status diagnostic");
    require(std::string_view(coverStatusText(CoverStatus::DecodeFailed, 0, 1234)) ==
                "Cover: decode failed (1234 B)",
            "decode diagnostic includes bounded byte count");

    std::cout << "All cover status tests passed\n";
    return 0;
}
