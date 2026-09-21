//! 面の解析の描き方(プロンプト surface_analysis)。色の決め方は core(app/SurfaceAnalysis)。

#include "V2Viewport.h"

#include "kachakacha/app/SurfaceAnalysis.h"
#include "kachakacha/view/ShapeShading.h"
#include "kachakacha/view/ViewOrientation.h"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <cmath>
#include <utility>

using kachakacha::v2::geometry::Vector3;

void V2Viewport::SetAnalysisViews(std::vector<AnalysisView> views)
{
    analysisViews_ = std::move(views);
    update();
}

const V2Viewport::AnalysisView* V2Viewport::AnalysisFor(
    const kachakacha::v2::base::EntityId& id) const
{
    if (id.IsNil()) {
        return nullptr;
    }
    for (const AnalysisView& view : analysisViews_) {
        if (view.entityId == id) {
            return &view;
        }
    }
    return nullptr;
}

void V2Viewport::DrawAnalysisShape(QPainter& painter, const AnalysisView& view) const
{
    using kachakacha::v2::app::SurfaceAnalysisMode;
    const Vector3 forward = kachakacha::v2::view::ForwardOf(orientation_);
    const Vector3 up = kachakacha::v2::view::UpOf(orientation_);
    const Vector3 light = kachakacha::v2::view::StandardLightDirection();
    if (!view.Painted()) {
        return;
    }
    painter.setPen(Qt::NoPen);
    const auto order = kachakacha::v2::view::PainterOrder(*view.triangles, forward);
    for (const std::size_t index : order) {
        if (index >= view.data->triangles.size()) {
            continue;
        }
        const auto& analysis = view.data->triangles[index];
        QPolygonF polygon;
        bool ok = true;
        for (const Vector3& point : analysis.triangle.points) {
            const auto screen = ToScreen(point);
            if (!screen.has_value()) {
                ok = false;
                break;
            }
            polygon << *screen;
        }
        if (!ok) {
            continue;
        }
        Vector3 normal = analysis.normals[0] + analysis.normals[1] + analysis.normals[2];
        const double length = normal.Length();
        normal = length > 1.0e-12 ? normal * (1.0 / length) : analysis.triangle.normal;
        QColor color;
        if (view.mode == SurfaceAnalysisMode::Zebra) {
            // 裏から見ていても縞がつながるよう、見る向きに対する表裏をそろえる。
            const double facing = normal.x * forward.x + normal.y * forward.y + normal.z * forward.z;
            const Vector3 seen = facing > 0.0 ? normal * -1.0 : normal;
            color = kachakacha::v2::app::ZebraDark(seen, forward, up, 16) ? QColor(35, 35, 40)
                                                                          : QColor(238, 238, 238);
        } else {
            const bool gaussian = view.mode == SurfaceAnalysisMode::GaussianCurvature;
            const auto& values = gaussian ? analysis.gaussian : analysis.mean;
            const double value = (values[0] + values[1] + values[2]) / 3.0;
            const auto rgb = kachakacha::v2::app::DivergingColor(value, view.scale);
            const double shade = 0.78 + 0.22 * kachakacha::v2::view::LambertShade(normal, light);
            color = QColor(static_cast<int>(rgb.r * shade), static_cast<int>(rgb.g * shade),
                static_cast<int>(rgb.b * shade));
        }
        color.setAlpha(235);
        painter.setBrush(color);
        painter.drawPolygon(polygon);
    }
}

void V2Viewport::DrawAnalysisLines(QPainter& painter, const AnalysisView& view) const
{
    painter.setBrush(Qt::NoBrush);
    for (const AnalysisLine& line : view.lines) {
        QPolygonF path;
        bool complete = true;
        for (const Vector3& point : line.points) {
            const auto screen = ToScreen(point);
            if (!screen.has_value()) {
                complete = false;
                break;
            }
            path << *screen;
        }
        if (!complete || path.size() < 2) {
            continue;
        }
        painter.setPen(QPen(QColor(line.color.r, line.color.g, line.color.b), line.width,
            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(path);
    }
}
