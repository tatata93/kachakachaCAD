#pragma once
// Private native construction shared only by the directional extrusion layer.
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctExtrude.h"
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <optional>
namespace kachakacha::v2::kernel::detail {
// nullopt leaves non-planar stops and multi-face sources on the existing route.
[[nodiscard]] std::optional<base::Result<ExtrudeBuildResult>> TryPlanarExtrudeClosure(
    const TopoDS_Shape& source,const TopoDS_Face& target,const geometry::Vector3& direction,
    const geometry::GeometryTolerance& tolerance);
}
#endif
