#include "kachakacha/modeling/MeshPick.h"

#include <cmath>
#include <limits>

namespace kachakacha::v2::modeling {

using geometry::Cross;
using geometry::Dot;
using geometry::Normalized;

namespace {

//! 光線が三角形と平行とみなすしきい。
constexpr double kParallelEpsilon = 1.0e-12;
//! 目の後ろは当たりにしない。
constexpr double kNearEpsilon = 1.0e-9;

} // namespace

std::optional<double> RayHitsTriangle(const Vector3& origin, const Vector3& direction,
    const MeshTriangle& triangle)
{
    const Vector3 forward = Normalized(direction);
    if (forward == Vector3{}) {
        return std::nullopt;
    }
    const Vector3 first = triangle.points[1] - triangle.points[0];
    const Vector3 second = triangle.points[2] - triangle.points[0];
    const Vector3 across = Cross(forward, second);
    const double determinant = Dot(first, across);
    if (std::abs(determinant) < kParallelEpsilon) {
        return std::nullopt;   // 平行。当たらない(縁をかすめる場合も拾わない)。
    }
    const double inverse = 1.0 / determinant;
    const Vector3 toOrigin = origin - triangle.points[0];
    const double u = Dot(toOrigin, across) * inverse;
    if (u < 0.0 || u > 1.0) {
        return std::nullopt;
    }
    const Vector3 sideways = Cross(toOrigin, first);
    const double v = Dot(forward, sideways) * inverse;
    if (v < 0.0 || u + v > 1.0) {
        return std::nullopt;
    }
    const double distance = Dot(second, sideways) * inverse;
    if (distance < kNearEpsilon) {
        return std::nullopt;   // 目の後ろ。
    }
    return distance;
}

std::optional<MeshHit> PickMesh(const std::vector<ShapeMesh>& shapes, const Vector3& origin,
    const Vector3& direction)
{
    std::optional<MeshHit> best;
    for (std::size_t shapeIndex = 0; shapeIndex < shapes.size(); ++shapeIndex) {
        const ShapeMesh& mesh = shapes[shapeIndex];
        for (std::size_t index = 0; index < mesh.triangles.size(); ++index) {
            const auto distance = RayHitsTriangle(origin, direction, mesh.triangles[index]);
            if (!distance.has_value()) {
                continue;
            }
            // いちばん手前だけを残す。奥のものを返すと手前の部品が掴めなくなる。
            if (best.has_value() && !(*distance < best->distanceMm)) {
                continue;
            }
            MeshHit hit;
            hit.shapeIndex = shapeIndex;
            hit.triangleIndex = index;
            hit.distanceMm = *distance;
            hit.point = origin + Normalized(direction) * (*distance);
            best = hit;
        }
    }
    return best;
}

} // namespace kachakacha::v2::modeling
