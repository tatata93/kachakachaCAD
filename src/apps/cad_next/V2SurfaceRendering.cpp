#include "V2Viewport.h"
#include "kachakacha/view/SurfaceRaster.h"
#include <QImage>
#include <QPainter>
#include <QRect>
#include <algorithm>

void V2Viewport::DrawSmoothShapes(QPainter& painter) const
{
    using namespace kachakacha::v2;
    view::SurfaceRaster raster(width(), height());
    const auto forward = view::ForwardOf(orientation_);
    for (const auto& shape : shapeViews_) {
        const auto* analysis = AnalysisFor(shape.entityId);
        if (analysis != nullptr && analysis->Painted() && app::AnalysisPaintsSurface(analysis->mode)) { continue; }
        const bool selected = !shape.entityId.IsNil() && app::IsSelected(selection_, shape.entityId);
        const std::uint32_t color = shape.surface ? (selected ? 0xb0d2e8 : 0x91bed9)
            : (selected ? 0xc8d4df : 0xb2bcc7);
        for (const auto& triangle : shape.mesh.triangles) {
            raster.Draw(triangle, mapping_, forward, shape.mesh.closed, color);
        }
    }
    QImage image(width(), height(), QImage::Format_ARGB32);
    if (image.isNull()) { return; }
    for (int y = 0; y < height(); ++y) {
        const auto start = raster.Pixels().begin() + static_cast<std::size_t>(y)*width();
        std::copy_n(start, width(), reinterpret_cast<std::uint32_t*>(image.scanLine(y)));
    }
    painter.drawImage(QRect(0, 0, width(), height()), image);
}
