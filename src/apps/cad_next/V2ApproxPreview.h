#pragma once
#include "kachakacha/geometry/Vector3.h"
#include <vector>
#include "kachakacha/app/FabricationEvaluate.h"
class V2Viewport;
bool ShowBandApproxPreview(V2Viewport& viewport,
    const std::vector<std::vector<kachakacha::v2::geometry::Vector3>>& rails,
    const kachakacha::v2::app::FabricationEvaluation& evaluation,std::string& error);
