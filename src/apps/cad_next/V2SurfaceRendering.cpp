#include "V2Viewport.h"
#include "kachakacha/view/SurfaceRaster.h"
#include <QImage>
#include <QColor>
#include <QPainter>
#include <QRect>
#include <algorithm>

void V2Viewport::DrawSmoothShapes(QPainter& painter) const
{
    using namespace kachakacha::v2;
    view::SurfaceRaster raster(width(), height());
    const auto forward = view::ForwardOf(orientation_);
    if (display_.shapesVisible) for (const auto& shape : shapeViews_) {
        if(!EntityShown(shape.entityId))continue;
        const auto* analysis = AnalysisFor(shape.entityId);
        if (analysis != nullptr && analysis->Painted() && app::AnalysisPaintsSurface(analysis->mode)) { continue; }
        const bool selected = !shape.entityId.IsNil() && app::IsSelected(selection_, shape.entityId);
        const std::uint32_t color = shape.surface ? (selected ? 0xb0d2e8 : 0x91bed9)
            : (selected ? 0xc8d4df : 0xb2bcc7);
        for (const auto& triangle : shape.mesh.triangles) {
            raster.Draw(triangle, mapping_, forward, shape.mesh.closed, color);
        }
    }
    struct Layer { const modeling::ImageTriangle* triangle; const view::RasterImage* image; double depth; };
    std::vector<Layer> layers;
    for (const auto& image : imageViews_) if(EntityShown(image.entityId)) for (const auto& triangle : image.triangles) {
        const auto center=(triangle.mesh.points[0]+triangle.mesh.points[1]+triangle.mesh.points[2])*(1.0/3);
        layers.push_back({&triangle,&image.image,geometry::Dot(center,forward)});
    }
    std::stable_sort(layers.begin(),layers.end(),[](const auto& a,const auto& b){return a.depth>b.depth;});
    for(const auto& layer:layers)raster.DrawImage(*layer.triangle,mapping_,*layer.image);
    if (display_.shapesVisible) for (const auto& shape : shapeViews_) {
        if (!EntityShown(shape.entityId)) continue;
        const bool selected = app::IsSelected(selection_, shape.entityId);
        const bool hovered = shape.entityId == hoveredEntityId_;
        QColor edge = palette_.background.red() > 160 ? QColor(0x36,0x52,0x66) : QColor(0xc9,0xdf,0xeb);
        if (selected) edge = SemanticColor(app::SemanticState::Selected).darker(125);
        else if (hovered) edge = SemanticColor(app::SemanticState::Hover);
        for (const auto& line : shape.mesh.edges) {
            raster.DrawEdge(line, mapping_, edge.rgb() & 0xffffffu, selected || hovered ? 1 : 0);
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
