#pragma once

#include <cstddef>

namespace romm3ds::ui {

inline constexpr std::size_t kVisibleRows = 8;
inline constexpr std::size_t kDisplayedTitleGlyphsPerRow = 48;
inline constexpr std::size_t kRowPrefixGlyphs = 2;

inline constexpr std::size_t kStatusBufferSize = 192;
inline constexpr std::size_t kCountBufferSize = 80;
inline constexpr std::size_t kHeaderGlyphs = 11;
inline constexpr std::size_t kFooterGlyphs = 60;
inline constexpr std::size_t kSettingsUrlGlyphs = 64;
inline constexpr std::size_t kSettingsCopyGlyphs = 120;
inline constexpr std::size_t kBottomNavigationGlyphs = 40;

// Bottom-screen typography is intentionally larger than the top-screen list.
// These values were chosen for the physical 3DS display, not a desktop capture.
inline constexpr float kBottomHeadingScale = 0.50f;
inline constexpr float kBottomBodyScale = 0.40f;
inline constexpr float kBottomCaptionScale = 0.35f;
inline constexpr float kBottomLabelScale = 0.33f;
inline constexpr float kBottomStatusScale = 0.34f;
inline constexpr std::size_t kBottomStatusLines = 1;
inline constexpr int kBottomLineHeight = 15;
inline constexpr int kBottomContentRight = 312;
inline constexpr int kBottomContentBottom = 238;
inline constexpr int kBottomContentX = 60;
inline constexpr int kBottomContentWidth = 252;
inline constexpr int kBottomStatusY = 229;

inline constexpr int kBottomRailX = 5;
inline constexpr int kBottomRailY = 4;
inline constexpr int kBottomRailWidth = 48;
inline constexpr int kBottomRailHeight = 54;
inline constexpr int kBottomRailStride = 59;

inline constexpr int kDetailsMetadataY = 42;
inline constexpr int kDetailsMetadataLines = 11;
inline constexpr int kDetailsMetadataLineHeight = 12;
inline constexpr int kDetailsMetadataTextX = kBottomContentX;
inline constexpr int kDetailsMetadataTextRight = 304;
inline constexpr int kDetailsMetadataScrollX = 309;
inline constexpr std::size_t kDetailsMetadataColumns = 52;
inline constexpr std::size_t kDetailsMetadataLabelColumns = 12;
inline constexpr std::size_t kDetailsMetadataValueColumns = 39;
inline constexpr int kDetailsMetadataValueX = 126;
inline constexpr float kDetailsMetadataScale = 0.35f;
inline constexpr int kDetailsActionsY = 176;
inline constexpr int kDetailsActionStride = 29;
inline constexpr int kDetailsActionHeight = 24;
inline constexpr int kDetailsActionGap = 8;
inline constexpr int kDetailsActionWidth = 122;
inline constexpr int kScreenshotsActionsY = 164;
inline constexpr int kScreenshotsBackY = 204;
inline constexpr int kManageRowY = 36;
inline constexpr int kManageRowStride = 24;
inline constexpr int kManageRowHeight = 22;
inline constexpr int kManageBackY = 207;
inline constexpr int kFilterTypeY = 42;
inline constexpr int kFilterSearchY = 98;
inline constexpr int kFilterMetadataY = 154;
inline constexpr int kFilterCardHeight = 48;
inline constexpr int kSortCardY = 62;
inline constexpr int kSortCardHeight = 64;
inline constexpr int kSettingsUrlY = 29;
inline constexpr int kSettingsTokenY = 66;
inline constexpr int kSettingsThemeY = 103;
inline constexpr int kSettingsConnectY = 140;
inline constexpr int kSettingsUpdateY = 177;
inline constexpr int kSettingsCardHeight = 35;
inline constexpr int kTypeChooserY = 36;
inline constexpr int kTypeChooserStride = 25;
inline constexpr int kTypeChooserHeight = 22;
inline constexpr int kSaveLocalY = 59;
inline constexpr int kSaveRemoteY = 108;
inline constexpr int kSaveCardHeight = 43;
inline constexpr int kSaveStatusY = 157;
inline constexpr int kSavePrimaryActionsY = 171;
inline constexpr int kSaveSecondaryActionsY = 205;
inline constexpr int kSaveActionHeight = 30;

inline constexpr std::size_t kMaxRectangleObjects = 32;
inline constexpr std::size_t kMaxImageObjects = 1;

// Includes the navigation rail, the densest modal/save screen, and shapes.
// Page layouts are mutually exclusive, so budget the largest one rather than
// summing every bottom page as if all were visible together.
inline constexpr std::size_t kWorstCaseBottomGlyphs = 720;
inline constexpr std::size_t kWorstCaseBottomObjects =
    kWorstCaseBottomGlyphs + kMaxRectangleObjects;

inline constexpr std::size_t kWorstCaseRenderObjects =
    kVisibleRows * (kDisplayedTitleGlyphsPerRow + kRowPrefixGlyphs) +
    (kStatusBufferSize - 1) + (kCountBufferSize - 1) + kHeaderGlyphs +
    kFooterGlyphs + kSettingsUrlGlyphs + kSettingsCopyGlyphs +
    kWorstCaseBottomObjects + kMaxImageObjects;

// C2D_Init's argument is a per-frame object capacity. Each text glyph consumes
// an object, so leave substantial headroom over the bounded worst-case UI frame.
inline constexpr std::size_t kMaxRenderObjects = 2048;

static_assert(kMaxRenderObjects >= kWorstCaseRenderObjects,
              "Citro2D object capacity must cover the worst-case UI frame");

} // namespace romm3ds::ui
