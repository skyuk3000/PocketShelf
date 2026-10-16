#pragma once

#include <cstddef>
#include <cstdint>

namespace romm3ds::ui {

// Citro3D GPU_RGBA8 texels are stored in CPU memory as A, B, G, R bytes.
void copyRgbaToCtrRgba8(std::uint8_t* destination, std::size_t destination_width,
                        const std::uint8_t* rgba, std::size_t width,
                        std::size_t height) noexcept;

unsigned jpegScaleDenominator(unsigned width, unsigned height) noexcept;
unsigned imageSampleStep(unsigned width, unsigned height) noexcept;

} // namespace romm3ds::ui
