#pragma once
#include "kachakacha/modeling/ShapeMesh.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include <cstdint>
#include "kachakacha/modeling/ImagePlacement.h"

namespace kachakacha::v2::view {
struct RasterImage { int width=0,height=0; std::vector<std::uint32_t> pixels; double opacity=1; };
//! 表示専用。連続法線の陰影と画素単位の深度で三角形の境目・裏面の透過を防ぐ。
class SurfaceRaster {
public:
    SurfaceRaster(int width, int height);
    void Draw(const modeling::MeshTriangle& triangle, const geometry::ScreenMapping& mapping,
        const geometry::Vector3& forward, bool closed, std::uint32_t rgb);
    void DrawImage(const modeling::ImageTriangle&, const geometry::ScreenMapping&, const RasterImage&);
    //! 面の奥にある稜線は描かない。表示用ポリラインのみを処理する。
    void DrawEdge(const std::vector<geometry::Vector3>& points,
        const geometry::ScreenMapping& mapping, std::uint32_t rgb, int radius = 0);
    [[nodiscard]] const std::vector<std::uint32_t>& Pixels() const { return pixels_; }
private:
    struct Vertex {
        double x, y, z, inverseW;
        geometry::Vector3 normal;
        geometry::Vector3 pixel;
    };
    void Paint(const std::array<Vertex, 3>& vertices, std::uint32_t rgb);
    void PaintImage(const std::array<Vertex,3>&, const RasterImage&);
    int width_, height_;
    std::vector<std::uint32_t> pixels_;
    std::vector<double> depths_;
};
}
