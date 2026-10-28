#pragma once
#include "kachakacha/fabrication/BandApproximation.h"

namespace kachakacha::v2::fabrication {
void CompactMaterialLoop(std::vector<geometry::Point2>& loop);
// Pure parameter-space material, outer boundary first and holes thereafter.
// The support rectangle is only a chart, never the material outline.
struct BandMaterialRegion {
    std::vector<std::vector<Point2>> loops;
};
[[nodiscard]] base::Result<std::vector<BandMaterialRegion>> ClipBandMaterial(
    const std::vector<std::vector<Point2>>& loops, BandSplitAxis axis, double low, double high);
// Split paths at the strip's triangle boundaries before mapping. This keeps
// cut lengths identical in the developed and any rigidly folded state.
[[nodiscard]] std::vector<Vector3> MapBandMaterialLoop(const std::vector<Point2>& loop,
    BandSplitAxis axis, double low, double high, const std::vector<double>& longitudinal,
    const std::vector<std::vector<Vector3>>& twoRails);
}
