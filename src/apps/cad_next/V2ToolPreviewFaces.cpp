#include "V2Viewport.h"
#include <QPainter>
#include <QPolygonF>

void V2Viewport::SetToolPreviewFaces(
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> faces)
{
    toolPreviewFaces_ = std::move(faces);
    update();
}

void V2Viewport::DrawToolPreviewFaces(QPainter& painter) const
{
    painter.save();
    QColor fill = palette_.preview;
    fill.setAlpha(65);
    painter.setBrush(fill);
    painter.setPen(Qt::NoPen);
    for (const auto& face : toolPreviewFaces_) {
        QPolygonF polygon;
        bool complete = true;
        for (const auto& point : face) {
            const auto screen = ToScreen(point);
            if (!screen.has_value()) { complete = false; break; }
            polygon << *screen;
        }
        if (complete && polygon.size() >= 3) { painter.drawPolygon(polygon); }
    }
    painter.restore();
}
