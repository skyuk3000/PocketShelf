#include "cover_pixels.hpp"

#include <initializer_list>

namespace romm3ds::ui {

unsigned jpegScaleDenominator(unsigned width, unsigned height) noexcept {
    for (const unsigned denominator : {1U, 2U, 4U, 8U}) {
        const unsigned scaled_width = (width + denominator - 1) / denominator;
        const unsigned scaled_height = (height + denominator - 1) / denominator;
        if (scaled_width <= 512 && scaled_height <= 512) return denominator;
    }
    return 8;
}

unsigned imageSampleStep(unsigned width, unsigned height) noexcept {
    const unsigned largest = width > height ? width : height;
    return largest <= 512 ? 1 : (largest + 511) / 512;
}

void copyRgbaToCtrRgba8(std::uint8_t* destination, std::size_t destination_width,
                        const std::uint8_t* rgba, std::size_t width,
                        std::size_t height) noexcept {
    for (std::size_t y = 0; y < height; ++y) {
        auto* output = destination + y * destination_width * 4U;
        const auto* input = rgba + y * width * 4U;
        for (std::size_t x = 0; x < width; ++x) {
            output[x * 4U] = input[x * 4U + 3U];
            output[x * 4U + 1U] = input[x * 4U + 2U];
            output[x * 4U + 2U] = input[x * 4U + 1U];
            output[x * 4U + 3U] = input[x * 4U];
        }
    }
}

} // namespace romm3ds::ui
