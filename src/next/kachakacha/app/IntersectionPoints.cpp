#include "kachakacha/app/IntersectionPoints.h"

#include "kachakacha/geometry/WireEdit.h"

#include <algorithm>
#include <string>

namespace kachakacha::v2::app {

using base::MakeError;
using base::Result;
using geometry::Vector3;

namespace {

constexpr const char* kNoIntersection = "UI-X101";

[[nodiscard]] bool Near(const Vector3& a, const Vector3& b, double toleranceMm)
{
    return (a - b).Length() <= toleranceMm;
}

} // namespace

Result<std::vector<Vector3>> IntersectionPointsOf(
    const std::vector<geometry::CurveSegment>& curves, double toleranceMm)
{
    using Out = Result<std::vector<Vector3>>;
    if (curves.size() < 2) {
        return Out::Failure(MakeError(kNoIntersection, "交点がありません。",
            "線を2本以上選んでください。"));
    }
    std::vector<Vector3> points;
    for (std::size_t a = 0; a < curves.size(); ++a) {
        for (std::size_t b = a + 1; b < curves.size(); ++b) {
            for (const auto& hit : geometry::IntersectCurvesForEditing(curves[a], curves[b],
                     toleranceMm)) {
                const bool seen = std::any_of(points.begin(), points.end(),
                    [&](const Vector3& point) { return Near(point, hit.point, toleranceMm); });
                if (!seen) {
                    points.push_back(hit.point);
                }
            }
        }
    }
    if (points.empty()) {
        return Out::Failure(MakeError(kNoIntersection, "交点がありません。",
            "選んだ " + std::to_string(curves.size()) + " 本は互いに交わっていません。"));
    }
    // 並びは選んだ線の順 → 交点の順。同じ入力なら同じ並びで出る。
    return Out::Success(std::move(points));
}

} // namespace kachakacha::v2::app
