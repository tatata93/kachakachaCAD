#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <TopoDS_Face.hxx>
#include <optional>

namespace kachakacha::v2::kernel::detail {
// Private exact preprocessing. Only an unused part of a nonperiodic B-spline
// support is removed; no fitting or relaxation of real boundary errors occurs.
std::optional<TopoDS_Face> RestrictOffsetSupport(
    const TopoDS_Face& face, const geometry::GeometryTolerance& tolerance);
}
#endif
