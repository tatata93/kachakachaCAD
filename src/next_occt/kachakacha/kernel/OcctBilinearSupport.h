#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include <TopoDS_Shape.hxx>
#include <optional>
namespace kachakacha::v2::kernel::detail {
// A calculation-only copy, with independently bounded position/offset error.
[[nodiscard]] std::optional<TopoDS_Shape> BilinearSupportCopy(const TopoDS_Shape& source,double thickness,double tolerance);
}
#endif
