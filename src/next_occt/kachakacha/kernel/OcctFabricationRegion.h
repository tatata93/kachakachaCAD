#pragma once
#include "kachakacha/kernel/OcctFaceQuery.h"
#include "kachakacha/geometry/CurveSampling.h"
namespace kachakacha::v2::kernel {
struct FabricationFaceRegion {
    fabrication::SurfacePatchSamples samples;
    fabrication::SurfaceEvaluator evaluate;
    // Normalized (column,row) UV; first outer, remaining holes. No OCCT types.
    std::vector<std::vector<geometry::Point2>> loops;
    double boundarySamplingDeviationMm=0;
};
[[nodiscard]] base::Result<FabricationFaceRegion> FabricationRegionOf(
    modeling::KernelShapeHandle handle,std::size_t faceIndex,double boundaryToleranceMm);
}
