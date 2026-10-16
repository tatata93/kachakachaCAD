#pragma once
#include "kachakacha/kernel/OcctContact.h"
namespace kachakacha::v2::kernel {
// Native splitter: solids, surfaces and wires retain their dimensionality.
base::Result<ContactResult> SplitNativeRegions(modeling::KernelShapeHandle target,
    modeling::KernelShapeHandle tool,bool boundaryWires,double toleranceMm);
base::Result<modeling::KernelShapeHandle> RegionSplitWire(
    const std::vector<geometry::CurveSegment>& curves,modeling::KernelShapeHandle target,
    geometry::Vector3 cuttingDirection,bool curtain,double toleranceMm);
base::Result<modeling::KernelShapeHandle> RegionSplitPoint(geometry::Vector3 point,
    geometry::Vector3 normal,bool asPlane);
}
