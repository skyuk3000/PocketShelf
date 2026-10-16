#pragma once

#include <citro2d.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace romm3ds::ui {

inline constexpr std::size_t kMaxCoverDownloadBytes = 512 * 1024;
inline constexpr unsigned kMaxCoverDimension = 512;

class CoverTexture {
public:
    CoverTexture() = default;
    CoverTexture(const CoverTexture&) = delete;
    CoverTexture& operator=(const CoverTexture&) = delete;
    ~CoverTexture();

    bool load(const std::vector<std::uint8_t>& encoded);
    void clear();
    bool ready() const noexcept { return ready_; }
    C2D_Image image() const noexcept { return C2D_Image{const_cast<C3D_Tex*>(&texture_), &subtexture_}; }
    float displayScale(float max_width, float max_height) const noexcept;

private:
    C3D_Tex texture_{};
    Tex3DS_SubTexture subtexture_{};
    bool ready_ = false;
};

} // namespace romm3ds::ui
