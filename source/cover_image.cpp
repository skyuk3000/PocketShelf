#include "cover_image.hpp"
#include "cover_pixels.hpp"

#include <3ds.h>
#include <jpeglib.h>
#include <png.h>

#include <algorithm>
#include <csetjmp>
#include <cstring>
#include <limits>

namespace romm3ds::ui {
namespace {

struct DecodedImage {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<std::uint8_t> rgba;
};

unsigned textureDimension(unsigned value) {
    unsigned result = 8;
    while (result < value && result < 1024) result <<= 1;
    return result;
}

bool safeDimensions(unsigned width, unsigned height) {
    return width > 0 && height > 0 && width <= kMaxCoverDimension &&
           height <= kMaxCoverDimension &&
           static_cast<std::size_t>(width) <=
               std::numeric_limits<std::size_t>::max() / 4U / height;
}

bool decodePng(const std::vector<std::uint8_t>& encoded, DecodedImage& image) {
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&png, encoded.data(), encoded.size())) return false;
    constexpr std::size_t kMaxPngPixels = 1920U * 1080U;
    if (png.width == 0 || png.height == 0 || png.width > 4096 || png.height > 4096 ||
        static_cast<std::size_t>(png.width) > kMaxPngPixels / png.height) {
        png_image_free(&png);
        return false;
    }
    const unsigned source_width = png.width;
    const unsigned source_height = png.height;
    png.format = PNG_FORMAT_RGBA;
    std::vector<std::uint8_t> source(PNG_IMAGE_SIZE(png));
    const bool ok = png_image_finish_read(&png, nullptr, source.data(), 0, nullptr) != 0;
    png_image_free(&png);
    if (!ok) return false;
    const unsigned step = imageSampleStep(source_width, source_height);
    image.width = (source_width + step - 1) / step;
    image.height = (source_height + step - 1) / step;
    if (!safeDimensions(image.width, image.height)) return false;
    image.rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4U);
    for (unsigned y = 0; y < image.height; ++y) {
        const unsigned source_y = std::min(y * step, source_height - 1);
        for (unsigned x = 0; x < image.width; ++x) {
            const unsigned source_x = std::min(x * step, source_width - 1);
            std::memcpy(image.rgba.data() +
                            (static_cast<std::size_t>(y) * image.width + x) * 4U,
                        source.data() +
                            (static_cast<std::size_t>(source_y) * source_width + source_x) * 4U,
                        4U);
        }
    }
    return true;
}

struct JpegError {
    jpeg_error_mgr base;
    std::jmp_buf jump;
};

void jpegFailure(j_common_ptr common) {
    auto* error = reinterpret_cast<JpegError*>(common->err);
    std::longjmp(error->jump, 1);
}

bool decodeJpeg(const std::vector<std::uint8_t>& encoded, DecodedImage& image) {
    jpeg_decompress_struct decoder{};
    JpegError error{};
    decoder.err = jpeg_std_error(&error.base);
    error.base.error_exit = jpegFailure;
    if (setjmp(error.jump)) {
        jpeg_destroy_decompress(&decoder);
        image.rgba.clear();
        return false;
    }
    jpeg_create_decompress(&decoder);
    jpeg_mem_src(&decoder, encoded.data(), encoded.size());
    if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK || decoder.image_width == 0 ||
        decoder.image_height == 0 || decoder.image_width > 8192 || decoder.image_height > 8192) {
        jpeg_destroy_decompress(&decoder);
        return false;
    }
    decoder.scale_num = 1;
    decoder.scale_denom = jpegScaleDenominator(decoder.image_width, decoder.image_height);
    decoder.out_color_space = JCS_RGB;
    jpeg_start_decompress(&decoder);
    image.width = decoder.output_width;
    image.height = decoder.output_height;
    if (!safeDimensions(image.width, image.height)) {
        jpeg_destroy_decompress(&decoder);
        return false;
    }
    image.rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4U);
    std::vector<std::uint8_t> row(static_cast<std::size_t>(image.width) * 3U);
    while (decoder.output_scanline < decoder.output_height) {
        JSAMPROW pointer = row.data();
        if (jpeg_read_scanlines(&decoder, &pointer, 1) != 1) {
            jpeg_destroy_decompress(&decoder);
            image.rgba.clear();
            return false;
        }
        const std::size_t y = decoder.output_scanline - 1;
        auto* output = image.rgba.data() + y * image.width * 4U;
        for (unsigned x = 0; x < image.width; ++x) {
            output[x * 4U] = row[x * 3U];
            output[x * 4U + 1] = row[x * 3U + 1];
            output[x * 4U + 2] = row[x * 3U + 2];
            output[x * 4U + 3] = 255;
        }
    }
    jpeg_finish_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
    return true;
}

bool decode(const std::vector<std::uint8_t>& encoded, DecodedImage& image) {
    static constexpr std::uint8_t png_signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (encoded.size() >= sizeof(png_signature) &&
        std::memcmp(encoded.data(), png_signature, sizeof(png_signature)) == 0) {
        return decodePng(encoded, image);
    }
    if (encoded.size() >= 3 && encoded[0] == 0xFF && encoded[1] == 0xD8 && encoded[2] == 0xFF) {
        return decodeJpeg(encoded, image);
    }
    return false;
}

} // namespace

CoverTexture::~CoverTexture() { clear(); }

void CoverTexture::clear() {
    if (ready_) C3D_TexDelete(&texture_);
    texture_ = {};
    subtexture_ = {};
    ready_ = false;
}

bool CoverTexture::load(const std::vector<std::uint8_t>& encoded) {
    clear();
    DecodedImage decoded;
    if (encoded.empty() || encoded.size() > kMaxCoverDownloadBytes || !decode(encoded, decoded)) {
        return false;
    }

    const unsigned texture_width = textureDimension(decoded.width);
    const unsigned texture_height = textureDimension(decoded.height);
    const std::size_t texture_bytes =
        static_cast<std::size_t>(texture_width) * texture_height * 4U;
    auto* linear = static_cast<std::uint8_t*>(linearAlloc(texture_bytes));
    if (!linear) return false;
    std::memset(linear, 0, texture_bytes);
    copyRgbaToCtrRgba8(linear, texture_width, decoded.rgba.data(), decoded.width,
                       decoded.height);
    if (!C3D_TexInit(&texture_, texture_width, texture_height, GPU_RGBA8)) {
        linearFree(linear);
        return false;
    }
    const u32 flags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) |
                      GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                      GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                      GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    GSPGPU_FlushDataCache(linear, texture_bytes);
    C3D_SyncDisplayTransfer(reinterpret_cast<u32*>(linear),
                            GX_BUFFER_DIM(texture_width, texture_height),
                            reinterpret_cast<u32*>(texture_.data),
                            GX_BUFFER_DIM(texture_width, texture_height), flags);
    linearFree(linear);
    C3D_TexSetFilter(&texture_, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&texture_, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    subtexture_.width = static_cast<u16>(decoded.width);
    subtexture_.height = static_cast<u16>(decoded.height);
    subtexture_.left = 0.0f;
    subtexture_.right = static_cast<float>(decoded.width) / texture_width;
    subtexture_.top = 1.0f;
    subtexture_.bottom = 1.0f - static_cast<float>(decoded.height) / texture_height;
    ready_ = true;
    return true;
}

float CoverTexture::displayScale(float max_width, float max_height) const noexcept {
    if (!ready_ || subtexture_.width == 0 || subtexture_.height == 0) return 0.0f;
    return std::min(max_width / subtexture_.width, max_height / subtexture_.height);
}

} // namespace romm3ds::ui
