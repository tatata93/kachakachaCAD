#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include <TopoDS_Shape.hxx>
#include <optional>
namespace kachakacha::v2::kernel::detail {
std::optional<base::Result<TopoDS_Shape>> BuildMatchedLoft(
    const modeling::GuideSurfaceRequest& request,const std::vector<std::size_t>& order,
    const geometry::GeometryTolerance& tolerance);
}
#endif
