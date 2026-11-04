#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <cstddef>
#include <span>
#include <vector>

namespace kachakacha::v2::kernel::detail {
struct NativeDistanceProbe {
    gp_Pnt point;
    bool boundary = false;
};
// Results retain input order. A negative value means the native solver could
// not measure that probe, exactly as in the serial evaluator. Worker geometry
// is private; at most two extra workers are shared by all concurrent callers.
[[nodiscard]] std::vector<double> MeasureFinitePoints(const TopoDS_Shape& shape,
    std::span<const NativeDistanceProbe> points,double precision,std::size_t maxHelpers=2);
}
#endif
