#include "kachakacha/modeling/ShapeMesh.h"

#include <algorithm>
#include <limits>

namespace kachakacha::v2::modeling {

void RefreshBounds(ShapeMesh& mesh)
{
    constexpr double kBig = std::numeric_limits<double>::max();
    mesh.minimum = Vector3{kBig, kBig, kBig};
    mesh.maximum = Vector3{-kBig, -kBig, -kBig};
    const auto take = [&](const Vector3& point) {
        mesh.minimum = Vector3{std::min(mesh.minimum.x, point.x),
            std::min(mesh.minimum.y, point.y), std::min(mesh.minimum.z, point.z)};
        mesh.maximum = Vector3{std::max(mesh.maximum.x, point.x),
            std::max(mesh.maximum.y, point.y), std::max(mesh.maximum.z, point.z)};
    };
    for (const MeshTriangle& triangle : mesh.triangles) {
        for (const Vector3& point : triangle.points) {
            take(point);
        }
    }
    for (const auto& edge : mesh.edges) {
        for (const Vector3& point : edge) {
            take(point);
        }
    }
}

void RefreshNormals(ShapeMesh& mesh)
{
    for (MeshTriangle& triangle : mesh.triangles) {
        const Vector3 first = triangle.points[1] - triangle.points[0];
        const Vector3 second = triangle.points[2] - triangle.points[0];
        // 長さ0なら潰れた三角形。勝手に上向きへ丸めない(描かない印になる)。
        triangle.normal = geometry::Normalized(geometry::Cross(first, second));
    }
}

} // namespace kachakacha::v2::modeling
