#pragma once
#include "kachakacha/geometry/Vector3.h"
#include <vector>
class V2Viewport;
void ShowBandApproxPreview(V2Viewport& viewport,
    const std::vector<std::vector<kachakacha::v2::geometry::Vector3>>& rails);
