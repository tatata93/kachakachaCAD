#include "V2Viewport.h"
#include "kachakacha/view/SurfaceRaster.h"
#include <QPainter>
#include <QImage>
#include <QRect>
#include <algorithm>

void V2Viewport::SetToolPreviewFaces(
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> faces,
    const kachakacha::v2::modeling::ShapeMesh* smoothMesh)
{
    using namespace kachakacha::v2;
    toolPreviewFaces_ = std::move(faces);
    toolPreviewMesh_ = smoothMesh ? *smoothMesh : modeling::ShapeMesh{};
    if (!smoothMesh) {
        for (const auto& face : toolPreviewFaces_) {
            for (std::size_t at = 1; at + 1 < face.size(); ++at) {
                modeling::MeshTriangle triangle;
                triangle.points = {face[0], face[at], face[at+1]};
                toolPreviewMesh_.triangles.push_back(triangle);
            }
        }
        modeling::RefreshNormals(toolPreviewMesh_);
    }
    update();
}

void V2Viewport::DrawToolPreviewFaces(QPainter& painter) const
{
    if (toolPreviewMesh_.triangles.empty()) { return; }
    using namespace kachakacha::v2;
    view::SurfaceRaster raster(width(), height());
    for (const auto& triangle : toolPreviewMesh_.triangles) {
        raster.Draw(triangle, mapping_, view::ForwardOf(orientation_), false, 0x91ced6);
    }
    QImage image(width(), height(), QImage::Format_ARGB32);
    if (image.isNull()) { return; }
    for (int y = 0; y < height(); ++y) {
        const auto start = raster.Pixels().begin() + static_cast<std::size_t>(y)*width();
        std::copy_n(start, width(), reinterpret_cast<std::uint32_t*>(image.scanLine(y)));
    }
    painter.drawImage(QRect(0, 0, width(), height()), image);
}
