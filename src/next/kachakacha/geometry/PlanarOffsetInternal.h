#pragma once
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include <optional>
#include <vector>

// Private implementation shared only by the planar offset translation units.
namespace kachakacha::v2::geometry::detail {
struct OffsetStatistics { double bound = 0, squares = 0; std::size_t samples = 0; };
[[nodiscard]] base::Diagnostic OffsetApproximationDiagnostic(const OffsetStatistics&, const GeometryTolerance&);
[[nodiscard]] base::Result<CurveSegment> OffsetFreeCurve(const CurveSegment&, Vector3 normal,
    double distance, const GeometryTolerance&, double first = 0, double last = 1,
    OffsetStatistics* statistics = nullptr);
[[nodiscard]] base::Result<std::vector<CurveSegment>> OffsetCurvedContour(
    const std::vector<CurveSegment>& ordered, Vector3 normal, double distance,
    const GeometryTolerance&);
[[nodiscard]] std::optional<Vector3> PrimitiveOffsetJoint(const CurveSegment&, const CurveSegment&,
    Vector3 reference, Vector3 normal, const GeometryTolerance&);
[[nodiscard]] base::Result<CurveSegment> TrimOffsetPrimitive(const CurveSegment&,
    Vector3 start, Vector3 end, const GeometryTolerance&);
}
