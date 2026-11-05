#pragma once

#include "kachakacha/app/ProfileRegion.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include <QPointF>
#include <vector>

namespace v2ui {
// Match only the selected region whose boundary is actually under the pointer.
// Several cells may share a Wire UUID; their seed references remain distinct.
void MatchSeededBoundaryReference(kachakacha::v2::app::PickCandidate& candidate,
    const kachakacha::v2::app::SelectionSet& selection,
    const std::vector<kachakacha::v2::app::ProfileRegion>& regions,
    const kachakacha::v2::geometry::ScreenMapping& mapping,
    const QPointF& position, double edgePickPx);
}
