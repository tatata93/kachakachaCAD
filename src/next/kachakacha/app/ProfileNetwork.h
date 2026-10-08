#pragma once

#include "kachakacha/app/ProfileRegion.h"

namespace kachakacha::v2::app::detail {
std::vector<modeling::SnapCurve> SplitProfileNetwork(
    const std::vector<modeling::SnapCurve>& curves,
    const geometry::GeometryTolerance& tolerance);
// Temporary exact curve fragments; never edits the source document.
std::vector<ProfileBoundary> ProfileNetworkBoundaries(
    const std::vector<modeling::SnapCurve>& curves,
    const geometry::GeometryTolerance& tolerance);
}
