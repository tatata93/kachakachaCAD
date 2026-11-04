#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/fabrication/FabricationSettings.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include <TopoDS_Shape.hxx>
#include <optional>
namespace kachakacha::v2::kernel::detail {
// Recognize a planar spline by its complete control hull, preserving every
// native boundary on a private calculation copy. Never flatten a curved face.
[[nodiscard]] std::optional<TopoDS_Shape> ThickenPlanarSpline(const TopoDS_Shape& source,
    double thickness,fabrication::ThicknessPlacement placement,const geometry::GeometryTolerance& tolerance);
}
#endif
