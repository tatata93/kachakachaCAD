#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/fabrication/FabricationSettings.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include <TopoDS_Shape.hxx>
#include <optional>
namespace kachakacha::v2::kernel::detail {
// Exact subdivision of a private source before offsetting. Failure leaves the
// ordinary thickening path and its diagnostics unchanged.
[[nodiscard]] std::optional<TopoDS_Shape> ThickenSmoothSpans(const TopoDS_Shape& source,
    double thickness, fabrication::ThicknessPlacement placement,
    const geometry::GeometryTolerance& tolerance, bool individualOnly = false);
}
#endif
