#include "cover_pixels.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using romm3ds::ui::copyRgbaToCtrRgba8;

    constexpr std::array<std::uint8_t, 16> rgba = {
        0x11, 0x22, 0x33, 0x44,
        0xA1, 0xB2, 0xC3, 0xD4,
        0x10, 0x20, 0x30, 0x40,
        0xA0, 0xB0, 0xC0, 0xD0,
    };
    std::array<std::uint8_t, 24> texture{};
    texture.fill(0xEE);

    copyRgbaToCtrRgba8(texture.data(), 3, rgba.data(), 2, 2);

    constexpr std::array<std::uint8_t, 24> expected = {
        0x44, 0x33, 0x22, 0x11,
        0xD4, 0xC3, 0xB2, 0xA1,
        0xEE, 0xEE, 0xEE, 0xEE,
        0x40, 0x30, 0x20, 0x10,
        0xD0, 0xC0, 0xB0, 0xA0,
        0xEE, 0xEE, 0xEE, 0xEE,
    };
    require(texture == expected,
            "decoded RGBA is packed as Citro3D RGBA8 (ABGR bytes) without touching padding");

    require(romm3ds::ui::jpegScaleDenominator(400, 480) == 1,
            "small JPEGs decode at native size");
    require(romm3ds::ui::jpegScaleDenominator(1280, 720) == 4,
            "large screenshots use bounded JPEG DCT scaling");
    require(romm3ds::ui::jpegScaleDenominator(4096, 2160) == 8,
            "largest supported DCT reduction is selected");
    require(romm3ds::ui::imageSampleStep(1280, 720) == 3,
            "large PNG screenshots receive bounded nearest-neighbour sampling");
    require(romm3ds::ui::imageSampleStep(512, 512) == 1,
            "already bounded images are not resampled");

    std::cout << "All cover pixel tests passed\n";
    return 0;
}
