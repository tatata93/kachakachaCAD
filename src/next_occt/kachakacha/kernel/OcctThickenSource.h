#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include <TopoDS_Shape.hxx>
#include <optional>
namespace kachakacha::v2::kernel::detail {
// An exact private topology copy of a proved complete rectangular trim.
// Holes, notches, overlaps and periodic supports are left untouched.
[[nodiscard]] std::optional<TopoDS_Shape> CanonicalRectangularThickenFace(
    const TopoDS_Shape& source, double tolerance);
// Necessary preservation check in addition to closedness/volume/intersections.
// A valid Boolean result can still have silently dropped a complete region.
[[nodiscard]] bool ThickeningContainsSourceBounds(const TopoDS_Shape& source,
    const TopoDS_Shape& wall, double tolerance);
}
#endif
