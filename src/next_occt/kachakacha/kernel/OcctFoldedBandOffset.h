#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <TopoDS_Shape.hxx>
#include <optional>
namespace kachakacha::v2::kernel::detail {
// Private kernel fallback for a single, proven piecewise-planar ruled band.
std::optional<base::Result<TopoDS_Shape>> OffsetFoldedBand(
    const TopoDS_Shape& source,double distance,const geometry::GeometryTolerance& tolerance);
}
#endif
