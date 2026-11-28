#pragma once
#include "kachakacha/app/MeasurePanel.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/modeling/SnapEngine.h"

namespace v2ui {
[[nodiscard]] kachakacha::v2::app::MeasureOverlay BuildSelectionMeasure(
    const kachakacha::v2::app::SelectionSet& selection,
    const kachakacha::v2::document::Document& document,
    const kachakacha::v2::modeling::SnapScene& scene);
}
