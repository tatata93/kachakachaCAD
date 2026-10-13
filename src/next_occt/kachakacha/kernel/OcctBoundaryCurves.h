#pragma once
#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include <utility>
#include <vector>
#ifdef KACHACAD_V2_WITH_OCCT
#include <TopoDS_Edge.hxx>
namespace kachakacha::v2::kernel::detail {
struct BoundedBoundaryCurves {
    std::vector<geometry::CurveSegment> curves;
    std::vector<std::pair<double,double>> parameters;
    double maxDeviationBoundMm=0, squaredDeviationMm=0;
    std::size_t samples=0;
};
base::Result<BoundedBoundaryCurves> BoundaryCurves(const TopoDS_Edge& edge,double tolerance);
}
#endif
