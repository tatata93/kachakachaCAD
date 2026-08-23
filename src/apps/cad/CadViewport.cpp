#include "CadViewport.h"

#include <QKeyEvent>
#include <QFontMetrics>
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
    Selection,
};

struct ViewCubeGeometry {
    QPolygonF top;
    QPolygonF front;
    QPolygonF right;
    QPolygonF isometric;
    QRectF selection;
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
        QRectF(centerX - 50.0, 116.0, 100.0, 28.0),
        QRectF(centerX - 54.0, 10.0, 108.0, 140.0),
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
    if (cube.selection.contains(position)) {
        return ViewCubeFace::Selection;
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
    case WireKind::CubicBSpline:
        return QColor("#4355a5");
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
    hoveredSelection_ = {};
    hoveredWirePoint_.reset();
    hoveredWireParameter_.reset();
    coincidencePicks_.clear();
    measurementPicks_.clear();
    measurementOverlayFirst_.reset();
    measurementOverlaySecond_.reset();
    measurementOverlayText_.clear();
    referenceDimensionOverlays_.clear();
    if (measurementChanged_) {
        measurementChanged_(measurementPicks_);
    }
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

void CadViewport::SetSplineCreatedCallback(std::function<void(const std::vector<Vector3>&)> callback)
{
    splineCreated_ = std::move(callback);
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

void CadViewport::SetCoincidenceRequestedCallback(
    std::function<void(WireEndpointPick, WireEndpointPick)> callback)
{
    coincidenceRequested_ = std::move(callback);
}

void CadViewport::SetTangentRequestedCallback(
    std::function<void(WireEndpointPick, WireEndpointPick)> callback)
{
    tangentRequested_ = std::move(callback);
}

void CadViewport::SetCurvatureRequestedCallback(
    std::function<void(WireEndpointPick, WireEndpointPick)> callback)
{
    curvatureRequested_ = std::move(callback);
}

void CadViewport::SetMeasurementChangedCallback(
    std::function<void(const std::vector<MeasurementPick>&)> callback)
{
    measurementChanged_ = std::move(callback);
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
    if (tool_ != tool) {
        coincidencePicks_.clear();
    }
    tool_ = tool;
    CancelDrawing();
    setCursor(hoveredSelection_.kind == CadSelectionKind::Wire
            && (tool_ == ViewportTool::Select || tool_ == ViewportTool::Measure
                || tool_ == ViewportTool::SplitWire || tool_ == ViewportTool::Coincident
                || tool_ == ViewportTool::Tangent || tool_ == ViewportTool::Curvature)
        ? Qt::PointingHandCursor
        : tool_ == ViewportTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
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

void CadViewport::SetWireOffsetPreview(std::vector<Wire> wires)
{
    wireOffsetPreviews_ = std::move(wires);
    update();
}

void CadViewport::SetMeasurementMode(MeasurementMode mode)
{
    measurementMode_ = mode;
    ClearMeasurement();
}

void CadViewport::ClearMeasurement()
{
    measurementPicks_.clear();
    measurementOverlayFirst_.reset();
    measurementOverlaySecond_.reset();
    measurementOverlayText_.clear();
    if (measurementChanged_) {
        measurementChanged_(measurementPicks_);
    }
    update();
}

void CadViewport::ClearCoincidencePicks()
{
    coincidencePicks_.clear();
    NotifyDrawingState();
    update();
}

void CadViewport::SetMeasurementOverlay(
    std::optional<Vector3> firstPoint,
    std::optional<Vector3> secondPoint,
    QString text)
{
    measurementOverlayFirst_ = firstPoint;
    measurementOverlaySecond_ = secondPoint;
    measurementOverlayText_ = std::move(text);
    update();
}

void CadViewport::SetReferenceDimensionOverlays(std::vector<ReferenceDimensionOverlay> overlays)
{
    referenceDimensionOverlays_ = std::move(overlays);
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

bool CadViewport::AlignToSelection()
{
    if (project_ == nullptr || selection_.kind == CadSelectionKind::None) {
        return false;
    }

    std::vector<Vector3> points;
    Vector3 origin;
    Vector3 normal;
    Vector3 uAxisHint;
    const Vector3 previousViewDirection = ViewDirection();

    const auto sampleWire = [&](const Wire& wire) {
        const int samples = wire.Kind() == WireKind::Line ? 1 : 64;
        for (int sample = 0; sample <= samples; ++sample) {
            points.push_back(wire.Evaluate(static_cast<double>(sample) / samples));
        }
    };
    const auto sampleSurface = [&](const kachakacha::model::Surface& surface) {
        for (int uIndex = 0; uIndex <= 24; ++uIndex) {
            for (int vIndex = 0; vIndex <= 8; ++vIndex) {
                points.push_back(surface.Evaluate(
                    static_cast<double>(uIndex) / 24.0,
                    static_cast<double>(vIndex) / 8.0));
            }
        }
    };
    const auto surfaceUAxis = [](const kachakacha::model::Surface& surface, double u, double v) {
        const double before = std::max(0.0, u - 0.01);
        const double after = std::min(1.0, u + 0.01);
        Vector3 tangent = surface.Evaluate(after, v) - surface.Evaluate(before, v);
        if (tangent.LengthSquared() <= 1.0e-18) {
            const double vBefore = std::max(0.0, v - 0.01);
            const double vAfter = std::min(1.0, v + 0.01);
            tangent = surface.Evaluate(u, vAfter) - surface.Evaluate(u, vBefore);
        }
        return tangent;
    };

    try {
        if (selection_.kind == CadSelectionKind::WorkPlane
            && selection_.index >= 0
            && selection_.index < static_cast<int>(project_->WorkPlanes().size())) {
            const auto& plane = project_->WorkPlanes()[selection_.index].plane;
            origin = plane.Origin();
            normal = plane.Normal();
            uAxisHint = plane.UAxis();
            points = {
                origin + plane.UAxis() * kPlaneHalfSize + plane.VAxis() * kPlaneHalfSize,
                origin + plane.UAxis() * kPlaneHalfSize - plane.VAxis() * kPlaneHalfSize,
                origin - plane.UAxis() * kPlaneHalfSize + plane.VAxis() * kPlaneHalfSize,
                origin - plane.UAxis() * kPlaneHalfSize - plane.VAxis() * kPlaneHalfSize,
            };
        } else if (selection_.kind == CadSelectionKind::Surface
            && selection_.index >= 0
            && selection_.index < static_cast<int>(project_->Surfaces().size())) {
            const auto& surface = project_->Surfaces()[selection_.index].surface;
            origin = surface.Evaluate(0.5, 0.5);
            normal = surface.Normal(0.5, 0.5);
            uAxisHint = surfaceUAxis(surface, 0.5, 0.5);
            sampleSurface(surface);
        } else if (selection_.kind == CadSelectionKind::Plate
            && selection_.index >= 0
            && selection_.index < static_cast<int>(project_->Plates().size())) {
            const auto& plate = project_->Plates()[selection_.index].plate;
            const double sourceU = plate.SourceU(0.5);
            const double sourceV = plate.SourceV(0.5);
            origin = plate.Evaluate(0.5, 0.5, 0.5);
            normal = plate.SourceSurface().Normal(sourceU, sourceV);
            uAxisHint = surfaceUAxis(plate.SourceSurface(), sourceU, sourceV);
            for (int uIndex = 0; uIndex <= 24; ++uIndex) {
                for (int vIndex = 0; vIndex <= 8; ++vIndex) {
                    const double u = static_cast<double>(uIndex) / 24.0;
                    const double v = static_cast<double>(vIndex) / 8.0;
                    points.push_back(plate.Evaluate(u, v, 0.0));
                    points.push_back(plate.Evaluate(u, v, 1.0));
                }
            }
        } else if (selection_.kind == CadSelectionKind::Wire
            && selection_.index >= 0
            && selection_.index < static_cast<int>(project_->Wires().size())) {
            const auto& namedWire = project_->Wires()[selection_.index];
            const Wire& wire = namedWire.wire;
            sampleWire(wire);
            for (const Vector3& point : points) {
                origin = origin + point;
            }
            origin = origin / static_cast<double>(points.size());

            bool usedSourcePlane = false;
            if (namedWire.metadata.sourcePlaneName.has_value()) {
                const auto sourcePlane = project_->FindWorkPlane(*namedWire.metadata.sourcePlaneName);
                if (sourcePlane.has_value()) {
                    const bool liesOnPlane = std::all_of(points.begin(), points.end(), [&](Vector3 point) {
                        return std::abs(sourcePlane->Project(point).w) <= 1.0e-6;
                    });
                    if (liesOnPlane) {
                        normal = sourcePlane->Normal();
                        uAxisHint = sourcePlane->UAxis();
                        usedSourcePlane = true;
                    }
                }
            }
            if (!usedSourcePlane && namedWire.projection.has_value()) {
                const auto targetSurface = project_->FindSurface(namedWire.projection->targetSurfaceName);
                if (targetSurface.has_value()) {
                    normal = targetSurface->Normal(0.5, 0.5);
                    uAxisHint = surfaceUAxis(*targetSurface, 0.5, 0.5);
                    usedSourcePlane = true;
                }
            }
            if (!usedSourcePlane
                && (wire.Kind() == WireKind::Circle || wire.Kind() == WireKind::CircularArc)) {
                const auto arc = wire.ArcData();
                normal = Cross(arc.uAxis, arc.vAxis);
                uAxisHint = arc.uAxis;
                usedSourcePlane = true;
            }
            if (!usedSourcePlane) {
                double bestCrossLengthSquared = 0.0;
                for (std::size_t first = 0; first < points.size(); ++first) {
                    for (std::size_t second = first + 1; second < points.size(); ++second) {
                        const Vector3 candidate = Cross(points[first] - origin, points[second] - origin);
                        if (candidate.LengthSquared() > bestCrossLengthSquared) {
                            bestCrossLengthSquared = candidate.LengthSquared();
                            normal = candidate;
                        }
                    }
                }
                uAxisHint = wire.End() - wire.Start();
                if (uAxisHint.LengthSquared() <= 1.0e-18 && points.size() > 1) {
                    uAxisHint = points[1] - points[0];
                }
                if (bestCrossLengthSquared <= 1.0e-18) {
                    const Vector3 lineDirection = uAxisHint.Normalized();
                    normal = previousViewDirection
                        - lineDirection * Dot(previousViewDirection, lineDirection);
                    if (normal.LengthSquared() <= 1.0e-18) {
                        const Vector3 fallback = std::abs(lineDirection.z) < 0.9
                            ? Vector3{0.0, 0.0, 1.0}
                            : Vector3{0.0, 1.0, 0.0};
                        normal = fallback - lineDirection * Dot(fallback, lineDirection);
                    }
                }
            }
        } else {
            return false;
        }

        if (points.empty() || normal.LengthSquared() <= 1.0e-18
            || uAxisHint.LengthSquared() <= 1.0e-18) {
            return false;
        }
        normal = normal.Normalized();
        if (Dot(normal, previousViewDirection) < 0.0) {
            normal = -normal;
        }
        AlignToWorkPlane(kachakacha::model::WorkPlane::FromPointNormal(origin, normal, uAxisHint));

        const auto basis = CurrentViewBasis();
        Vector3 minimum{
            Dot(points.front(), basis[0]),
            Dot(points.front(), basis[1]),
            Dot(points.front(), basis[2])};
        Vector3 maximum = minimum;
        for (const Vector3& point : points) {
            const Vector3 coordinates{
                Dot(point, basis[0]),
                Dot(point, basis[1]),
                Dot(point, basis[2])};
            minimum.x = std::min(minimum.x, coordinates.x);
            minimum.y = std::min(minimum.y, coordinates.y);
            minimum.z = std::min(minimum.z, coordinates.z);
            maximum.x = std::max(maximum.x, coordinates.x);
            maximum.y = std::max(maximum.y, coordinates.y);
            maximum.z = std::max(maximum.z, coordinates.z);
        }
        const Vector3 middle = (minimum + maximum) * 0.5;
        target_ = basis[0] * middle.x + basis[1] * middle.y + basis[2] * middle.z;
        const double span = std::max({maximum.y - minimum.y, maximum.z - minimum.z, 10.0});
        const double available = std::max(160, std::min(width() - 150, height() - 80));
        pixelsPerMillimeter_ = std::clamp(available / (span * 1.25), 1.0, 80.0);
        update();
        return true;
    } catch (const std::exception&) {
        return false;
    }
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
    } else if (tool_ == ViewportTool::DrawSpline && drawingPoints_.size() >= 4) {
        const std::vector<Vector3> points = drawingPoints_;
        drawingPoints_.clear();
        if (splineCreated_) {
            splineCreated_(points);
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

    const Vector3 anchor = tool_ == ViewportTool::DrawPolyline || tool_ == ViewportTool::DrawSpline
        ? drawingPoints_.back()
        : drawingPoints_.front();
    const auto start = activePlane_->Project(anchor);
    const auto hover = activePlane_->Project(*hoverDrawingPoint_);
    const double deltaU = hover.u - start.u;
    const double deltaV = hover.v - start.v;

    if (tool_ == ViewportTool::DrawLine || tool_ == ViewportTool::DrawPolyline
        || tool_ == ViewportTool::DrawSpline) {
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

    if (tool_ == ViewportTool::DrawLine || tool_ == ViewportTool::DrawPolyline
        || tool_ == ViewportTool::DrawSpline) {
        const Vector3 anchor = tool_ == ViewportTool::DrawPolyline || tool_ == ViewportTool::DrawSpline
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

std::optional<double> CadViewport::NearestWireParameter(
    int wireIndex,
    QPointF position,
    double maximumDistance,
    bool allowEndpoints) const
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
    if (bestDistance >= maximumDistance
        || (!allowEndpoints && (bestParameter <= 1.0e-6 || bestParameter >= 1.0 - 1.0e-6))) {
        return std::nullopt;
    }
    return bestParameter;
}

std::optional<WireEndpointPick> CadViewport::NearestWireEndpoint(
    QPointF position,
    double maximumDistance) const
{
    if (project_ == nullptr || !std::isfinite(maximumDistance) || maximumDistance <= 0.0) {
        return std::nullopt;
    }
    double bestDistance = maximumDistance;
    std::optional<WireEndpointPick> best;
    for (int index = 0; index < static_cast<int>(project_->Wires().size()); ++index) {
        const NamedWire& namedWire = project_->Wires()[index];
        if (!namedWire.visible || namedWire.projection.has_value() || namedWire.wire.IsClosed()) {
            continue;
        }
        if (tool_ == ViewportTool::Tangent && !coincidencePicks_.empty()
            && namedWire.wire.Kind() != WireKind::CubicBezier
            && namedWire.wire.Kind() != WireKind::CubicBSpline
            && namedWire.wire.Kind() != WireKind::CircularArc) {
            continue;
        }
        if (tool_ == ViewportTool::Curvature && !coincidencePicks_.empty()
            && namedWire.wire.Kind() != WireKind::CubicBezier) {
            continue;
        }
        for (const auto endpoint : {kachakacha::model::WireEndpoint::Start, kachakacha::model::WireEndpoint::End}) {
            const Vector3 point = endpoint == kachakacha::model::WireEndpoint::Start
                ? namedWire.wire.Start()
                : namedWire.wire.End();
            const double distance = QLineF(position, ProjectPoint(point)).length();
            if (distance < bestDistance) {
                bestDistance = distance;
                best = WireEndpointPick{index, endpoint, point};
            }
        }
    }
    return best;
}

void CadViewport::CommitMeasurementPick(QPointF position)
{
    if (project_ == nullptr) {
        return;
    }

    MeasurementPick pick;
    const CadSelection hit = HitTest(position);
    if (measurementMode_ == MeasurementMode::Elements) {
        if (hit.kind == CadSelectionKind::Wire) {
            const auto parameter = NearestWireParameter(hit.index, position, 14.0, true);
            if (!parameter.has_value()) {
                return;
            }
            pick = {
                MeasurementPickKind::Wire,
                hit.index,
                project_->Wires()[hit.index].wire.Evaluate(*parameter),
                *parameter,
            };
        } else if (hit.kind == CadSelectionKind::WorkPlane) {
            pick = {
                MeasurementPickKind::WorkPlane,
                hit.index,
                project_->WorkPlanes()[hit.index].plane.Origin(),
                0.0,
            };
        } else {
            const auto point = PointOnActivePlane(position);
            if (!point.has_value()) {
                return;
            }
            pick = {MeasurementPickKind::Point, -1, SnapPoint(*point, position), 0.0};
        }
    } else {
        if (hit.kind == CadSelectionKind::Wire) {
            const auto parameter = NearestWireParameter(hit.index, position, 14.0, true);
            if (parameter.has_value()) {
                pick = {
                    MeasurementPickKind::Point,
                    hit.index,
                    project_->Wires()[hit.index].wire.Evaluate(*parameter),
                    *parameter,
                };
            } else {
                return;
            }
        } else {
            const auto point = PointOnActivePlane(position);
            if (!point.has_value()) {
                return;
            }
            pick = {MeasurementPickKind::Point, -1, SnapPoint(*point, position), 0.0};
        }
    }

    if (measurementPicks_.size() >= 2
        || (measurementMode_ == MeasurementMode::Elements
            && measurementPicks_.size() == 1
            && measurementPicks_.front().kind == pick.kind
            && measurementPicks_.front().index == pick.index)) {
        measurementPicks_.clear();
        measurementOverlayFirst_.reset();
        measurementOverlaySecond_.reset();
        measurementOverlayText_.clear();
    }
    measurementPicks_.push_back(pick);
    if (measurementChanged_) {
        measurementChanged_(measurementPicks_);
    }
    update();
}

void CadViewport::CommitCoincidencePick(QPointF position)
{
    const std::optional<WireEndpointPick> pick = NearestWireEndpoint(position, 14.0);
    if (!pick.has_value()) {
        return;
    }
    if (!coincidencePicks_.empty()
        && coincidencePicks_.front().wireIndex == pick->wireIndex) {
        return;
    }
    coincidencePicks_.push_back(*pick);
    if (coincidencePicks_.size() == 2) {
        const WireEndpointPick anchor = coincidencePicks_[0];
        const WireEndpointPick follower = coincidencePicks_[1];
        coincidencePicks_.clear();
        if (tool_ == ViewportTool::Curvature && curvatureRequested_) {
            curvatureRequested_(anchor, follower);
        } else if (tool_ == ViewportTool::Tangent && tangentRequested_) {
            tangentRequested_(anchor, follower);
        } else if (coincidenceRequested_) {
            coincidenceRequested_(anchor, follower);
        }
    }
    NotifyDrawingState();
    update();
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

    const Vector3 anchor = tool_ == ViewportTool::DrawPolyline || tool_ == ViewportTool::DrawSpline
        ? drawingPoints_.back()
        : drawingPoints_.front();
    const auto start = activePlane_->Project(anchor);
    auto target = activePlane_->Project(point);
    const double deltaU = target.u - start.u;
    const double deltaV = target.v - start.v;

    if (tool_ == ViewportTool::DrawLine || tool_ == ViewportTool::DrawPolyline
        || tool_ == ViewportTool::DrawSpline) {
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

void CadViewport::UpdateHover(QPointF position)
{
    hoverScreenPosition_ = position.toPoint();
    hoveredSelection_ = {};
    hoveredWirePoint_.reset();
    hoveredWireParameter_.reset();

    if (project_ != nullptr
        && (tool_ == ViewportTool::Coincident || tool_ == ViewportTool::Tangent
            || tool_ == ViewportTool::Curvature)) {
        const std::optional<WireEndpointPick> endpoint = NearestWireEndpoint(position, 8.0);
        if (endpoint.has_value()) {
            hoveredSelection_ = {CadSelectionKind::Wire, endpoint->wireIndex};
            hoveredWirePoint_ = endpoint->point;
        }
    } else if (project_ != nullptr) {
        double bestPointDistance = 8.0;
        int bestPointWire = -1;
        Vector3 bestPoint;
        const auto considerPoint = [&](int wireIndex, Vector3 point) {
            const double distance = QLineF(position, ProjectPoint(point)).length();
            if (distance < bestPointDistance) {
                bestPointDistance = distance;
                bestPointWire = wireIndex;
                bestPoint = point;
            }
        };

        for (int index = 0; index < static_cast<int>(project_->Wires().size()); ++index) {
            const NamedWire& namedWire = project_->Wires()[index];
            if (!namedWire.visible) {
                continue;
            }
            considerPoint(index, namedWire.wire.Start());
            if (!namedWire.wire.IsClosed()) {
                considerPoint(index, namedWire.wire.End());
            }
            if (IsSelected(CadSelectionKind::Wire, index)) {
                for (const Vector3& point : namedWire.wire.ControlPoints()) {
                    considerPoint(index, point);
                }
            }
        }

        if (bestPointWire >= 0) {
            hoveredSelection_ = {CadSelectionKind::Wire, bestPointWire};
            hoveredWirePoint_ = bestPoint;
        } else {
            const CadSelection hit = HitTestWire(position, 9.0);
            if (hit.kind == CadSelectionKind::Wire) {
                hoveredSelection_ = hit;
                hoveredWireParameter_ = NearestWireParameter(hit.index, position, 12.0, true);
            }
        }
    }

    setCursor(hoveredSelection_.kind == CadSelectionKind::Wire
            && (tool_ == ViewportTool::Select || tool_ == ViewportTool::Measure
                || tool_ == ViewportTool::SplitWire || tool_ == ViewportTool::Coincident
                || tool_ == ViewportTool::Tangent || tool_ == ViewportTool::Curvature)
        ? Qt::PointingHandCursor
        : tool_ == ViewportTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

void CadViewport::ClearHover()
{
    if (hoveredSelection_.kind == CadSelectionKind::None
        && !hoveredWirePoint_.has_value() && !hoveredWireParameter_.has_value()) {
        return;
    }
    hoveredSelection_ = {};
    hoveredWirePoint_.reset();
    hoveredWireParameter_.reset();
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
            const bool hovered = hoveredSelection_.kind == CadSelectionKind::Wire
                && hoveredSelection_.index == index;
            QPainterPath path(ProjectPoint(namedWire.wire.Evaluate(0.0)));
            const int samples = namedWire.wire.Kind() == WireKind::Line ? 1 : 64;
            for (int sample = 1; sample <= samples; ++sample) {
                path.lineTo(ProjectPoint(namedWire.wire.Evaluate(static_cast<double>(sample) / samples)));
            }

            if (hovered) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(17, 132, 160, 92), selected ? 7.5 : 7.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawPath(path);
            }
            const QColor wireColor = reference ? QColor("#007f78")
                : selected ? QColor("#e69200")
                : hovered ? QColor("#087f9c")
                : namedWire.metadata.construction ? QColor("#697984")
                : WireColor(namedWire.wire.Kind());
            const Qt::PenStyle wireStyle = reference ? Qt::DashDotLine
                : namedWire.metadata.construction ? Qt::DashLine
                : Qt::SolidLine;
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(
                wireColor,
                hovered ? (selected || reference ? 4.2 : 3.4) : reference || selected ? 3.2 : namedWire.metadata.construction ? 1.7 : 2.0,
                wireStyle,
                Qt::RoundCap,
                Qt::RoundJoin));
            painter.drawPath(path);

            if (selected) {
                if (namedWire.wire.Kind() == WireKind::CubicBezier
                    || namedWire.wire.Kind() == WireKind::CubicBSpline) {
                    QPainterPath controlPath(ProjectPoint(namedWire.wire.ControlPoints().front()));
                    for (std::size_t controlIndex = 1;
                         controlIndex < namedWire.wire.ControlPoints().size(); ++controlIndex) {
                        controlPath.lineTo(ProjectPoint(namedWire.wire.ControlPoints()[controlIndex]));
                    }
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(QColor("#79838a"), 1.2, Qt::DashLine));
                    painter.drawPath(controlPath);
                }
                painter.setBrush(QColor("#ffffff"));
                painter.setPen(QPen(QColor("#e69f00"), 2.0));
                for (const Vector3& point : namedWire.wire.ControlPoints()) {
                    painter.drawEllipse(ProjectPoint(point), 4.0, 4.0);
                }
                const auto drawEndpoint = [&](Vector3 point) {
                    const QPointF screenPoint = ProjectPoint(point);
                    painter.drawRect(QRectF(screenPoint - QPointF(4.5, 4.5), QSizeF(9.0, 9.0)));
                };
                drawEndpoint(namedWire.wire.Start());
                if (!namedWire.wire.IsClosed()) {
                    drawEndpoint(namedWire.wire.End());
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

        painter.save();
        const auto findWire = [this](const std::string& name) {
            return std::find_if(project_->Wires().begin(), project_->Wires().end(), [&](const NamedWire& wire) {
                return wire.name == name;
            });
        };
        const auto endpointPoint = [](const NamedWire& wire, kachakacha::model::WireEndpoint endpoint) {
            return endpoint == kachakacha::model::WireEndpoint::Start
                ? wire.wire.Start()
                : wire.wire.End();
        };
        for (const auto& constraint : project_->CoincidentConstraints()) {
            const auto anchorWire = findWire(constraint.anchor.wireName);
            const auto followerWire = findWire(constraint.follower.wireName);
            if (anchorWire == project_->Wires().end() || followerWire == project_->Wires().end()
                || !anchorWire->visible || !followerWire->visible) {
                continue;
            }
            const Vector3 point = endpointPoint(*anchorWire, constraint.anchor.endpoint);
            const QPointF screenPoint = ProjectPoint(point);
            const int anchorIndex = static_cast<int>(std::distance(project_->Wires().begin(), anchorWire));
            const int followerIndex = static_cast<int>(std::distance(project_->Wires().begin(), followerWire));
            const auto smoothConstraint = std::find_if(
                project_->TangentConstraints().begin(), project_->TangentConstraints().end(),
                [&](const auto& candidate) {
                    return candidate.anchor.wireName == constraint.anchor.wireName
                        && candidate.anchor.endpoint == constraint.anchor.endpoint
                        && candidate.follower.wireName == constraint.follower.wireName
                        && candidate.follower.endpoint == constraint.follower.endpoint;
                });
            const bool tangent = smoothConstraint != project_->TangentConstraints().end();
            const bool curvature = tangent
                && smoothConstraint->continuity == kachakacha::model::WireContinuity::G2Curvature;
            const bool emphasized = tool_ == ViewportTool::Coincident || tool_ == ViewportTool::Tangent
                || tool_ == ViewportTool::Curvature
                || IsSelected(CadSelectionKind::Wire, anchorIndex)
                || IsSelected(CadSelectionKind::Wire, followerIndex);
            const QColor markerColor(curvature ? "#7a4c9e" : tangent ? "#2f7d4a" : "#0b7f78");
            painter.setBrush(QColor(255, 255, 255, 235));
            painter.setPen(QPen(markerColor, emphasized ? 2.6 : 1.8));
            painter.drawEllipse(screenPoint + QPointF(-3.0, 0.0), 4.0, 4.0);
            painter.drawEllipse(screenPoint + QPointF(3.0, 0.0), 4.0, 4.0);
            if (tangent) {
                painter.drawLine(screenPoint + QPointF(-8.0, 6.0), screenPoint + QPointF(8.0, 6.0));
                if (curvature) {
                    painter.drawLine(screenPoint + QPointF(-8.0, 10.0), screenPoint + QPointF(8.0, 10.0));
                }
            }
            if (emphasized) {
                painter.setPen(markerColor.darker(125));
                painter.drawText(
                    screenPoint + QPointF(9.0, -8.0),
                    curvature ? QStringLiteral("G2")
                              : tangent ? QStringLiteral("G1") : QStringLiteral("一致"));
            }
        }
        if (!coincidencePicks_.empty()) {
            const QPointF screenPoint = ProjectPoint(coincidencePicks_.front().point);
            painter.setBrush(QColor("#ffffff"));
            painter.setPen(QPen(QColor("#0b7f78"), 2.8));
            painter.drawRect(QRectF(screenPoint - QPointF(6.0, 6.0), QSizeF(12.0, 12.0)));
            painter.drawText(screenPoint + QPointF(10.0, -9.0), QStringLiteral("固定側"));
        }
        painter.restore();

        if (hoveredSelection_.kind == CadSelectionKind::Wire
            && hoveredSelection_.index >= 0
            && hoveredSelection_.index < static_cast<int>(project_->Wires().size())) {
            const NamedWire& hoveredWire = project_->Wires()[hoveredSelection_.index];
            Vector3 anchorPoint = hoveredWire.wire.Evaluate(0.5);
            QString hoverText = QString::fromUtf8(hoveredWire.name);
            if (hoveredWire.metadata.construction) {
                hoverText += QStringLiteral("  （補助）");
            }
            if (hoveredWirePoint_.has_value()) {
                anchorPoint = *hoveredWirePoint_;
                hoverText += hoveredWire.wire.IsClosed()
                    ? QStringLiteral("  基準点")
                    : AlmostEqual(anchorPoint, hoveredWire.wire.Start(), 1.0e-8)
                    ? QStringLiteral("  始点")
                    : AlmostEqual(anchorPoint, hoveredWire.wire.End(), 1.0e-8)
                    ? QStringLiteral("  終点")
                    : QStringLiteral("  制御点");
                const QPointF screenPoint = ProjectPoint(anchorPoint);
                painter.setBrush(QColor("#ffffff"));
                painter.setPen(QPen(QColor("#087f9c"), 2.4));
                painter.drawEllipse(screenPoint, 6.0, 6.0);
            } else if (hoveredWireParameter_.has_value()) {
                anchorPoint = hoveredWire.wire.Evaluate(*hoveredWireParameter_);
            }

            const QFontMetrics metrics = painter.fontMetrics();
            const QRect textBounds = metrics.boundingRect(hoverText);
            QPointF labelAnchor = ProjectPoint(anchorPoint) + QPointF(10.0, -10.0);
            if (!rect().contains(hoverScreenPosition_)) {
                labelAnchor = QPointF(hoverScreenPosition_) + QPointF(10.0, -10.0);
            }
            QRectF labelBox(
                labelAnchor.x(), labelAnchor.y() - textBounds.height() - 7.0,
                textBounds.width() + 14.0, textBounds.height() + 10.0);
            const QRectF viewportBounds = QRectF(rect()).adjusted(4.0, 4.0, -4.0, -4.0);
            if (labelBox.right() > viewportBounds.right()) {
                labelBox.moveRight(viewportBounds.right());
            }
            if (labelBox.left() < viewportBounds.left()) {
                labelBox.moveLeft(viewportBounds.left());
            }
            if (labelBox.top() < viewportBounds.top()) {
                labelBox.moveTop(viewportBounds.top());
            }
            if (labelBox.bottom() > viewportBounds.bottom()) {
                labelBox.moveBottom(viewportBounds.bottom());
            }
            painter.setPen(QPen(QColor("#087f9c"), 1.0));
            painter.setBrush(QColor(255, 255, 255, 238));
            painter.drawRoundedRect(labelBox, 3.0, 3.0);
            painter.setPen(QColor("#075f69"));
            painter.drawText(labelBox, Qt::AlignCenter, hoverText);
        }
    }

    if (!wireOffsetPreviews_.empty()) {
        painter.save();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor("#8b3fa7"), 2.4, Qt::DashLine));
        for (const Wire& wire : wireOffsetPreviews_) {
            QPainterPath path(ProjectPoint(wire.Evaluate(0.0)));
            const int samples = wire.Kind() == WireKind::Line ? 1 : 64;
            for (int sample = 1; sample <= samples; ++sample) {
                path.lineTo(ProjectPoint(wire.Evaluate(static_cast<double>(sample) / samples)));
            }
            painter.drawPath(path);
        }
        painter.restore();
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
            } else if (tool_ == ViewportTool::DrawSpline) {
                std::vector<Vector3> previewPoints = drawingPoints_;
                previewPoints.push_back(*hoverDrawingPoint_);
                QPainterPath guide(ProjectPoint(previewPoints.front()));
                for (std::size_t index = 1; index < previewPoints.size(); ++index) {
                    guide.lineTo(ProjectPoint(previewPoints[index]));
                }
                painter.drawPath(guide);
                if (previewPoints.size() >= 4) {
                    try {
                        drawPreviewWire(Wire::InterpolatingCubicBSpline(previewPoints));
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

    if (!referenceDimensionOverlays_.empty()) {
        painter.save();
        const QColor dimensionColor("#256b63");
        const QRectF viewportBounds = QRectF(rect()).adjusted(4.0, 4.0, -4.0, -4.0);
        for (std::size_t index = 0; index < referenceDimensionOverlays_.size(); ++index) {
            const ReferenceDimensionOverlay& overlay = referenceDimensionOverlays_[index];
            const QPointF first = ProjectPoint(overlay.firstPoint);
            const QPointF second = ProjectPoint(overlay.secondPoint);
            painter.setPen(QPen(dimensionColor, 1.6, Qt::DashLine));
            painter.drawLine(first, second);
            painter.setBrush(QColor("#ffffff"));
            painter.drawEllipse(first, 3.5, 3.5);
            painter.drawEllipse(second, 3.5, 3.5);

            const QFontMetrics metrics = painter.fontMetrics();
            const QRect textBounds = metrics.boundingRect(overlay.text);
            const QPointF midpoint = (first + second) * 0.5;
            const QPointF labelAnchor = midpoint
                + QPointF(8.0, -8.0 - static_cast<double>(index % 4) * 15.0);
            QRectF labelBox(
                labelAnchor.x(),
                labelAnchor.y() - textBounds.height() - 7.0,
                textBounds.width() + 14.0,
                textBounds.height() + 10.0);
            if (labelBox.right() > viewportBounds.right()) {
                labelBox.moveRight(viewportBounds.right());
            }
            if (labelBox.left() < viewportBounds.left()) {
                labelBox.moveLeft(viewportBounds.left());
            }
            if (labelBox.top() < viewportBounds.top()) {
                labelBox.moveTop(viewportBounds.top());
            }
            if (labelBox.bottom() > viewportBounds.bottom()) {
                labelBox.moveBottom(viewportBounds.bottom());
            }
            painter.setPen(QPen(dimensionColor, 1.0));
            painter.setBrush(QColor(248, 255, 253, 238));
            painter.drawRoundedRect(labelBox, 3.0, 3.0);
            painter.setPen(dimensionColor);
            painter.drawText(labelBox, Qt::AlignCenter, overlay.text);
        }
        painter.restore();
    }

    if (!measurementPicks_.empty() || measurementOverlayFirst_.has_value()) {
        painter.save();
        const QColor measurementColor("#8b3fb0");
        painter.setBrush(QColor("#ffffff"));
        painter.setPen(QPen(measurementColor, 2.2));
        for (const MeasurementPick& pick : measurementPicks_) {
            const QPointF point = ProjectPoint(pick.point);
            painter.drawEllipse(point, 5.0, 5.0);
            painter.drawLine(point + QPointF(-8.0, 0.0), point + QPointF(8.0, 0.0));
            painter.drawLine(point + QPointF(0.0, -8.0), point + QPointF(0.0, 8.0));
        }
        if (measurementOverlayFirst_.has_value()) {
            const QPointF first = ProjectPoint(*measurementOverlayFirst_);
            QPointF labelAnchor = first + QPointF(10.0, -10.0);
            if (measurementOverlaySecond_.has_value()) {
                const QPointF second = ProjectPoint(*measurementOverlaySecond_);
                painter.setPen(QPen(measurementColor, 2.0, Qt::DashLine));
                painter.drawLine(first, second);
                painter.setBrush(QColor("#ffffff"));
                painter.drawEllipse(first, 4.0, 4.0);
                painter.drawEllipse(second, 4.0, 4.0);
                labelAnchor = (first + second) * 0.5 + QPointF(8.0, -8.0);
            }
            if (!measurementOverlayText_.isEmpty()) {
                const QFontMetrics metrics = painter.fontMetrics();
                const QRect textBounds = metrics.boundingRect(measurementOverlayText_);
                QRectF labelBox(
                    labelAnchor.x(),
                    labelAnchor.y() - textBounds.height() - 7.0,
                    textBounds.width() + 14.0,
                    textBounds.height() + 10.0);
                labelBox = labelBox.intersected(QRectF(rect()).adjusted(4.0, 4.0, -4.0, -4.0));
                painter.setPen(QPen(measurementColor, 1.0));
                painter.setBrush(QColor(255, 255, 255, 232));
                painter.drawRoundedRect(labelBox, 3.0, 3.0);
                painter.setPen(measurementColor);
                painter.drawText(labelBox, Qt::AlignCenter, measurementOverlayText_);
            }
        }
        painter.restore();
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
    case ViewportTool::DrawSpline:
        modeText = QStringLiteral("通過点スプライン");
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
    case ViewportTool::Coincident:
        modeText = coincidencePicks_.empty()
            ? QStringLiteral("一致 · 固定側の端点")
            : QStringLiteral("一致 · 追従側の端点");
        break;
    case ViewportTool::Tangent:
        modeText = coincidencePicks_.empty()
            ? QStringLiteral("接線 · 固定側の端点")
            : QStringLiteral("接線 · 追従曲線の端点");
        break;
    case ViewportTool::Curvature:
        modeText = coincidencePicks_.empty()
            ? QStringLiteral("曲率 · 固定側の端点")
            : QStringLiteral("曲率 · 追従ベジェの端点");
        break;
    case ViewportTool::Measure:
        modeText = measurementMode_ == MeasurementMode::TwoPoints
            ? QStringLiteral("測定 · 2点間")
            : QStringLiteral("測定 · 要素");
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
            } else if (tool_ == ViewportTool::DrawSpline) {
                modeText += QStringLiteral("   通過点 %1").arg(drawingPoints_.size());
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
    const bool canAlignSelection = project_ != nullptr && selection_.kind != CadSelectionKind::None;
    const bool selectionHovered = hoveredViewCubeFace_ == static_cast<int>(ViewCubeFace::Selection);
    painter.setBrush(canAlignSelection
            ? (selectionHovered ? QColor("#c9e8e5") : QColor("#e3f1ef"))
            : QColor("#eceff0"));
    painter.setPen(QPen(
        canAlignSelection ? QColor("#39777a") : QColor("#aeb7bc"),
        selectionHovered ? 2.0 : 1.0));
    painter.drawRoundedRect(cube.selection, 3.0, 3.0);
    const QPointF targetCenter(cube.selection.left() + 14.0, cube.selection.center().y());
    painter.drawEllipse(targetCenter, 6.0, 6.0);
    painter.drawLine(targetCenter + QPointF(-9.0, 0.0), targetCenter + QPointF(9.0, 0.0));
    painter.drawLine(targetCenter + QPointF(0.0, -9.0), targetCenter + QPointF(0.0, 9.0));
    painter.setPen(canAlignSelection ? QColor("#174d50") : QColor("#8c969b"));
    painter.drawText(cube.selection.adjusted(27.0, 0.0, -4.0, 0.0), Qt::AlignCenter, QStringLiteral("選択に正対"));
}

CadSelection CadViewport::HitTestWire(QPointF position, double maximumDistance) const
{
    if (project_ == nullptr || !std::isfinite(maximumDistance) || maximumDistance <= 0.0) {
        return {};
    }

    double bestDistance = maximumDistance;
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
    return best;
}

CadSelection CadViewport::HitTest(QPointF position) const
{
    if (project_ == nullptr) {
        return {};
    }

    const CadSelection wire = HitTestWire(position);
    if (wire.kind != CadSelectionKind::None) {
        return wire;
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
        && tool_ != ViewportTool::Measure
        && tool_ != ViewportTool::Coincident
        && tool_ != ViewportTool::Tangent
        && tool_ != ViewportTool::Curvature
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
        const bool unavailableSelection = hoveredFace == static_cast<int>(ViewCubeFace::Selection)
            && selection_.kind == CadSelectionKind::None;
        setCursor(hoveredFace == static_cast<int>(ViewCubeFace::None)
                ? (tool_ == ViewportTool::Select ? Qt::ArrowCursor : Qt::CrossCursor)
                : unavailableSelection ? Qt::ForbiddenCursor : Qt::PointingHandCursor);
        QString tooltip;
        switch (static_cast<ViewCubeFace>(hoveredFace)) {
        case ViewCubeFace::Top:
            tooltip = QStringLiteral("上面に正対");
            break;
        case ViewCubeFace::Front:
            tooltip = QStringLiteral("正面に正対");
            break;
        case ViewCubeFace::Right:
            tooltip = QStringLiteral("右面に正対");
            break;
        case ViewCubeFace::Isometric:
            tooltip = QStringLiteral("3D表示");
            break;
        case ViewCubeFace::Selection:
            tooltip = unavailableSelection
                ? QStringLiteral("線・面・板材・作業平面を先に選択")
                : QStringLiteral("選択対象に正対して中央表示");
            break;
        case ViewCubeFace::None:
            break;
        }
        setToolTip(tooltip);
        update();
    }

    if (dragButton_ == Qt::NoButton
        && hoveredFace == static_cast<int>(ViewCubeFace::None)) {
        UpdateHover(event->position());
    } else if (hoveredFace != static_cast<int>(ViewCubeFace::None)) {
        ClearHover();
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
    } else if (tool_ != ViewportTool::Select
        && tool_ != ViewportTool::Measure
        && tool_ != ViewportTool::Coincident
        && tool_ != ViewportTool::Tangent
        && tool_ != ViewportTool::Curvature
        && activePlane_.has_value()) {
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
            case ViewCubeFace::Selection:
                (void)AlignToSelection();
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

    if (tool_ == ViewportTool::Measure) {
        if (event->button() == Qt::LeftButton && !mouseMoved_) {
            CommitMeasurementPick(event->position());
        } else if (event->button() == Qt::RightButton && !mouseMoved_) {
            ClearMeasurement();
        }
        dragButton_ = Qt::NoButton;
        return;
    }

    if (tool_ == ViewportTool::Coincident || tool_ == ViewportTool::Tangent
        || tool_ == ViewportTool::Curvature) {
        if (event->button() == Qt::LeftButton && !mouseMoved_) {
            CommitCoincidencePick(event->position());
        } else if (event->button() == Qt::RightButton && !mouseMoved_) {
            ClearCoincidencePicks();
        }
        dragButton_ = Qt::NoButton;
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
            if ((tool_ == ViewportTool::DrawPolyline && drawingPoints_.size() >= 2)
                || (tool_ == ViewportTool::DrawSpline && drawingPoints_.size() >= 4)) {
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
        if (tool_ == ViewportTool::Measure) {
            ClearMeasurement();
        } else if (tool_ == ViewportTool::Coincident) {
            ClearCoincidencePicks();
        } else if (tool_ == ViewportTool::Tangent || tool_ == ViewportTool::Curvature) {
            ClearCoincidencePicks();
        } else {
            CancelDrawing();
        }
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

void CadViewport::leaveEvent(QEvent* event)
{
    ClearHover();
    QWidget::leaveEvent(event);
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
