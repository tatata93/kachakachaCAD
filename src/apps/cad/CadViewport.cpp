#include "CadViewport.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

using kachakacha::geometry::Vector3;
using kachakacha::geometry::AlmostEqual;
using kachakacha::model::NamedWire;
using kachakacha::model::Wire;
using kachakacha::model::WireKind;

namespace {

constexpr double kPlaneHalfSize = 12.0;

enum class ViewCubeFace {
    None,
    Top,
    Front,
    Right,
    Isometric,
};

struct ViewCubeGeometry {
    QPolygonF top;
    QPolygonF front;
    QPolygonF right;
    QPolygonF isometric;
    QRectF bounds;
};

ViewCubeGeometry MakeViewCubeGeometry(int viewportWidth)
{
    const double centerX = viewportWidth - 60.0;
    const QPointF top(centerX, 16.0);
    const QPointF upperRight(centerX + 30.0, 31.0);
    const QPointF center(centerX, 46.0);
    const QPointF upperLeft(centerX - 30.0, 31.0);
    const QPointF lowerLeft(centerX - 30.0, 62.0);
    const QPointF lowerCenter(centerX, 77.0);
    const QPointF lowerRight(centerX + 30.0, 62.0);
    return {
        QPolygonF{top, upperRight, center, upperLeft},
        QPolygonF{upperLeft, center, lowerCenter, lowerLeft},
        QPolygonF{center, upperRight, lowerRight, lowerCenter},
        QPolygonF{
            QPointF(centerX, 84.0),
            QPointF(centerX + 24.0, 96.0),
            QPointF(centerX, 108.0),
            QPointF(centerX - 24.0, 96.0)},
        QRectF(centerX - 36.0, 10.0, 72.0, 104.0),
    };
}

ViewCubeFace HitViewCube(const ViewCubeGeometry& cube, QPointF position)
{
    if (cube.top.containsPoint(position, Qt::OddEvenFill)) {
        return ViewCubeFace::Top;
    }
    if (cube.front.containsPoint(position, Qt::OddEvenFill)) {
        return ViewCubeFace::Front;
    }
    if (cube.right.containsPoint(position, Qt::OddEvenFill)) {
        return ViewCubeFace::Right;
    }
    if (cube.isometric.containsPoint(position, Qt::OddEvenFill)) {
        return ViewCubeFace::Isometric;
    }
    return ViewCubeFace::None;
}

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

void CadViewport::SetProject(const kachakacha::model::Project* project, bool fitView)
{
    project_ = project;
    selection_ = {};
    selections_.clear();
    reference_ = {};
    if (fitView) {
        FitAll();
    } else {
        update();
    }
}

void CadViewport::SetSelection(CadSelection selection)
{
    if (selection.kind == CadSelectionKind::None) {
        SetSelections({});
    } else {
        SetSelections({selection});
    }
}

void CadViewport::SetSelections(std::vector<CadSelection> selections)
{
    selections_ = std::move(selections);
    selection_ = selections_.empty() ? CadSelection{} : selections_.back();
    update();
}

void CadViewport::SetReference(CadSelection reference)
{
    reference_ = reference;
    update();
}

void CadViewport::SetSelectionChangedCallback(std::function<void(const std::vector<CadSelection>&)> callback)
{
    selectionChanged_ = std::move(callback);
}

void CadViewport::SetLineCreatedCallback(std::function<void(Vector3, Vector3)> callback)
{
    lineCreated_ = std::move(callback);
}

void CadViewport::SetPolylineCreatedCallback(std::function<void(const std::vector<Vector3>&)> callback)
{
    polylineCreated_ = std::move(callback);
}

void CadViewport::SetRectangleCreatedCallback(std::function<void(const std::array<Vector3, 4>&)> callback)
{
    rectangleCreated_ = std::move(callback);
}

void CadViewport::SetCircleCreatedCallback(std::function<void(Vector3, double)> callback)
{
    circleCreated_ = std::move(callback);
}

void CadViewport::SetArcCreatedCallback(std::function<void(Vector3, Vector3, Vector3)> callback)
{
    arcCreated_ = std::move(callback);
}

void CadViewport::SetBezierCreatedCallback(std::function<void(const std::array<Vector3, 4>&)> callback)
{
    bezierCreated_ = std::move(callback);
}

void CadViewport::SetTranslationRequestedCallback(std::function<void(Vector3, bool)> callback)
{
    translationRequested_ = std::move(callback);
}

void CadViewport::SetMirrorRequestedCallback(std::function<void(Vector3, Vector3, Vector3)> callback)
{
    mirrorRequested_ = std::move(callback);
}

void CadViewport::SetRotationRequestedCallback(std::function<void(Vector3, Vector3, double)> callback)
{
    rotationRequested_ = std::move(callback);
}

void CadViewport::SetSplitRequestedCallback(std::function<void(int, double)> callback)
{
    splitRequested_ = std::move(callback);
}

void CadViewport::SetDrawingStateChangedCallback(std::function<void(ViewportTool, std::size_t)> callback)
{
    drawingStateChanged_ = std::move(callback);
    NotifyDrawingState();
}

void CadViewport::SetActiveWorkPlane(std::optional<kachakacha::model::WorkPlane> plane)
{
    activePlane_ = std::move(plane);
    CancelDrawing();
    update();
}

void CadViewport::SetTool(ViewportTool tool)
{
    tool_ = tool;
    CancelDrawing();
    setCursor(tool_ == ViewportTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

void CadViewport::SetSnapEnabled(bool enabled)
{
    snapEnabled_ = enabled;
    update();
}

void CadViewport::SetSnapStep(double stepMillimeters)
{
    if (std::isfinite(stepMillimeters) && stepMillimeters > 1.0e-6) {
        snapStep_ = stepMillimeters;
        update();
    }
}

void CadViewport::SetPlateSplitPreview(
    std::optional<kachakacha::model::PlateSplitAxis> axis,
    double parameter)
{
    plateSplitPreviewAxis_ = axis;
    plateSplitPreviewParameter_ = std::clamp(parameter, 0.0, 1.0);
    update();
}

void CadViewport::AlignToActiveWorkPlane()
{
    if (!activePlane_.has_value()) {
        return;
    }
    AlignToWorkPlane(*activePlane_);
}

void CadViewport::AlignToWorkPlane(const kachakacha::model::WorkPlane& plane)
{
    target_ = plane.Origin();
    alignedViewBasis_ = std::array<Vector3, 3>{plane.Normal(), plane.UAxis(), plane.VAxis()};
    update();
}

void CadViewport::SetIsometricView()
{
    alignedViewBasis_.reset();
    yawRadians_ = 0.75;
    pitchRadians_ = 0.48;
    update();
}

Vector3 CadViewport::ViewDirection() const
{
    return CurrentViewBasis()[0];
}

void CadViewport::FinishDrawing()
{
    if (tool_ == ViewportTool::DrawPolyline && drawingPoints_.size() >= 2) {
        const std::vector<Vector3> points = drawingPoints_;
        drawingPoints_.clear();
        if (polylineCreated_) {
            polylineCreated_(points);
        }
    } else {
        drawingPoints_.clear();
    }
    hoverDrawingPoint_.reset();
    NotifyDrawingState();
    update();
}

void CadViewport::CancelDrawing()
{
    drawingPoints_.clear();
    hoverDrawingPoint_.reset();
    splitPreviewParameter_.reset();
    NotifyDrawingState();
    update();
}

DrawingMeasurements CadViewport::CurrentDrawingMeasurements() const
{
    DrawingMeasurements measurements;
    if (!activePlane_.has_value() || drawingPoints_.empty() || !hoverDrawingPoint_.has_value()) {
        return measurements;
    }

    const Vector3 anchor = tool_ == ViewportTool::DrawPolyline
        ? drawingPoints_.back()
        : drawingPoints_.front();
    const auto start = activePlane_->Project(anchor);
    const auto hover = activePlane_->Project(*hoverDrawingPoint_);
    const double deltaU = hover.u - start.u;
    const double deltaV = hover.v - start.v;

    if (tool_ == ViewportTool::DrawLine || tool_ == ViewportTool::DrawPolyline) {
        measurements.lengthMillimeters = std::hypot(deltaU, deltaV);
        measurements.angleDegrees = std::atan2(deltaV, deltaU) * 180.0 / std::numbers::pi;
        measurements.available = measurements.lengthMillimeters > 1.0e-9;
    } else if (tool_ == ViewportTool::DrawRectangle) {
        measurements.widthMillimeters = std::abs(deltaU);
        measurements.heightMillimeters = std::abs(deltaV);
        measurements.available = measurements.widthMillimeters > 1.0e-9
            && measurements.heightMillimeters > 1.0e-9;
    } else if (tool_ == ViewportTool::DrawCircle) {
        measurements.radiusMillimeters = std::hypot(deltaU, deltaV);
        measurements.available = measurements.radiusMillimeters > 1.0e-9;
    }
    return measurements;
}

bool CadViewport::CommitDrawingDimensions(double primaryMillimeters, double secondaryValue)
{
    if (!activePlane_.has_value() || drawingPoints_.empty()
        || !std::isfinite(primaryMillimeters) || primaryMillimeters <= 1.0e-9
        || !std::isfinite(secondaryValue)) {
        return false;
    }

    if (tool_ == ViewportTool::DrawLine || tool_ == ViewportTool::DrawPolyline) {
        const Vector3 anchor = tool_ == ViewportTool::DrawPolyline
            ? drawingPoints_.back()
            : drawingPoints_.front();
        const auto coordinates = activePlane_->Project(anchor);
        const double angleRadians = secondaryValue * std::numbers::pi / 180.0;
        CommitDrawingPoint(activePlane_->ToWorld(
            coordinates.u + primaryMillimeters * std::cos(angleRadians),
            coordinates.v + primaryMillimeters * std::sin(angleRadians)));
        return true;
    }

    if (tool_ == ViewportTool::DrawRectangle && secondaryValue > 1.0e-9) {
        const auto start = activePlane_->Project(drawingPoints_.front());
        double directionU = 1.0;
        double directionV = 1.0;
        if (hoverDrawingPoint_.has_value()) {
            const auto hover = activePlane_->Project(*hoverDrawingPoint_);
            directionU = hover.u < start.u ? -1.0 : 1.0;
            directionV = hover.v < start.v ? -1.0 : 1.0;
        }
        CommitDrawingPoint(activePlane_->ToWorld(
            start.u + directionU * primaryMillimeters,
            start.v + directionV * secondaryValue));
        return true;
    }

    if (tool_ == ViewportTool::DrawCircle) {
        const auto center = activePlane_->Project(drawingPoints_.front());
        CommitDrawingPoint(activePlane_->ToWorld(center.u + primaryMillimeters, center.v));
        return true;
    }
    return false;
}

bool CadViewport::IsSelected(CadSelectionKind kind, int index) const
{
    return std::any_of(selections_.begin(), selections_.end(), [&](const CadSelection& selection) {
        return selection.kind == kind && selection.index == index;
    });
}

std::array<Vector3, 3> CadViewport::CurrentViewBasis() const
{
    if (alignedViewBasis_.has_value()) {
        return *alignedViewBasis_;
    }
    const Vector3 viewDirection = {
        std::cos(pitchRadians_) * std::cos(yawRadians_),
        std::cos(pitchRadians_) * std::sin(yawRadians_),
        std::sin(pitchRadians_),
    };
    Vector3 right{-viewDirection.y, viewDirection.x, 0.0};
    if (right.LengthSquared() <= 1.0e-12) {
        right = {1.0, 0.0, 0.0};
    } else {
        right = right.Normalized();
    }
    const Vector3 up = Cross(viewDirection, right).Normalized();
    return {viewDirection, right, up};
}

QPointF CadViewport::ProjectPoint(Vector3 point) const
{
    const auto basis = CurrentViewBasis();
    const Vector3& right = basis[1];
    const Vector3& up = basis[2];
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

    alignedViewBasis_.reset();
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
        if (!wire.visible) {
            continue;
        }
        for (int sample = 0; sample <= 32; ++sample) {
            include(wire.wire.Evaluate(static_cast<double>(sample) / 32.0));
        }
    }
    for (const auto& surface : project_->Surfaces()) {
        if (!surface.visible) {
            continue;
        }
        for (int uIndex = 0; uIndex <= 24; ++uIndex) {
            for (int vIndex = 0; vIndex <= 8; ++vIndex) {
                include(surface.surface.Evaluate(
                    static_cast<double>(uIndex) / 24.0,
                    static_cast<double>(vIndex) / 8.0));
            }
        }
    }
    for (const auto& plate : project_->Plates()) {
        if (!plate.visible) {
            continue;
        }
        for (int uIndex = 0; uIndex <= 24; ++uIndex) {
            for (int vIndex = 0; vIndex <= 8; ++vIndex) {
                const double u = static_cast<double>(uIndex) / 24.0;
                const double v = static_cast<double>(vIndex) / 8.0;
                include(plate.plate.Evaluate(u, v, 0.0));
                include(plate.plate.Evaluate(u, v, 1.0));
            }
        }
    }
    for (const auto& plane : project_->WorkPlanes()) {
        if (plane.visible) {
            include(plane.plane.Origin());
        }
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

std::optional<Vector3> CadViewport::PointOnActivePlane(QPointF position) const
{
    if (!activePlane_.has_value()) {
        return std::nullopt;
    }
    const auto basis = CurrentViewBasis();
    const Vector3& viewDirection = basis[0];
    const Vector3& right = basis[1];
    const Vector3& up = basis[2];
    const double screenX = (position.x() - width() * 0.5) / pixelsPerMillimeter_;
    const double screenY = (height() * 0.5 - position.y()) / pixelsPerMillimeter_;
    const Vector3 rayPoint = target_ + right * screenX + up * screenY;
    const double denominator = Dot(viewDirection, activePlane_->Normal());
    if (std::abs(denominator) <= 1.0e-9) {
        return std::nullopt;
    }
    const double distance = Dot(activePlane_->Origin() - rayPoint, activePlane_->Normal()) / denominator;
    return rayPoint + viewDirection * distance;
}

std::optional<double> CadViewport::NearestWireParameter(int wireIndex, QPointF position, double maximumDistance) const
{
    if (project_ == nullptr || wireIndex < 0 || wireIndex >= static_cast<int>(project_->Wires().size())
        || !project_->Wires()[wireIndex].visible) {
        return std::nullopt;
    }
    const Wire& wire = project_->Wires()[wireIndex].wire;
    const int samples = wire.Kind() == WireKind::Line ? 1 : 256;
    double bestDistance = maximumDistance;
    double bestParameter = 0.0;
    QPointF previous = ProjectPoint(wire.Evaluate(0.0));
    for (int sample = 0; sample < samples; ++sample) {
        const QPointF current = ProjectPoint(wire.Evaluate(static_cast<double>(sample + 1) / samples));
        const QPointF segment = current - previous;
        const double lengthSquared = QPointF::dotProduct(segment, segment);
        const double local = lengthSquared <= 1.0e-12
            ? 0.0
            : std::clamp(QPointF::dotProduct(position - previous, segment) / lengthSquared, 0.0, 1.0);
        const double distance = QLineF(position, previous + segment * local).length();
        if (distance < bestDistance) {
            bestDistance = distance;
            bestParameter = (static_cast<double>(sample) + local) / samples;
        }
        previous = current;
    }
    if (bestDistance >= maximumDistance || bestParameter <= 1.0e-6 || bestParameter >= 1.0 - 1.0e-6) {
        return std::nullopt;
    }
    return bestParameter;
}

Vector3 CadViewport::SnapPoint(Vector3 point, QPointF screenPosition) const
{
    if (!activePlane_.has_value() || !snapEnabled_) {
        return point;
    }

    if (project_ != nullptr) {
        double bestDistance = 10.0;
        std::optional<Vector3> bestEndpoint;
        for (const auto& namedWire : project_->Wires()) {
            if (!namedWire.visible) {
                continue;
            }
            std::vector<Vector3> snapPoints;
            if (namedWire.wire.Kind() == WireKind::Polyline) {
                snapPoints = namedWire.wire.ControlPoints();
            } else {
                snapPoints = {namedWire.wire.Start(), namedWire.wire.End()};
            }
            for (const Vector3& endpoint : snapPoints) {
                if (std::abs(activePlane_->Project(endpoint).w) > 1.0e-6) {
                    continue;
                }
                const double distance = QLineF(screenPosition, ProjectPoint(endpoint)).length();
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestEndpoint = endpoint;
                }
            }
        }
        if (bestEndpoint.has_value()) {
            return *bestEndpoint;
        }
    }

    const auto coordinates = activePlane_->Project(point);
    const double snappedU = std::round(coordinates.u / snapStep_) * snapStep_;
    const double snappedV = std::round(coordinates.v / snapStep_) * snapStep_;
    return activePlane_->ToWorld(snappedU, snappedV);
}

Vector3 CadViewport::ApplyDrawingConstraint(Vector3 point, Qt::KeyboardModifiers modifiers) const
{
    if (!activePlane_.has_value() || drawingPoints_.empty()
        || !modifiers.testFlag(Qt::ShiftModifier)) {
        return point;
    }

    const Vector3 anchor = tool_ == ViewportTool::DrawPolyline
        ? drawingPoints_.back()
        : drawingPoints_.front();
    const auto start = activePlane_->Project(anchor);
    auto target = activePlane_->Project(point);
    const double deltaU = target.u - start.u;
    const double deltaV = target.v - start.v;

    if (tool_ == ViewportTool::DrawLine || tool_ == ViewportTool::DrawPolyline) {
        if (std::abs(deltaU) >= std::abs(deltaV)) {
            target.v = start.v;
        } else {
            target.u = start.u;
        }
        return activePlane_->ToWorld(target.u, target.v);
    }

    if (tool_ == ViewportTool::DrawRectangle) {
        const double side = std::max(std::abs(deltaU), std::abs(deltaV));
        target.u = start.u + (deltaU < 0.0 ? -side : side);
        target.v = start.v + (deltaV < 0.0 ? -side : side);
        return activePlane_->ToWorld(target.u, target.v);
    }
    return point;
}

void CadViewport::CommitDrawingPoint(Vector3 point)
{
    if (!activePlane_.has_value() || tool_ == ViewportTool::Select) {
        return;
    }
    if (!drawingPoints_.empty() && (point - drawingPoints_.back()).LengthSquared() <= 1.0e-18) {
        return;
    }

    if (tool_ == ViewportTool::DrawRectangle && drawingPoints_.size() == 1) {
        const auto firstCoordinates = activePlane_->Project(drawingPoints_.front());
        const auto secondCoordinates = activePlane_->Project(point);
        if (std::abs(secondCoordinates.u - firstCoordinates.u) <= 1.0e-9
            || std::abs(secondCoordinates.v - firstCoordinates.v) <= 1.0e-9) {
            return;
        }
    }

    drawingPoints_.push_back(point);
    if ((tool_ == ViewportTool::MoveSelection || tool_ == ViewportTool::CopySelection)
        && drawingPoints_.size() == 2) {
        const Vector3 delta = drawingPoints_[1] - drawingPoints_[0];
        const bool copy = tool_ == ViewportTool::CopySelection;
        drawingPoints_.clear();
        if (translationRequested_) {
            translationRequested_(delta, copy);
        }
    } else if (tool_ == ViewportTool::MirrorSelection && drawingPoints_.size() == 2) {
        const Vector3 linePoint = drawingPoints_[0];
        const Vector3 lineDirection = drawingPoints_[1] - drawingPoints_[0];
        const Vector3 planeNormal = activePlane_->Normal();
        drawingPoints_.clear();
        if (mirrorRequested_) {
            mirrorRequested_(linePoint, lineDirection, planeNormal);
        }
    } else if (tool_ == ViewportTool::RotateSelection && drawingPoints_.size() == 3) {
        const Vector3 from = drawingPoints_[1] - drawingPoints_[0];
        const Vector3 to = drawingPoints_[2] - drawingPoints_[0];
        if (from.LengthSquared() <= 1.0e-18 || to.LengthSquared() <= 1.0e-18) {
            drawingPoints_.pop_back();
            return;
        }
        const Vector3 normal = activePlane_->Normal();
        const double angle = std::atan2(Dot(Cross(from, to), normal), Dot(from, to));
        const Vector3 axisPoint = drawingPoints_[0];
        drawingPoints_.clear();
        if (rotationRequested_) {
            rotationRequested_(axisPoint, normal, angle);
        }
    } else if (tool_ == ViewportTool::DrawLine && drawingPoints_.size() == 2) {
        const Vector3 start = drawingPoints_[0];
        const Vector3 end = drawingPoints_[1];
        drawingPoints_.clear();
        if (lineCreated_) {
            lineCreated_(start, end);
        }
    } else if (tool_ == ViewportTool::DrawRectangle && drawingPoints_.size() == 2) {
        const auto firstCoordinates = activePlane_->Project(drawingPoints_[0]);
        const auto secondCoordinates = activePlane_->Project(drawingPoints_[1]);
        const std::array<Vector3, 4> corners = {
            activePlane_->ToWorld(firstCoordinates.u, firstCoordinates.v),
            activePlane_->ToWorld(secondCoordinates.u, firstCoordinates.v),
            activePlane_->ToWorld(secondCoordinates.u, secondCoordinates.v),
            activePlane_->ToWorld(firstCoordinates.u, secondCoordinates.v),
        };
        drawingPoints_.clear();
        if (rectangleCreated_) {
            rectangleCreated_(corners);
        }
    } else if (tool_ == ViewportTool::DrawCircle && drawingPoints_.size() == 2) {
        const Vector3 center = drawingPoints_[0];
        const double radius = (drawingPoints_[1] - drawingPoints_[0]).Length();
        drawingPoints_.clear();
        if (circleCreated_) {
            circleCreated_(center, radius);
        }
    } else if (tool_ == ViewportTool::DrawArc && drawingPoints_.size() == 3) {
        const std::array<Vector3, 3> points = {drawingPoints_[0], drawingPoints_[1], drawingPoints_[2]};
        drawingPoints_.clear();
        if (arcCreated_) {
            arcCreated_(points[0], points[1], points[2]);
        }
    } else if (tool_ == ViewportTool::DrawBezier && drawingPoints_.size() == 4) {
        const std::array<Vector3, 4> points = {drawingPoints_[0], drawingPoints_[1], drawingPoints_[2], drawingPoints_[3]};
        drawingPoints_.clear();
        if (bezierCreated_) {
            bezierCreated_(points);
        }
    }
    hoverDrawingPoint_ = point;
    NotifyDrawingState();
    update();
}

void CadViewport::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#f5f6f7"));

    painter.setPen(QPen(QColor("#d7dce0"), 1.0));
    if (activePlane_.has_value()) {
        double spacing = snapEnabled_ ? snapStep_ : 5.0;
        while (spacing * pixelsPerMillimeter_ < 12.0) {
            spacing *= 2.0;
        }
        const double extent = std::max(width(), height()) / std::max(pixelsPerMillimeter_, 0.1);
        const int lineCount = std::min(100, static_cast<int>(std::ceil(extent / spacing)) + 2);
        for (int index = -lineCount; index <= lineCount; ++index) {
            const double coordinate = index * spacing;
            painter.drawLine(ProjectPoint(activePlane_->ToWorld(coordinate, -extent)), ProjectPoint(activePlane_->ToWorld(coordinate, extent)));
            painter.drawLine(ProjectPoint(activePlane_->ToWorld(-extent, coordinate)), ProjectPoint(activePlane_->ToWorld(extent, coordinate)));
        }
    } else if (project_ == nullptr || project_->WorkPlanes().empty()) {
        for (int coordinate = -50; coordinate <= 50; coordinate += 5) {
            painter.drawLine(ProjectPoint({static_cast<double>(coordinate), -50.0, 0.0}), ProjectPoint({static_cast<double>(coordinate), 50.0, 0.0}));
            painter.drawLine(ProjectPoint({-50.0, static_cast<double>(coordinate), 0.0}), ProjectPoint({50.0, static_cast<double>(coordinate), 0.0}));
        }
    }

    if (project_ != nullptr) {
        for (int index = 0; index < static_cast<int>(project_->WorkPlanes().size()); ++index) {
            const auto& namedPlane = project_->WorkPlanes()[index];
            if (!namedPlane.visible) {
                continue;
            }
            const auto& plane = namedPlane.plane;
            QPolygonF polygon;
            polygon << ProjectPoint(plane.ToWorld(-kPlaneHalfSize, -kPlaneHalfSize))
                    << ProjectPoint(plane.ToWorld(kPlaneHalfSize, -kPlaneHalfSize))
                    << ProjectPoint(plane.ToWorld(kPlaneHalfSize, kPlaneHalfSize))
                    << ProjectPoint(plane.ToWorld(-kPlaneHalfSize, kPlaneHalfSize));
            const bool selected = IsSelected(CadSelectionKind::WorkPlane, index);
            const bool active = activePlane_.has_value()
                && AlmostEqual(activePlane_->Origin(), plane.Origin(), 1.0e-8)
                && AlmostEqual(activePlane_->Normal(), plane.Normal(), 1.0e-8)
                && AlmostEqual(activePlane_->UAxis(), plane.UAxis(), 1.0e-8);
            painter.setBrush(selected ? QColor(241, 178, 54, 52) : active ? QColor(0, 127, 120, 36) : QColor(69, 132, 142, 18));
            painter.setPen(QPen(selected ? QColor("#c47a13") : active ? QColor("#007f78") : QColor("#7d9aa0"), selected || active ? 2.2 : 1.0, Qt::DashLine));
            painter.drawPolygon(polygon);

            painter.setPen(QPen(QColor("#25747d"), 1.7));
            painter.drawLine(ProjectPoint(plane.Origin()), ProjectPoint(plane.ToWorld(4.0, 0.0)));
            painter.setPen(QPen(QColor("#8b5a2b"), 1.7));
            painter.drawLine(ProjectPoint(plane.Origin()), ProjectPoint(plane.ToWorld(0.0, 4.0)));
        }

        for (int index = 0; index < static_cast<int>(project_->Surfaces().size()); ++index) {
            const auto& namedSurface = project_->Surfaces()[index];
            if (!namedSurface.visible) {
                continue;
            }
            const auto& surface = namedSurface.surface;
            const bool selected = IsSelected(CadSelectionKind::Surface, index);
            const QColor fill = selected ? QColor(230, 159, 0, 90) : QColor(31, 132, 138, 66);
            const QColor edge = selected ? QColor("#c47a13") : QColor("#277b80");
            if (surface.Kind() == kachakacha::model::SurfaceKind::Planar) {
                QPainterPath boundary(ProjectPoint(surface.FirstBoundary().Evaluate(0.0)));
                for (int sample = 1; sample <= 128; ++sample) {
                    boundary.lineTo(ProjectPoint(surface.FirstBoundary().Evaluate(static_cast<double>(sample) / 128.0)));
                }
                boundary.closeSubpath();
                painter.setBrush(fill);
                painter.setPen(QPen(edge, selected ? 2.5 : 1.4));
                painter.drawPath(boundary);
            } else {
                painter.setPen(QPen(edge, selected ? 1.8 : 0.7));
                for (int uIndex = 0; uIndex < 32; ++uIndex) {
                    for (int vIndex = 0; vIndex < 10; ++vIndex) {
                        const double u0 = static_cast<double>(uIndex) / 32.0;
                        const double u1 = static_cast<double>(uIndex + 1) / 32.0;
                        const double v0 = static_cast<double>(vIndex) / 10.0;
                        const double v1 = static_cast<double>(vIndex + 1) / 10.0;
                        QPolygonF patch;
                        patch << ProjectPoint(surface.Evaluate(u0, v0))
                              << ProjectPoint(surface.Evaluate(u1, v0))
                              << ProjectPoint(surface.Evaluate(u1, v1))
                              << ProjectPoint(surface.Evaluate(u0, v1));
                        QColor patchFill = fill;
                        if ((uIndex + vIndex) % 2 != 0) {
                            patchFill.setAlpha(std::max(16, patchFill.alpha() - 14));
                        }
                        painter.setBrush(patchFill);
                        painter.drawPolygon(patch);
                    }
                }
            }
        }

        for (int index = 0; index < static_cast<int>(project_->Plates().size()); ++index) {
            const auto& namedPlate = project_->Plates()[index];
            if (!namedPlate.visible) {
                continue;
            }
            const auto& plate = namedPlate.plate;
            const auto& source = plate.SourceSurface();
            const bool selected = IsSelected(CadSelectionKind::Plate, index);
            QColor fill = namedPlate.material == "brass" ? QColor(188, 156, 72, 150)
                : namedPlate.material == "paper" ? QColor(208, 193, 142, 150)
                : QColor(178, 194, 203, 158);
            if (selected) {
                fill = QColor(230, 159, 0, 168);
            }
            const QColor edge = selected ? QColor("#b66700") : QColor("#586970");
            std::vector<QPainterPath> openingPaths;
            for (const std::string& openingName : namedPlate.openingWireNames) {
                const auto opening = std::find_if(project_->Wires().begin(), project_->Wires().end(), [&](const auto& wire) {
                    return wire.name == openingName;
                });
                if (opening == project_->Wires().end()) {
                    continue;
                }
                QPainterPath path(ProjectPoint(opening->wire.Evaluate(0.0)));
                for (int sample = 1; sample <= 128; ++sample) {
                    path.lineTo(ProjectPoint(opening->wire.Evaluate(static_cast<double>(sample) / 128.0)));
                }
                path.closeSubpath();
                openingPaths.push_back(std::move(path));
            }

            painter.save();
            if (!openingPaths.empty()) {
                QPainterPath clipPath;
                clipPath.addRect(QRectF(rect()));
                for (const QPainterPath& openingPath : openingPaths) {
                    clipPath.addPath(openingPath);
                }
                clipPath.setFillRule(Qt::OddEvenFill);
                painter.setClipPath(clipPath);
            }
            painter.setPen(QPen(edge, selected ? 2.2 : 1.0));

            if (source.Kind() == kachakacha::model::SurfaceKind::Planar) {
                const Vector3 normal = source.Normal(0.5, 0.5);
                const auto drawLayer = [&](double offset, int alpha) {
                    QPainterPath path(ProjectPoint(source.FirstBoundary().Evaluate(0.0) + normal * offset));
                    for (int sample = 1; sample <= 128; ++sample) {
                        path.lineTo(ProjectPoint(source.FirstBoundary().Evaluate(static_cast<double>(sample) / 128.0) + normal * offset));
                    }
                    path.closeSubpath();
                    QColor layerFill = fill;
                    layerFill.setAlpha(alpha);
                    painter.setBrush(layerFill);
                    painter.drawPath(path);
                };
                drawLayer(plate.MinimumOffset(), std::max(40, fill.alpha() - 42));
                drawLayer(plate.MaximumOffset(), fill.alpha());
                painter.setBrush(fill.darker(108));
                for (int sample = 0; sample < 64; ++sample) {
                    const double t0 = static_cast<double>(sample) / 64.0;
                    const double t1 = static_cast<double>(sample + 1) / 64.0;
                    QPolygonF side;
                    side << ProjectPoint(source.FirstBoundary().Evaluate(t0) + normal * plate.MinimumOffset())
                         << ProjectPoint(source.FirstBoundary().Evaluate(t1) + normal * plate.MinimumOffset())
                         << ProjectPoint(source.FirstBoundary().Evaluate(t1) + normal * plate.MaximumOffset())
                         << ProjectPoint(source.FirstBoundary().Evaluate(t0) + normal * plate.MaximumOffset());
                    painter.drawPolygon(side);
                }
            } else {
                for (int layer = 0; layer < 2; ++layer) {
                    QColor layerFill = fill;
                    if (layer == 0) {
                        layerFill.setAlpha(std::max(36, fill.alpha() - 50));
                    }
                    painter.setBrush(layerFill);
                    for (int uIndex = 0; uIndex < 32; ++uIndex) {
                        for (int vIndex = 0; vIndex < 10; ++vIndex) {
                            const double u0 = static_cast<double>(uIndex) / 32.0;
                            const double u1 = static_cast<double>(uIndex + 1) / 32.0;
                            const double v0 = static_cast<double>(vIndex) / 10.0;
                            const double v1 = static_cast<double>(vIndex + 1) / 10.0;
                            QPolygonF patch;
                            patch << ProjectPoint(plate.Evaluate(u0, v0, static_cast<double>(layer)))
                                  << ProjectPoint(plate.Evaluate(u1, v0, static_cast<double>(layer)))
                                  << ProjectPoint(plate.Evaluate(u1, v1, static_cast<double>(layer)))
                                  << ProjectPoint(plate.Evaluate(u0, v1, static_cast<double>(layer)));
                            painter.drawPolygon(patch);
                        }
                    }
                }
                painter.setBrush(fill.darker(108));
                for (int uIndex = 0; uIndex < 32; ++uIndex) {
                    const double u0 = static_cast<double>(uIndex) / 32.0;
                    const double u1 = static_cast<double>(uIndex + 1) / 32.0;
                    for (double v : {0.0, 1.0}) {
                        QPolygonF side;
                        side << ProjectPoint(plate.Evaluate(u0, v, 0.0))
                             << ProjectPoint(plate.Evaluate(u1, v, 0.0))
                             << ProjectPoint(plate.Evaluate(u1, v, 1.0))
                             << ProjectPoint(plate.Evaluate(u0, v, 1.0));
                        painter.drawPolygon(side);
                    }
                }
                const auto& range = plate.Range();
                if (!source.FirstBoundary().IsClosed()
                    || range.minimumU > 1.0e-12 || range.maximumU < 1.0 - 1.0e-12) {
                    for (int vIndex = 0; vIndex < 16; ++vIndex) {
                        const double v0 = static_cast<double>(vIndex) / 16.0;
                        const double v1 = static_cast<double>(vIndex + 1) / 16.0;
                        for (double u : {0.0, 1.0}) {
                            QPolygonF side;
                            side << ProjectPoint(plate.Evaluate(u, v0, 0.0))
                                 << ProjectPoint(plate.Evaluate(u, v1, 0.0))
                                 << ProjectPoint(plate.Evaluate(u, v1, 1.0))
                                 << ProjectPoint(plate.Evaluate(u, v0, 1.0));
                            painter.drawPolygon(side);
                        }
                    }
                }
            }
            if (plateSplitPreviewAxis_.has_value()
                && selection_.kind == CadSelectionKind::Plate && selection_.index == index
                && source.Kind() != kachakacha::model::SurfaceKind::Planar) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor("#b23a48"), 3.0, Qt::DashLine));
                const bool splitU = *plateSplitPreviewAxis_ == kachakacha::model::PlateSplitAxis::U;
                const double firstU = splitU ? plateSplitPreviewParameter_ : 0.0;
                const double firstV = splitU ? 0.0 : plateSplitPreviewParameter_;
                QPainterPath splitPath(ProjectPoint(plate.Evaluate(firstU, firstV, 1.0)));
                for (int sample = 1; sample <= 96; ++sample) {
                    const double position = static_cast<double>(sample) / 96.0;
                    splitPath.lineTo(ProjectPoint(plate.Evaluate(
                        splitU ? plateSplitPreviewParameter_ : position,
                        splitU ? position : plateSplitPreviewParameter_,
                        1.0)));
                }
                painter.drawPath(splitPath);
            }
            painter.restore();
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(edge, selected ? 2.8 : 1.6));
            for (const QPainterPath& openingPath : openingPaths) {
                painter.drawPath(openingPath);
            }
        }

        for (int index = 0; index < static_cast<int>(project_->Wires().size()); ++index) {
            const NamedWire& namedWire = project_->Wires()[index];
            if (!namedWire.visible) {
                continue;
            }
            const bool selected = IsSelected(CadSelectionKind::Wire, index);
            const bool reference = reference_.kind == CadSelectionKind::Wire && reference_.index == index;
            painter.setPen(QPen(
                reference ? QColor("#007f78") : selected ? QColor("#e69f00") : WireColor(namedWire.wire.Kind()),
                reference || selected ? 3.2 : 2.0,
                reference ? Qt::DashDotLine : Qt::SolidLine));
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
            if (reference) {
                painter.setBrush(QColor("#ffffff"));
                painter.setPen(QPen(QColor("#007f78"), 2.0));
                painter.drawRect(QRectF(ProjectPoint(namedWire.wire.Start()) - QPointF(4.0, 4.0), QSizeF(8.0, 8.0)));
                painter.drawRect(QRectF(ProjectPoint(namedWire.wire.End()) - QPointF(4.0, 4.0), QSizeF(8.0, 8.0)));
                painter.drawText(
                    ProjectPoint(namedWire.wire.Evaluate(0.5)) + QPointF(6.0, -7.0),
                    QStringLiteral("基準"));
            }
        }
    }

    if (activePlane_.has_value() && hoverDrawingPoint_.has_value() && tool_ != ViewportTool::Select) {
        painter.setPen(QPen(QColor("#d97706"), 2.0, Qt::DashLine));
        const auto drawPreviewWire = [&](const Wire& wire) {
            QPainterPath path(ProjectPoint(wire.Evaluate(0.0)));
            for (int sample = 1; sample <= 64; ++sample) {
                path.lineTo(ProjectPoint(wire.Evaluate(static_cast<double>(sample) / 64.0)));
            }
            painter.drawPath(path);
        };
        if (!drawingPoints_.empty()) {
            if (tool_ == ViewportTool::DrawLine || tool_ == ViewportTool::DrawPolyline) {
                QPainterPath path(ProjectPoint(drawingPoints_.front()));
                for (std::size_t index = 1; index < drawingPoints_.size(); ++index) {
                    path.lineTo(ProjectPoint(drawingPoints_[index]));
                }
                path.lineTo(ProjectPoint(*hoverDrawingPoint_));
                painter.drawPath(path);
            } else if (tool_ == ViewportTool::DrawRectangle) {
                const auto first = activePlane_->Project(drawingPoints_.front());
                const auto second = activePlane_->Project(*hoverDrawingPoint_);
                QPolygonF rectangle;
                rectangle << ProjectPoint(activePlane_->ToWorld(first.u, first.v))
                          << ProjectPoint(activePlane_->ToWorld(second.u, first.v))
                          << ProjectPoint(activePlane_->ToWorld(second.u, second.v))
                          << ProjectPoint(activePlane_->ToWorld(first.u, second.v));
                painter.drawPolygon(rectangle);
            } else if (tool_ == ViewportTool::DrawCircle) {
                const double radius = (*hoverDrawingPoint_ - drawingPoints_.front()).Length();
                QPainterPath circlePath(ProjectPoint(activePlane_->ToWorld(
                    activePlane_->Project(drawingPoints_.front()).u + radius,
                    activePlane_->Project(drawingPoints_.front()).v)));
                const auto center = activePlane_->Project(drawingPoints_.front());
                for (int sample = 1; sample <= 64; ++sample) {
                    const double angle = static_cast<double>(sample) / 64.0 * 6.28318530717958647692;
                    circlePath.lineTo(ProjectPoint(activePlane_->ToWorld(
                        center.u + std::cos(angle) * radius,
                        center.v + std::sin(angle) * radius)));
                }
                painter.drawPath(circlePath);
            } else if (tool_ == ViewportTool::DrawArc) {
                if (drawingPoints_.size() == 1) {
                    painter.drawLine(ProjectPoint(drawingPoints_.front()), ProjectPoint(*hoverDrawingPoint_));
                } else {
                    try {
                        drawPreviewWire(Wire::CircularArcThroughThreePoints(
                            drawingPoints_[0], drawingPoints_[1], *hoverDrawingPoint_));
                    } catch (const std::exception&) {
                        QPainterPath guide(ProjectPoint(drawingPoints_[0]));
                        guide.lineTo(ProjectPoint(drawingPoints_[1]));
                        guide.lineTo(ProjectPoint(*hoverDrawingPoint_));
                        painter.drawPath(guide);
                    }
                }
            } else if (tool_ == ViewportTool::DrawBezier) {
                QPainterPath guide(ProjectPoint(drawingPoints_.front()));
                for (std::size_t index = 1; index < drawingPoints_.size(); ++index) {
                    guide.lineTo(ProjectPoint(drawingPoints_[index]));
                }
                guide.lineTo(ProjectPoint(*hoverDrawingPoint_));
                painter.drawPath(guide);
                if (drawingPoints_.size() == 3) {
                    try {
                        drawPreviewWire(Wire::CubicBezier(
                            drawingPoints_[0], drawingPoints_[1], drawingPoints_[2], *hoverDrawingPoint_));
                    } catch (const std::exception&) {
                    }
                }
            } else if ((tool_ == ViewportTool::MoveSelection || tool_ == ViewportTool::CopySelection)
                && project_ != nullptr) {
                const Vector3 delta = *hoverDrawingPoint_ - drawingPoints_.front();
                painter.drawLine(ProjectPoint(drawingPoints_.front()), ProjectPoint(*hoverDrawingPoint_));
                for (const CadSelection& selection : selections_) {
                    if (selection.kind != CadSelectionKind::Wire || selection.index < 0
                        || selection.index >= static_cast<int>(project_->Wires().size())) {
                        continue;
                    }
                    drawPreviewWire(project_->Wires()[selection.index].wire.Translated(delta));
                }
            } else if (tool_ == ViewportTool::MirrorSelection && project_ != nullptr) {
                painter.drawLine(ProjectPoint(drawingPoints_.front()), ProjectPoint(*hoverDrawingPoint_));
                const Vector3 direction = *hoverDrawingPoint_ - drawingPoints_.front();
                try {
                    for (const CadSelection& selection : selections_) {
                        if (selection.kind != CadSelectionKind::Wire || selection.index < 0
                            || selection.index >= static_cast<int>(project_->Wires().size())) {
                            continue;
                        }
                        drawPreviewWire(project_->Wires()[selection.index].wire.Mirrored(
                            drawingPoints_.front(), direction, activePlane_->Normal()));
                    }
                } catch (const std::exception&) {
                }
            } else if (tool_ == ViewportTool::RotateSelection && project_ != nullptr) {
                painter.drawLine(ProjectPoint(drawingPoints_.front()), ProjectPoint(*hoverDrawingPoint_));
                if (drawingPoints_.size() == 2) {
                    painter.drawLine(ProjectPoint(drawingPoints_.front()), ProjectPoint(drawingPoints_[1]));
                    const Vector3 from = drawingPoints_[1] - drawingPoints_[0];
                    const Vector3 to = *hoverDrawingPoint_ - drawingPoints_[0];
                    if (from.LengthSquared() > 1.0e-18 && to.LengthSquared() > 1.0e-18) {
                        const Vector3 normal = activePlane_->Normal();
                        const double angle = std::atan2(Dot(Cross(from, to), normal), Dot(from, to));
                        for (const CadSelection& selection : selections_) {
                            if (selection.kind != CadSelectionKind::Wire || selection.index < 0
                                || selection.index >= static_cast<int>(project_->Wires().size())) {
                                continue;
                            }
                            drawPreviewWire(project_->Wires()[selection.index].wire.RotatedAroundAxis(
                                drawingPoints_[0], normal, angle));
                        }
                    }
                }
            }
        }
        painter.setBrush(QColor("#ffffff"));
        painter.setPen(QPen(QColor("#d97706"), 2.0));
        painter.drawEllipse(ProjectPoint(*hoverDrawingPoint_), 4.0, 4.0);
        for (const Vector3& point : drawingPoints_) {
            painter.drawEllipse(ProjectPoint(point), 4.0, 4.0);
        }
    }

    if (tool_ == ViewportTool::SplitWire && splitPreviewParameter_.has_value() && project_ != nullptr) {
        const auto selectedWire = std::find_if(selections_.begin(), selections_.end(), [](const CadSelection& selection) {
            return selection.kind == CadSelectionKind::Wire;
        });
        if (selectedWire != selections_.end() && selectedWire->index >= 0
            && selectedWire->index < static_cast<int>(project_->Wires().size())) {
            const QPointF splitPoint = ProjectPoint(project_->Wires()[selectedWire->index].wire.Evaluate(*splitPreviewParameter_));
            painter.setBrush(QColor("#ffffff"));
            painter.setPen(QPen(QColor("#c0392b"), 2.5));
            painter.drawEllipse(splitPoint, 6.0, 6.0);
            painter.drawLine(splitPoint + QPointF(-9.0, 0.0), splitPoint + QPointF(9.0, 0.0));
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
    QString modeText;
    switch (tool_) {
    case ViewportTool::Select:
        modeText = QStringLiteral("選択");
        break;
    case ViewportTool::DrawLine:
        modeText = QStringLiteral("直線");
        break;
    case ViewportTool::DrawPolyline:
        modeText = QStringLiteral("ポリライン");
        break;
    case ViewportTool::DrawRectangle:
        modeText = QStringLiteral("矩形");
        break;
    case ViewportTool::DrawCircle:
        modeText = QStringLiteral("円");
        break;
    case ViewportTool::DrawArc:
        modeText = QStringLiteral("3点円弧");
        break;
    case ViewportTool::DrawBezier:
        modeText = QStringLiteral("ベジェ曲線");
        break;
    case ViewportTool::MoveSelection:
        modeText = QStringLiteral("移動");
        break;
    case ViewportTool::CopySelection:
        modeText = QStringLiteral("コピー");
        break;
    case ViewportTool::MirrorSelection:
        modeText = QStringLiteral("ミラー複製");
        break;
    case ViewportTool::RotateSelection:
        modeText = QStringLiteral("回転");
        break;
    case ViewportTool::SplitWire:
        modeText = QStringLiteral("分割");
        break;
    }
    if (activePlane_.has_value() && hoverDrawingPoint_.has_value()) {
        const auto coordinates = activePlane_->Project(*hoverDrawingPoint_);
        modeText += QStringLiteral("   U %1 mm   V %2 mm").arg(coordinates.u, 0, 'f', 3).arg(coordinates.v, 0, 'f', 3);
        if (!drawingPoints_.empty() && tool_ != ViewportTool::Select) {
            const auto start = activePlane_->Project(drawingPoints_.front());
            if (tool_ == ViewportTool::DrawLine) {
                modeText += QStringLiteral("   長さ %1 mm").arg((*hoverDrawingPoint_ - drawingPoints_.front()).Length(), 0, 'f', 3);
            } else if (tool_ == ViewportTool::DrawPolyline) {
                double length = 0.0;
                for (std::size_t index = 1; index < drawingPoints_.size(); ++index) {
                    length += (drawingPoints_[index] - drawingPoints_[index - 1]).Length();
                }
                length += (*hoverDrawingPoint_ - drawingPoints_.back()).Length();
                modeText += QStringLiteral("   合計 %1 mm").arg(length, 0, 'f', 3);
            } else if (tool_ == ViewportTool::DrawRectangle) {
                modeText += QStringLiteral("   幅 %1 mm   高さ %2 mm")
                    .arg(std::abs(coordinates.u - start.u), 0, 'f', 3)
                    .arg(std::abs(coordinates.v - start.v), 0, 'f', 3);
            } else if (tool_ == ViewportTool::DrawCircle) {
                modeText += QStringLiteral("   半径 %1 mm").arg((*hoverDrawingPoint_ - drawingPoints_.front()).Length(), 0, 'f', 3);
            } else if (tool_ == ViewportTool::MoveSelection || tool_ == ViewportTool::CopySelection) {
                const Vector3 delta = *hoverDrawingPoint_ - drawingPoints_.front();
                modeText += QStringLiteral("   移動 X %1   Y %2   Z %3 mm")
                    .arg(delta.x, 0, 'f', 3)
                    .arg(delta.y, 0, 'f', 3)
                    .arg(delta.z, 0, 'f', 3);
            } else if (tool_ == ViewportTool::MirrorSelection) {
                modeText += QStringLiteral("   軸長 %1 mm")
                    .arg((*hoverDrawingPoint_ - drawingPoints_.front()).Length(), 0, 'f', 3);
            } else if (tool_ == ViewportTool::RotateSelection && drawingPoints_.size() == 2) {
                const Vector3 from = drawingPoints_[1] - drawingPoints_[0];
                const Vector3 to = *hoverDrawingPoint_ - drawingPoints_[0];
                if (from.LengthSquared() > 1.0e-18 && to.LengthSquared() > 1.0e-18) {
                    const double angle = std::atan2(
                        Dot(Cross(from, to), activePlane_->Normal()), Dot(from, to));
                    modeText += QStringLiteral("   角度 %1 °").arg(angle * 180.0 / 3.14159265358979323846, 0, 'f', 2);
                }
            }
        }
    }
    painter.drawText(QRect(14, 12, std::max(80, width() - 150), 24), Qt::AlignLeft, modeText);

    const ViewCubeGeometry cube = MakeViewCubeGeometry(width());
    const auto drawCubeFace = [&](const QPolygonF& polygon, ViewCubeFace face, const QColor& color, const QString& label) {
        const bool hovered = hoveredViewCubeFace_ == static_cast<int>(face);
        painter.setBrush(hovered ? color.lighter(118) : color);
        painter.setPen(QPen(QColor("#68747c"), hovered ? 2.0 : 1.2));
        painter.drawPolygon(polygon);
        painter.setPen(QColor("#1f2b33"));
        painter.drawText(polygon.boundingRect(), Qt::AlignCenter, label);
    };
    drawCubeFace(cube.top, ViewCubeFace::Top, QColor("#e8edef"), QStringLiteral("上"));
    drawCubeFace(cube.front, ViewCubeFace::Front, QColor("#d6e6e7"), QStringLiteral("正"));
    drawCubeFace(cube.right, ViewCubeFace::Right, QColor("#dfe4e8"), QStringLiteral("右"));
    drawCubeFace(cube.isometric, ViewCubeFace::Isometric, QColor("#f3d9a5"), QStringLiteral("3D"));
}

CadSelection CadViewport::HitTest(QPointF position) const
{
    if (project_ == nullptr) {
        return {};
    }

    double bestDistance = 9.0;
    CadSelection best;
    for (int index = 0; index < static_cast<int>(project_->Wires().size()); ++index) {
        const auto& namedWire = project_->Wires()[index];
        if (!namedWire.visible) {
            continue;
        }
        const auto& wire = namedWire.wire;
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

    for (int index = 0; index < static_cast<int>(project_->Plates().size()); ++index) {
        const auto& namedPlate = project_->Plates()[index];
        if (!namedPlate.visible) {
            continue;
        }
        const auto& plate = namedPlate.plate;
        const auto& surface = plate.SourceSurface();
        bool insideOpening = false;
        for (const std::string& openingName : namedPlate.openingWireNames) {
            const auto opening = std::find_if(project_->Wires().begin(), project_->Wires().end(), [&](const auto& wire) {
                return wire.name == openingName;
            });
            if (opening == project_->Wires().end()) {
                continue;
            }
            QPolygonF openingPolygon;
            for (int sample = 0; sample < 128; ++sample) {
                openingPolygon << ProjectPoint(opening->wire.Evaluate(static_cast<double>(sample) / 128.0));
            }
            if (openingPolygon.containsPoint(position, Qt::OddEvenFill)) {
                insideOpening = true;
                break;
            }
        }
        if (insideOpening) {
            continue;
        }
        if (surface.Kind() == kachakacha::model::SurfaceKind::Planar) {
            const Vector3 normal = surface.Normal(0.5, 0.5);
            QPolygonF polygon;
            for (int sample = 0; sample < 128; ++sample) {
                polygon << ProjectPoint(surface.FirstBoundary().Evaluate(static_cast<double>(sample) / 128.0)
                    + normal * plate.MaximumOffset());
            }
            if (polygon.containsPoint(position, Qt::OddEvenFill)) {
                return {CadSelectionKind::Plate, index};
            }
            continue;
        }
        for (int uIndex = 0; uIndex < 24; ++uIndex) {
            for (int vIndex = 0; vIndex < 8; ++vIndex) {
                const double u0 = static_cast<double>(uIndex) / 24.0;
                const double u1 = static_cast<double>(uIndex + 1) / 24.0;
                const double v0 = static_cast<double>(vIndex) / 8.0;
                const double v1 = static_cast<double>(vIndex + 1) / 8.0;
                QPolygonF patch;
                patch << ProjectPoint(plate.Evaluate(u0, v0, 1.0))
                      << ProjectPoint(plate.Evaluate(u1, v0, 1.0))
                      << ProjectPoint(plate.Evaluate(u1, v1, 1.0))
                      << ProjectPoint(plate.Evaluate(u0, v1, 1.0));
                if (patch.containsPoint(position, Qt::OddEvenFill)) {
                    return {CadSelectionKind::Plate, index};
                }
            }
        }
    }

    for (int index = 0; index < static_cast<int>(project_->Surfaces().size()); ++index) {
        const auto& namedSurface = project_->Surfaces()[index];
        if (!namedSurface.visible) {
            continue;
        }
        const auto& surface = namedSurface.surface;
        if (surface.Kind() == kachakacha::model::SurfaceKind::Planar) {
            QPolygonF polygon;
            for (int sample = 0; sample < 128; ++sample) {
                polygon << ProjectPoint(surface.FirstBoundary().Evaluate(static_cast<double>(sample) / 128.0));
            }
            if (polygon.containsPoint(position, Qt::OddEvenFill)) {
                return {CadSelectionKind::Surface, index};
            }
            continue;
        }
        for (int uIndex = 0; uIndex < 24; ++uIndex) {
            for (int vIndex = 0; vIndex < 8; ++vIndex) {
                const double u0 = static_cast<double>(uIndex) / 24.0;
                const double u1 = static_cast<double>(uIndex + 1) / 24.0;
                const double v0 = static_cast<double>(vIndex) / 8.0;
                const double v1 = static_cast<double>(vIndex + 1) / 8.0;
                QPolygonF patch;
                patch << ProjectPoint(surface.Evaluate(u0, v0))
                      << ProjectPoint(surface.Evaluate(u1, v0))
                      << ProjectPoint(surface.Evaluate(u1, v1))
                      << ProjectPoint(surface.Evaluate(u0, v1));
                if (patch.containsPoint(position, Qt::OddEvenFill)) {
                    return {CadSelectionKind::Surface, index};
                }
            }
        }
    }

    for (int index = 0; index < static_cast<int>(project_->WorkPlanes().size()); ++index) {
        const auto& namedPlane = project_->WorkPlanes()[index];
        if (!namedPlane.visible) {
            continue;
        }
        const auto& plane = namedPlane.plane;
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
    if (event->button() == Qt::LeftButton
        && MakeViewCubeGeometry(width()).bounds.contains(event->position())) {
        viewCubeInteraction_ = true;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (tool_ != ViewportTool::Select
        && tool_ != ViewportTool::SplitWire
        && event->button() == Qt::LeftButton
        && activePlane_.has_value()
        && drawingPoints_.empty()) {
        const auto point = PointOnActivePlane(event->position());
        if (point.has_value()) {
            CommitDrawingPoint(SnapPoint(*point, event->position()));
        }
    }
}

void CadViewport::mouseMoveEvent(QMouseEvent* event)
{
    if (viewCubeInteraction_ && dragButton_ == Qt::LeftButton) {
        const QPoint delta = event->position().toPoint() - lastMousePosition_;
        if (delta.manhattanLength() > 1) {
            mouseMoved_ = true;
        }
        alignedViewBasis_.reset();
        yawRadians_ += delta.x() * 0.008;
        pitchRadians_ = std::clamp(pitchRadians_ - delta.y() * 0.008, -1.45, 1.45);
        lastMousePosition_ = event->position().toPoint();
        update();
        event->accept();
        return;
    }

    const int hoveredFace = static_cast<int>(HitViewCube(MakeViewCubeGeometry(width()), event->position()));
    if (hoveredFace != hoveredViewCubeFace_) {
        hoveredViewCubeFace_ = hoveredFace;
        setCursor(hoveredFace == static_cast<int>(ViewCubeFace::None)
                ? (tool_ == ViewportTool::Select ? Qt::ArrowCursor : Qt::CrossCursor)
                : Qt::PointingHandCursor);
        update();
    }

    if (tool_ == ViewportTool::SplitWire) {
        splitPreviewParameter_.reset();
        const auto selectedWire = std::find_if(selections_.begin(), selections_.end(), [](const CadSelection& selection) {
            return selection.kind == CadSelectionKind::Wire;
        });
        if (selectedWire != selections_.end()) {
            splitPreviewParameter_ = NearestWireParameter(selectedWire->index, event->position());
        }
        update();
    } else if (tool_ != ViewportTool::Select && activePlane_.has_value()) {
        const auto point = PointOnActivePlane(event->position());
        hoverDrawingPoint_ = point.has_value()
            ? std::optional<Vector3>(ApplyDrawingConstraint(
                  SnapPoint(*point, event->position()), event->modifiers()))
            : std::nullopt;
        NotifyDrawingState();
        update();
    }

    if (dragButton_ == Qt::NoButton) {
        return;
    }
    const QPoint delta = event->position().toPoint() - lastMousePosition_;
    if (delta.manhattanLength() > 1) {
        mouseMoved_ = true;
    }
    if (dragButton_ == Qt::LeftButton && tool_ == ViewportTool::Select) {
        alignedViewBasis_.reset();
        yawRadians_ += delta.x() * 0.008;
        pitchRadians_ = std::clamp(pitchRadians_ - delta.y() * 0.008, -1.45, 1.45);
    } else if (dragButton_ == Qt::RightButton || dragButton_ == Qt::MiddleButton) {
        const auto basis = CurrentViewBasis();
        const Vector3& right = basis[1];
        const Vector3& up = basis[2];
        target_ = target_ - right * (delta.x() / pixelsPerMillimeter_) + up * (delta.y() / pixelsPerMillimeter_);
    }
    lastMousePosition_ = event->position().toPoint();
    update();
}

void CadViewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (viewCubeInteraction_) {
        if (event->button() == Qt::LeftButton && !mouseMoved_) {
            const ViewCubeFace face = HitViewCube(MakeViewCubeGeometry(width()), event->position());
            const Vector3 viewTarget = target_;
            switch (face) {
            case ViewCubeFace::Top:
                AlignToWorkPlane(kachakacha::model::WorkPlane::FromPointNormal(
                    {}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}));
                break;
            case ViewCubeFace::Front:
                AlignToWorkPlane(kachakacha::model::WorkPlane::FromPointNormal(
                    {}, {0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}));
                break;
            case ViewCubeFace::Right:
                AlignToWorkPlane(kachakacha::model::WorkPlane::FromPointNormal(
                    {}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}));
                break;
            case ViewCubeFace::Isometric:
                SetIsometricView();
                break;
            case ViewCubeFace::None:
                break;
            }
            if (face == ViewCubeFace::Top || face == ViewCubeFace::Front || face == ViewCubeFace::Right) {
                target_ = viewTarget;
                update();
            }
        }
        viewCubeInteraction_ = false;
        dragButton_ = Qt::NoButton;
        setCursor(hoveredViewCubeFace_ == static_cast<int>(ViewCubeFace::None)
                ? (tool_ == ViewportTool::Select ? Qt::ArrowCursor : Qt::CrossCursor)
                : Qt::PointingHandCursor);
        event->accept();
        return;
    }

    if (tool_ == ViewportTool::SplitWire) {
        if (event->button() == Qt::LeftButton && !mouseMoved_ && splitPreviewParameter_.has_value()) {
            const auto selectedWire = std::find_if(selections_.begin(), selections_.end(), [](const CadSelection& selection) {
                return selection.kind == CadSelectionKind::Wire;
            });
            if (selectedWire != selections_.end() && splitRequested_) {
                splitRequested_(selectedWire->index, *splitPreviewParameter_);
            }
        } else if (event->button() == Qt::RightButton && !mouseMoved_) {
            CancelDrawing();
        }
        dragButton_ = Qt::NoButton;
        return;
    }

    if (tool_ != ViewportTool::Select) {
        if (event->button() == Qt::LeftButton && activePlane_.has_value()) {
            const auto point = PointOnActivePlane(event->position());
            if (point.has_value()) {
                CommitDrawingPoint(ApplyDrawingConstraint(
                    SnapPoint(*point, event->position()), event->modifiers()));
            }
        } else if (!mouseMoved_ && event->button() == Qt::RightButton) {
            if (tool_ == ViewportTool::DrawPolyline && drawingPoints_.size() >= 2) {
                FinishDrawing();
            } else {
                CancelDrawing();
            }
        }
        dragButton_ = Qt::NoButton;
        return;
    }

    if (event->button() == Qt::LeftButton && !mouseMoved_) {
        const CadSelection hit = HitTest(event->position());
        const bool extendSelection = event->modifiers().testFlag(Qt::ControlModifier)
            || event->modifiers().testFlag(Qt::ShiftModifier);
        if (!extendSelection) {
            selections_.clear();
            if (hit.kind != CadSelectionKind::None) {
                selections_.push_back(hit);
            }
        } else if (hit.kind != CadSelectionKind::None) {
            const auto existing = std::find_if(selections_.begin(), selections_.end(), [&](const CadSelection& selection) {
                return selection.kind == hit.kind && selection.index == hit.index;
            });
            if (existing == selections_.end()) {
                selections_.push_back(hit);
            } else {
                selections_.erase(existing);
            }
        }
        selection_ = selections_.empty() ? CadSelection{} : selections_.back();
        NotifySelection();
        update();
    }
    dragButton_ = Qt::NoButton;
}

void CadViewport::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        CancelDrawing();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        FinishDrawing();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
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
        selectionChanged_(selections_);
    }
}

void CadViewport::NotifyDrawingState()
{
    if (drawingStateChanged_) {
        drawingStateChanged_(tool_, drawingPoints_.size());
    }
}
