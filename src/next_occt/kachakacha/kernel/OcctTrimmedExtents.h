#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include <optional>
class TopoDS_Shape;

namespace kachakacha::v2::kernel::detail {
// Lower X/Y/Z (0..2), upper X/Y/Z (3..5), measured on trimmed native geometry.
// A UV support rectangle can extend far beyond the material after a cut.
inline constexpr double kTrimmedExtentPrecisionMm=1e-7;
[[nodiscard]] std::optional<double> TrimmedCoordinateExtent(
    const TopoDS_Shape& shape,int component);
}
#endif
