#include "kachakacha/modeling/MeshPick.h"

#include <algorithm>
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

std::vector<MeshHit> CollectMeshHits(const std::vector<ShapeMesh>& shapes,
    const Vector3& origin, const Vector3& direction)
{
    std::vector<MeshHit> hits;
    for (std::size_t shapeIndex = 0; shapeIndex < shapes.size(); ++shapeIndex) {
        const ShapeMesh& mesh = shapes[shapeIndex];
        std::optional<MeshHit> nearest;
        for (std::size_t index = 0; index < mesh.triangles.size(); ++index) {
            const auto distance = RayHitsTriangle(origin, direction, mesh.triangles[index]);
            if (!distance.has_value()) {
                continue;
            }
            if (nearest.has_value() && !(*distance < nearest->distanceMm)) {
                continue;
            }
            MeshHit hit;
            hit.shapeIndex = shapeIndex;
            hit.triangleIndex = index;
            hit.distanceMm = *distance;
            hit.point = origin + Normalized(direction) * (*distance);
            nearest = hit;
        }
        if (nearest.has_value()) {
            hits.push_back(*nearest);
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const MeshHit& first,
                                                  const MeshHit& second) {
        return first.distanceMm < second.distanceMm;
    });
    return hits;
}

std::optional<MeshHit> PickMesh(const std::vector<ShapeMesh>& shapes, const Vector3& origin,
    const Vector3& direction)
{
    const auto hits = CollectMeshHits(shapes, origin, direction);
    return hits.empty() ? std::nullopt : std::optional<MeshHit>{hits.front()};
}

} // namespace kachakacha::v2::modeling
