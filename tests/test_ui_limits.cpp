#include "ui_limits.hpp"

#include <cstdlib>
#include <iostream>

namespace ui = romm3ds::ui;

static_assert(ui::kVisibleRows == 8);
static_assert(ui::kDisplayedTitleGlyphsPerRow == 48);
static_assert(ui::kStatusBufferSize == 192);
static_assert(ui::kMaxRenderObjects == 2048);
static_assert(ui::kMaxRenderObjects >= ui::kWorstCaseRenderObjects);
static_assert(ui::kBottomBodyScale >= 0.40f);
static_assert(ui::kBottomCaptionScale >= 0.35f);
static_assert(ui::kBottomHeadingScale >= 0.50f);
static_assert(ui::kBottomLabelScale >= 0.33f);
static_assert(ui::kBottomStatusScale >= 0.34f);
static_assert(ui::kBottomContentRight <= 320);
static_assert(ui::kBottomContentBottom <= 240);
static_assert(ui::kBottomStatusLines == 1);
static_assert(ui::kBottomRailY + 3 * ui::kBottomRailStride + ui::kBottomRailHeight <= 240);
static_assert(ui::kDetailsMetadataLines >= 11);
static_assert(ui::kDetailsMetadataLineHeight >= 12);
static_assert(ui::kDetailsMetadataColumns > 48);
static_assert(ui::kDetailsMetadataLabelColumns >= 10);
static_assert(ui::kDetailsMetadataValueColumns >= 36);
static_assert(ui::kDetailsMetadataLabelColumns + ui::kDetailsMetadataValueColumns + 1 <=
              ui::kDetailsMetadataColumns);
static_assert(ui::kDetailsMetadataScale >= 0.35f);
static_assert(ui::kDetailsMetadataTextX == ui::kBottomContentX);
static_assert(ui::kDetailsMetadataTextRight >= ui::kBottomContentRight - 12);
// The dead-space fix must not move or resize the existing action rows.
static_assert(ui::kDetailsActionsY == 176);
static_assert(ui::kDetailsActionStride == 29);
static_assert(ui::kDetailsActionHeight == 24);
static_assert(ui::kDetailsMetadataY +
                  ui::kDetailsMetadataLines * ui::kDetailsMetadataLineHeight <=
              ui::kDetailsActionsY);
static_assert(ui::kDetailsActionsY -
                  (ui::kDetailsMetadataY +
                   ui::kDetailsMetadataLines * ui::kDetailsMetadataLineHeight) <= 4);
static_assert(ui::kDetailsActionStride >= ui::kDetailsActionHeight + 3);
static_assert(ui::kDetailsActionStride <= ui::kDetailsActionHeight + 6);
static_assert(ui::kDetailsActionsY + ui::kDetailsActionStride +
                  ui::kDetailsActionHeight <=
              ui::kBottomContentBottom);
static_assert(ui::kBottomContentBottom -
                  (ui::kDetailsActionsY + ui::kDetailsActionStride +
                   ui::kDetailsActionHeight) <= 12);
static_assert(ui::kDetailsActionHeight <= 24);
static_assert(ui::kSettingsUpdateY + ui::kSettingsCardHeight <= ui::kBottomStatusY);
static_assert(ui::kTypeChooserY + 6 * ui::kTypeChooserStride + ui::kTypeChooserHeight <= 216);
static_assert(ui::kSaveRemoteY + ui::kSaveCardHeight <= ui::kSaveStatusY);
static_assert(ui::kSaveSecondaryActionsY + ui::kSaveActionHeight <= 240);
static_assert(ui::kWorstCaseBottomObjects <= ui::kMaxRenderObjects / 2);

int main() {
    if (ui::kMaxRenderObjects < ui::kWorstCaseRenderObjects) {
        std::cerr << "FAIL: Citro2D capacity does not cover worst-case UI demand\n";
        return EXIT_FAILURE;
    }

    std::cout << "UI object budget: worst-case=" << ui::kWorstCaseRenderObjects
              << ", configured=" << ui::kMaxRenderObjects << '\n';
    return EXIT_SUCCESS;
}
