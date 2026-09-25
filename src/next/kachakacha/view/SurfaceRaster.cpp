#include "kachakacha/view/SurfaceRaster.h"
#include "kachakacha/view/ShapeShading.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace kachakacha::v2::view {
SurfaceRaster::SurfaceRaster(int width, int height)
    : width_(std::max(0, width)), height_(std::max(0, height)),
      pixels_(static_cast<std::size_t>(width_) * height_, 0),
      depths_(pixels_.size(), std::numeric_limits<double>::infinity()) {}

void SurfaceRaster::Draw(const modeling::MeshTriangle& triangle, const geometry::ScreenMapping& mapping,
    const geometry::Vector3& forward, bool closed, std::uint32_t rgb)
{
    if (triangle.normal == geometry::Vector3{} || (closed && BackFacing(triangle, forward))) { return; }
    std::array<Vertex, 3> vertices{};
    const auto& m = mapping.matrix;
    for (std::size_t at = 0; at < 3; ++at) {
        const auto& p = triangle.points[at];
        const auto screen = mapping.Project(p);
        if (!screen.has_value()) { return; }
        const double w = m[12]*p.x + m[13]*p.y + m[14]*p.z + m[15];
        if (std::abs(w) < 1e-12) { return; }
        auto normal = triangle.vertexNormals[at];
        if (normal == geometry::Vector3{}) { normal = triangle.normal; }
        vertices[at] = {screen->x, screen->y, (m[8]*p.x + m[9]*p.y + m[10]*p.z + m[11])/w, 1/w, normal};
        if (!std::isfinite(vertices[at].x + vertices[at].y + vertices[at].z)) { return; }
    }
    Paint(vertices, rgb);
}

void SurfaceRaster::Paint(const std::array<Vertex, 3>& v, std::uint32_t rgb)
{
    if (width_ == 0 || height_ == 0) { return; }
    const double determinant = (v[1].y-v[2].y)*(v[0].x-v[2].x) + (v[2].x-v[1].x)*(v[0].y-v[2].y);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-10) { return; }
    const int x0 = static_cast<int>(std::clamp(std::floor(std::min({v[0].x,v[1].x,v[2].x})), 0.0, double(width_-1)));
    const int x1 = static_cast<int>(std::clamp(std::ceil(std::max({v[0].x,v[1].x,v[2].x})), 0.0, double(width_-1)));
    const int y0 = static_cast<int>(std::clamp(std::floor(std::min({v[0].y,v[1].y,v[2].y})), 0.0, double(height_-1)));
    const int y1 = static_cast<int>(std::clamp(std::ceil(std::max({v[0].y,v[1].y,v[2].y})), 0.0, double(height_-1)));
    const auto light = StandardLightDirection();
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double a = ((v[1].y-v[2].y)*(x+.5-v[2].x)+(v[2].x-v[1].x)*(y+.5-v[2].y))/determinant;
            const double b = ((v[2].y-v[0].y)*(x+.5-v[2].x)+(v[0].x-v[2].x)*(y+.5-v[2].y))/determinant;
            const double c = 1-a-b;
            if (a < -1e-9 || b < -1e-9 || c < -1e-9) { continue; }
            const auto index = static_cast<std::size_t>(y)*width_+x;
            const double depth = a*v[0].z + b*v[1].z + c*v[2].z;
            if (depth >= depths_[index]) { continue; }
            const auto normal = v[0].normal*(a*v[0].inverseW) + v[1].normal*(b*v[1].inverseW) + v[2].normal*(c*v[2].inverseW);
            const double shade = .45 + .55*LambertShade(normal, light);
            const auto channel = [&](int shift) {
                return static_cast<std::uint32_t>(std::clamp(double((rgb >> shift)&255)*shade, 0.0, 255.0)) << shift;
            };
            pixels_[index] = 0xff000000u | channel(16) | channel(8) | channel(0);
            depths_[index] = depth;
        }
    }
}
}
