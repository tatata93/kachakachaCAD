#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctThicken.h"
#include <TopoDS_Shape.hxx>
#include <optional>
namespace kachakacha::v2::kernel::detail {
// Exact intersections of planar facet offsets; curved boundaries keep the general path.
std::optional<base::Result<TopoDS_Shape>> ThickenPlanarSheet(
    const TopoDS_Shape& shape,double thickness,fabrication::ThicknessPlacement placement,
    const geometry::GeometryTolerance& tolerance);
}
#endif
