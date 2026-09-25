#pragma once
#include "kachakacha/modeling/ShapeMesh.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include <cstdint>

namespace kachakacha::v2::view {
//! 表示専用。連続法線の陰影と画素単位の深度で三角形の境目・裏面の透過を防ぐ。
class SurfaceRaster {
public:
    SurfaceRaster(int width, int height);
    void Draw(const modeling::MeshTriangle& triangle, const geometry::ScreenMapping& mapping,
        const geometry::Vector3& forward, bool closed, std::uint32_t rgb);
    [[nodiscard]] const std::vector<std::uint32_t>& Pixels() const { return pixels_; }
private:
    struct Vertex {
        double x, y, z, inverseW;
        geometry::Vector3 normal;
    };
    void Paint(const std::array<Vertex, 3>& vertices, std::uint32_t rgb);
    int width_, height_;
    std::vector<std::uint32_t> pixels_;
    std::vector<double> depths_;
};
}
