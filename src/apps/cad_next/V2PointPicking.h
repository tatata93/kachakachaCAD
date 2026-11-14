#pragma once
#include "kachakacha/app/DrawingSession.h"
#include "kachakacha/app/Selection.h"
#include <QPointF>

class V2Viewport;
namespace v2ui {
// Reference inputs share the viewport's snap rules, without a drawing-plane limit.
// Direction fitting can ignore the old lattice while retaining geometry snaps.
kachakacha::v2::app::HoverResult ReferencePointAt(const V2Viewport& view,
    const kachakacha::v2::modeling::SnapScene& scene,
    const kachakacha::v2::geometry::GeometryTolerance& tolerance,
    const QPointF& position, bool suppressed, bool snapToGrid,
    std::optional<kachakacha::v2::app::PickCandidate>* curve = nullptr);
}
