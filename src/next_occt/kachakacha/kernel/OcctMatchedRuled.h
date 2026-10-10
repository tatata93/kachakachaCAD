#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include <TopoDS_Shape.hxx>
#include <optional>
namespace kachakacha::v2::kernel::detail {
[[nodiscard]] std::optional<base::Result<TopoDS_Shape>> BuildMatchedRuled(
    const modeling::GuideSurfaceRequest& request, const std::vector<std::size_t>& order,
    const geometry::GeometryTolerance& tolerance);
}
#endif
