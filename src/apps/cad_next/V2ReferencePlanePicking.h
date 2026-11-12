#pragma once
#include "V2Viewport.h"

namespace v2ui {
// Explicit reference inputs can pick a displayed plane at its origin or border.
// Never add a plane-sized invisible hit target to ordinary drawing/selection.
void AppendReferencePlaneCandidates(const std::vector<V2Viewport::WorkPlaneView>& planes,
    const kachakacha::v2::geometry::ScreenMapping& mapping, double visibleWidthMm,
    double tolerancePx, const QPointF& pointer,
    std::vector<kachakacha::v2::app::PickCandidate>& candidates);
}
