#include "CadViewport.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using kachakacha::geometry::Vector3;
using kachakacha::model::NamedWire;
using kachakacha::model::WireKind;

namespace {

constexpr double kPlaneHalfSize = 12.0;

double DistanceToSegment(QPointF point, QPointF a, QPointF b)
{
    const QPointF segment = b - a;
    const double lengthSquared = QPointF::dotProduct(segment, segment);
    if (lengthSquared < 1.0e-9) {
        return QLineF(point, a).length();
    }

    const double t = std::clamp(QPointF::dotProduct(point - a, segment) / lengthSquared, 0.0, 1.0);
    return QLineF(point, a + segment * t).length();
}

QColor WireColor(WireKind kind)
{
    switch (kind) {
    case WireKind::Line:
    case WireKind::Polyline:
        return QColor("#24313b");
    case WireKind::CubicBezier:
        return QColor("#007f78");
    case WireKind::Circle:
    case WireKind::CircularArc:
        return QColor("#a23b3b");
    }
    return QColor("#24313b");
}

} // namespace

CadViewport::CadViewport(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(520, 420);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void CadViewport::SetProject(const kachakacha::model::Project* project)
{
    project_ = project;
    selection_ = {};
    FitAll();
}

void CadViewport::SetSelection(CadSelection selection)
{
    selection_ = selection;
    update();
}

void CadViewport::SetSelectionChangedCallback(std::function<void(CadSelection)> callback)
{
    selectionChanged_ = std::move(callback);
}

QPointF CadViewport::ProjectPoint(Vector3 point) const
{
    const Vector3 viewDirection = {
        std::cos(pitchRadians_) * std::cos(yawRadians_),
        std::cos(pitchRadians_) * std::sin(yawRadians_),
        std::sin(pitchRadians_),
    };
    const Vector3 right = Vector3{-viewDirection.y, viewDirection.x, 0.0}.Normalized();
    const Vector3 up = Cross(viewDirection, right).Normalized();
    const Vector3 relative = point - target_;
    return {
        width() * 0.5 + Dot(relative, right) * pixelsPerMillimeter_,
        height() * 0.5 - Dot(relative, up) * pixelsPerMillimeter_,
    };
}

void CadViewport::FitAll()
{
    if (project_ == nullptr) {
        return;
    }

    Vector3 minimum{0.0, 0.0, 0.0};
    Vector3 maximum{0.0, 0.0, 0.0};
    bool hasPoint = false;
    auto include = [&](Vector3 point) {
        if (!hasPoint) {
            minimum = point;
            maximum = point;
            hasPoint = true;
            return;
        }
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    };

    for (const auto& wire : project_->Wires()) {
        for (int sample = 0; sample <= 32; ++sample) {
            include(wire.wire.Evaluate(static_cast<double>(sample) / 32.0));
        }
    }
    for (const auto& plane : project_->WorkPlanes()) {
        include(plane.plane.Origin());
    }

    if (!hasPoint) {
        target_ = {};
        pixelsPerMillimeter_ = 14.0;
    } else {
        target_ = (minimum + maximum) * 0.5;
        const double span = std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z, 20.0});
        const double available = std::max(200, std::min(width(), height()));
        pixelsPerMillimeter_ = std::clamp(available / (span * 1.45), 1.0, 80.0);
    }
    update();
}

void CadViewport::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#f5f6f7"));

    painter.setPen(QPen(QColor("#d7dce0"), 1.0));
    for (int coordinate = -50; coordinate <= 50; coordinate += 5) {
        painter.drawLine(ProjectPoint({static_cast<double>(coordinate), -50.0, 0.0}), ProjectPoint({static_cast<double>(coordinate), 50.0, 0.0}));
        painter.drawLine(ProjectPoint({-50.0, static_cast<double>(coordinate), 0.0}), ProjectPoint({50.0, static_cast<double>(coordinate), 0.0}));
    }

    if (project_ != nullptr) {
        for (int index = 0; index < static_cast<int>(project_->WorkPlanes().size()); ++index) {
            const auto& namedPlane = project_->WorkPlanes()[index];
            const auto& plane = namedPlane.plane;
            QPolygonF polygon;
            polygon << ProjectPoint(plane.ToWorld(-kPlaneHalfSize, -kPlaneHalfSize))
                    << ProjectPoint(plane.ToWorld(kPlaneHalfSize, -kPlaneHalfSize))
                    << ProjectPoint(plane.ToWorld(kPlaneHalfSize, kPlaneHalfSize))
                    << ProjectPoint(plane.ToWorld(-kPlaneHalfSize, kPlaneHalfSize));
            const bool selected = selection_.kind == CadSelectionKind::WorkPlane && selection_.index == index;
            painter.setBrush(selected ? QColor(241, 178, 54, 52) : QColor(69, 132, 142, 24));
            painter.setPen(QPen(selected ? QColor("#c47a13") : QColor("#7d9aa0"), selected ? 2.2 : 1.0, Qt::DashLine));
            painter.drawPolygon(polygon);

            painter.setPen(QPen(QColor("#25747d"), 1.7));
            painter.drawLine(ProjectPoint(plane.Origin()), ProjectPoint(plane.ToWorld(4.0, 0.0)));
            painter.setPen(QPen(QColor("#8b5a2b"), 1.7));
            painter.drawLine(ProjectPoint(plane.Origin()), ProjectPoint(plane.ToWorld(0.0, 4.0)));
        }

        for (int index = 0; index < static_cast<int>(project_->Wires().size()); ++index) {
            const NamedWire& namedWire = project_->Wires()[index];
            const bool selected = selection_.kind == CadSelectionKind::Wire && selection_.index == index;
            painter.setPen(QPen(selected ? QColor("#e69f00") : WireColor(namedWire.wire.Kind()), selected ? 3.2 : 2.0));
            QPainterPath path(ProjectPoint(namedWire.wire.Evaluate(0.0)));
            const int samples = namedWire.wire.Kind() == WireKind::Line ? 1 : 64;
            for (int sample = 1; sample <= samples; ++sample) {
                path.lineTo(ProjectPoint(namedWire.wire.Evaluate(static_cast<double>(sample) / samples)));
            }
            painter.drawPath(path);

            if (selected) {
                painter.setBrush(QColor("#ffffff"));
                painter.setPen(QPen(QColor("#e69f00"), 2.0));
                for (const Vector3& point : namedWire.wire.ControlPoints()) {
                    painter.drawEllipse(ProjectPoint(point), 4.0, 4.0);
                }
            }
        }
    }

    const std::array<std::pair<Vector3, QColor>, 3> axes = {{
        {{8.0, 0.0, 0.0}, QColor("#c33b3b")},
        {{0.0, 8.0, 0.0}, QColor("#32844b")},
        {{0.0, 0.0, 8.0}, QColor("#336fc2")},
    }};
    const std::array<QString, 3> labels = {"X", "Y", "Z"};
    for (int index = 0; index < 3; ++index) {
        painter.setPen(QPen(axes[index].second, 2.5));
        painter.drawLine(ProjectPoint({0.0, 0.0, 0.0}), ProjectPoint(axes[index].first));
        painter.drawText(ProjectPoint(axes[index].first) + QPointF(4.0, -4.0), labels[index]);
    }

    painter.setPen(QColor("#52606a"));
    painter.drawText(QRect(14, 12, width() - 28, 24), Qt::AlignLeft, QString::fromUtf8("左ドラッグ: 回転   右ドラッグ: 移動   ホイール: 拡大縮小"));
}

