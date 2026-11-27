#pragma once
#include "kachakacha/app/Selection.h"
#include "kachakacha/modeling/SnapEngine.h"

namespace v2ui {
bool SnapMatchesPointTarget(const kachakacha::v2::app::PickCandidate& target,
    const kachakacha::v2::modeling::SnapCandidate& candidate);
void KeepWorkPlanePointSources(std::vector<kachakacha::v2::app::PickCandidate>&,
    const kachakacha::v2::modeling::SnapScene&, const kachakacha::v2::geometry::ScreenMapping&,
    const kachakacha::v2::geometry::ScreenPoint&, const kachakacha::v2::modeling::WorkPlaneFrame&,
    const kachakacha::v2::geometry::GeometryTolerance&);
}