CadSelection CadViewport::HitTest(QPointF position) const
{
    if (project_ == nullptr) {
        return {};
    }

    double bestDistance = 9.0;
    CadSelection best;
    for (int index = 0; index < static_cast<int>(project_->Wires().size()); ++index) {
        const auto& wire = project_->Wires()[index].wire;
        QPointF previous = ProjectPoint(wire.Evaluate(0.0));
        const int samples = wire.Kind() == WireKind::Line ? 1 : 48;
        for (int sample = 1; sample <= samples; ++sample) {
            const QPointF current = ProjectPoint(wire.Evaluate(static_cast<double>(sample) / samples));
            const double distance = DistanceToSegment(position, previous, current);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = {CadSelectionKind::Wire, index};
            }
            previous = current;
        }
    }
    if (best.kind != CadSelectionKind::None) {
        return best;
    }

    for (int index = 0; index < static_cast<int>(project_->WorkPlanes().size()); ++index) {
        const auto& plane = project_->WorkPlanes()[index].plane;
        const std::array<QPointF, 4> corners = {
            ProjectPoint(plane.ToWorld(-kPlaneHalfSize, -kPlaneHalfSize)),
            ProjectPoint(plane.ToWorld(kPlaneHalfSize, -kPlaneHalfSize)),
            ProjectPoint(plane.ToWorld(kPlaneHalfSize, kPlaneHalfSize)),
            ProjectPoint(plane.ToWorld(-kPlaneHalfSize, kPlaneHalfSize)),
        };
        for (int edge = 0; edge < 4; ++edge) {
            if (DistanceToSegment(position, corners[edge], corners[(edge + 1) % 4]) < 7.0) {
                return {CadSelectionKind::WorkPlane, index};
            }
        }
    }
    return {};
}

void CadViewport::mousePressEvent(QMouseEvent* event)
{
    lastMousePosition_ = event->position().toPoint();
    dragButton_ = event->button();
    mouseMoved_ = false;
    setFocus();
}

void CadViewport::mouseMoveEvent(QMouseEvent* event)
{
    if (dragButton_ == Qt::NoButton) {
        return;
    }
    const QPoint delta = event->position().toPoint() - lastMousePosition_;
    if (delta.manhattanLength() > 1) {
        mouseMoved_ = true;
    }
    if (dragButton_ == Qt::LeftButton) {
        yawRadians_ += delta.x() * 0.008;
        pitchRadians_ = std::clamp(pitchRadians_ - delta.y() * 0.008, -1.45, 1.45);
    } else if (dragButton_ == Qt::RightButton || dragButton_ == Qt::MiddleButton) {
        const Vector3 viewDirection = {
            std::cos(pitchRadians_) * std::cos(yawRadians_),
            std::cos(pitchRadians_) * std::sin(yawRadians_),
            std::sin(pitchRadians_),
        };
        const Vector3 right = Vector3{-viewDirection.y, viewDirection.x, 0.0}.Normalized();
        const Vector3 up = Cross(viewDirection, right).Normalized();
        target_ = target_ - right * (delta.x() / pixelsPerMillimeter_) + up * (delta.y() / pixelsPerMillimeter_);
    }
    lastMousePosition_ = event->position().toPoint();
    update();
}

void CadViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !mouseMoved_) {
        selection_ = HitTest(event->position());
        NotifySelection();
        update();
    }
    dragButton_ = Qt::NoButton;
}

void CadViewport::wheelEvent(QWheelEvent* event)
{
    const double factor = std::pow(1.0015, event->angleDelta().y());
    pixelsPerMillimeter_ = std::clamp(pixelsPerMillimeter_ * factor, 0.25, 400.0);
    update();
}

void CadViewport::NotifySelection()
{
    if (selectionChanged_) {
        selectionChanged_(selection_);
    }
}
