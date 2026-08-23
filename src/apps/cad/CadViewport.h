#pragma once

#include "kachakacha/model/Project.h"

#include <QPoint>
#include <QString>
#include <QWidget>

#include <array>
#include <functional>
#include <optional>
#include <vector>

class QKeyEvent;
class QEvent;

enum class CadSelectionKind {
    None,
    WorkPlane,
    Wire,
    Surface,
    Plate,
    Body,
};

struct CadSelection {
    CadSelectionKind kind = CadSelectionKind::None;
    int index = -1;
};

enum class ViewportTool {
    Select,
    DrawLine,
    DrawPolyline,
    DrawRectangle,
    DrawCircle,
    DrawArc,
    DrawBezier,
    DrawSpline,
    MoveSelection,
    CopySelection,
    MirrorSelection,
    RotateSelection,
    SplitWire,
    Coincident,
    Tangent,
    Curvature,
    Measure,
};

enum class MeasurementMode {
    TwoPoints,
    Elements,
};

enum class MeasurementPickKind {
    Point,
    Wire,
    WorkPlane,
};

struct MeasurementPick {
    MeasurementPickKind kind = MeasurementPickKind::Point;
    int index = -1;
    kachakacha::geometry::Vector3 point;
    double wireParameter = 0.0;
};

struct ReferenceDimensionOverlay {
    kachakacha::geometry::Vector3 firstPoint;
    kachakacha::geometry::Vector3 secondPoint;
    QString text;
};

struct WireEndpointPick {
    int wireIndex = -1;
    kachakacha::model::WireEndpoint endpoint = kachakacha::model::WireEndpoint::Start;
    kachakacha::geometry::Vector3 point;
};

struct DrawingMeasurements {
    bool available = false;
    double lengthMillimeters = 0.0;
    double angleDegrees = 0.0;
    double widthMillimeters = 0.0;
    double heightMillimeters = 0.0;
    double radiusMillimeters = 0.0;
};

struct WireControlPointPick {
    int wireIndex = -1;
    std::size_t controlPointIndex = 0;
};

class CadViewport final : public QWidget {
public:
    explicit CadViewport(QWidget* parent = nullptr);

    void SetProject(const kachakacha::model::Project* project, bool fitView = true);
    void SetSelection(CadSelection selection);
    void SetSelections(std::vector<CadSelection> selections);
    void SetReference(CadSelection reference);
    [[nodiscard]] CadSelection Selection() const noexcept { return selection_; }
    [[nodiscard]] const std::vector<CadSelection>& Selections() const noexcept { return selections_; }
    [[nodiscard]] CadSelection HoveredSelection() const noexcept { return hoveredSelection_; }
    void SetSelectionChangedCallback(std::function<void(const std::vector<CadSelection>&)> callback);
    void SetLineCreatedCallback(std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> callback);
    void SetPolylineCreatedCallback(std::function<void(const std::vector<kachakacha::geometry::Vector3>&)> callback);
    void SetRectangleCreatedCallback(std::function<void(const std::array<kachakacha::geometry::Vector3, 4>&)> callback);
    void SetCircleCreatedCallback(std::function<void(kachakacha::geometry::Vector3, double)> callback);
    void SetArcCreatedCallback(std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> callback);
    void SetBezierCreatedCallback(std::function<void(const std::array<kachakacha::geometry::Vector3, 4>&)> callback);
    void SetSplineCreatedCallback(std::function<void(const std::vector<kachakacha::geometry::Vector3>&)> callback);
    void SetWireControlPointMovedCallback(
        std::function<void(int, const kachakacha::model::Wire&)> callback);
    void SetTranslationRequestedCallback(std::function<void(kachakacha::geometry::Vector3, bool)> callback);
    void SetMirrorRequestedCallback(std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> callback);
    void SetRotationRequestedCallback(std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, double)> callback);
    void SetSplitRequestedCallback(std::function<void(int, double)> callback);
    void SetCoincidenceRequestedCallback(
        std::function<void(WireEndpointPick, WireEndpointPick)> callback);
    void SetTangentRequestedCallback(
        std::function<void(WireEndpointPick, WireEndpointPick)> callback);
    void SetCurvatureRequestedCallback(
        std::function<void(WireEndpointPick, WireEndpointPick)> callback);
    void SetMeasurementChangedCallback(std::function<void(const std::vector<MeasurementPick>&)> callback);
    void SetDrawingStateChangedCallback(std::function<void(ViewportTool, std::size_t)> callback);
    void SetActiveWorkPlane(std::optional<kachakacha::model::WorkPlane> plane);
    void SetTool(ViewportTool tool);
    [[nodiscard]] ViewportTool Tool() const noexcept { return tool_; }
    void SetSnapEnabled(bool enabled);
    void SetSnapStep(double stepMillimeters);
    void SetGridPointsVisible(bool visible);
    void SetGridOrigin(double u, double v);
    void SetPlateSplitPreview(std::optional<kachakacha::model::PlateSplitAxis> axis, double parameter);
    void SetWireOffsetPreview(std::vector<kachakacha::model::Wire> wires);
    [[nodiscard]] std::size_t WireOffsetPreviewCount() const noexcept { return wireOffsetPreviews_.size(); }
    void SetMeasurementMode(MeasurementMode mode);
    [[nodiscard]] MeasurementMode CurrentMeasurementMode() const noexcept { return measurementMode_; }
    void ClearMeasurement();
    void ClearCoincidencePicks();
    [[nodiscard]] const std::vector<WireEndpointPick>& CoincidencePicks() const noexcept
    {
        return coincidencePicks_;
    }
    void SetMeasurementOverlay(
        std::optional<kachakacha::geometry::Vector3> firstPoint,
        std::optional<kachakacha::geometry::Vector3> secondPoint,
        QString text);
    void SetReferenceDimensionOverlays(std::vector<ReferenceDimensionOverlay> overlays);
    [[nodiscard]] std::size_t ReferenceDimensionOverlayCount() const noexcept
    {
        return referenceDimensionOverlays_.size();
    }
    [[nodiscard]] const std::vector<MeasurementPick>& MeasurementPicks() const noexcept { return measurementPicks_; }
    void AlignToActiveWorkPlane();
    void AlignToWorkPlane(const kachakacha::model::WorkPlane& plane);
    [[nodiscard]] bool AlignToSelection();
    void SetIsometricView();
    void SetCornerView(kachakacha::geometry::Vector3 direction);
    void RotateViewYaw(double angleRadians);
    void RollView(double angleRadians);
    [[nodiscard]] kachakacha::geometry::Vector3 ViewDirection() const;
    [[nodiscard]] kachakacha::geometry::Vector3 ViewUpDirection() const;
    void FinishDrawing();
    void CancelDrawing();
    [[nodiscard]] DrawingMeasurements CurrentDrawingMeasurements() const;
    bool CommitDrawingDimensions(double primaryMillimeters, double secondaryValue = 0.0);
    [[nodiscard]] std::size_t DrawingPointCount() const noexcept { return drawingPoints_.size(); }
    void FitAll();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    [[nodiscard]] QPointF ProjectPoint(kachakacha::geometry::Vector3 point) const;
    [[nodiscard]] std::array<kachakacha::geometry::Vector3, 3> CurrentViewBasis() const;
    [[nodiscard]] CadSelection HitTestWire(QPointF position, double maximumDistance = 9.0) const;
    [[nodiscard]] CadSelection HitTest(QPointF position) const;
    [[nodiscard]] bool IsSelected(CadSelectionKind kind, int index) const;
    [[nodiscard]] std::optional<kachakacha::geometry::Vector3> PointOnPlane(
        QPointF position,
        const kachakacha::model::WorkPlane& plane) const;
    [[nodiscard]] std::optional<kachakacha::geometry::Vector3> PointOnActivePlane(QPointF position) const;
    [[nodiscard]] std::optional<WireControlPointPick> NearestEditableControlPoint(
        QPointF position,
        double maximumDistance = 9.0) const;
    [[nodiscard]] std::optional<double> NearestWireParameter(
        int wireIndex,
        QPointF position,
        double maximumDistance = 12.0,
        bool allowEndpoints = false) const;
    [[nodiscard]] std::optional<WireEndpointPick> NearestWireEndpoint(
        QPointF position,
        double maximumDistance = 12.0) const;
    [[nodiscard]] kachakacha::geometry::Vector3 SnapPoint(kachakacha::geometry::Vector3 point, QPointF screenPosition) const;
    [[nodiscard]] kachakacha::geometry::Vector3 SnapDraggedControlPoint(
        kachakacha::geometry::Vector3 point,
        QPointF screenPosition) const;
    [[nodiscard]] kachakacha::geometry::Vector3 ApplyDrawingConstraint(
        kachakacha::geometry::Vector3 point,
        Qt::KeyboardModifiers modifiers) const;
    void CommitDrawingPoint(kachakacha::geometry::Vector3 point);
    void CommitMeasurementPick(QPointF position);
    void CommitCoincidencePick(QPointF position);
    void UpdateHover(QPointF position);
    void ClearHover();
    void CancelControlPointDrag();
    void NotifyDrawingState();
    void NotifySelection();

    const kachakacha::model::Project* project_ = nullptr;
    CadSelection selection_;
    CadSelection reference_;
    CadSelection hoveredSelection_;
    std::vector<CadSelection> selections_;
    std::function<void(const std::vector<CadSelection>&)> selectionChanged_;
    std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> lineCreated_;
    std::function<void(const std::vector<kachakacha::geometry::Vector3>&)> polylineCreated_;
    std::function<void(const std::array<kachakacha::geometry::Vector3, 4>&)> rectangleCreated_;
    std::function<void(kachakacha::geometry::Vector3, double)> circleCreated_;
    std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> arcCreated_;
    std::function<void(const std::array<kachakacha::geometry::Vector3, 4>&)> bezierCreated_;
    std::function<void(const std::vector<kachakacha::geometry::Vector3>&)> splineCreated_;
    std::function<void(int, const kachakacha::model::Wire&)> wireControlPointMoved_;
    std::function<void(kachakacha::geometry::Vector3, bool)> translationRequested_;
    std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> mirrorRequested_;
    std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, double)> rotationRequested_;
    std::function<void(int, double)> splitRequested_;
    std::function<void(WireEndpointPick, WireEndpointPick)> coincidenceRequested_;
    std::function<void(WireEndpointPick, WireEndpointPick)> tangentRequested_;
    std::function<void(WireEndpointPick, WireEndpointPick)> curvatureRequested_;
    std::function<void(const std::vector<MeasurementPick>&)> measurementChanged_;
    std::function<void(ViewportTool, std::size_t)> drawingStateChanged_;
    std::optional<kachakacha::model::WorkPlane> activePlane_;
    ViewportTool tool_ = ViewportTool::Select;
    bool snapEnabled_ = true;
    double snapStep_ = 1.0;
    bool gridPointsVisible_ = true;
    double gridOriginU_ = 0.0;
    double gridOriginV_ = 0.0;
    std::vector<kachakacha::geometry::Vector3> drawingPoints_;
    std::optional<kachakacha::geometry::Vector3> hoverDrawingPoint_;
    std::optional<kachakacha::geometry::Vector3> hoveredWirePoint_;
    std::optional<double> hoveredWireParameter_;
    std::optional<WireControlPointPick> hoveredControlPoint_;
    std::optional<WireControlPointPick> draggedControlPoint_;
    std::optional<kachakacha::model::Wire> draggedWirePreview_;
    std::optional<kachakacha::model::WorkPlane> controlPointDragPlane_;
    std::optional<kachakacha::model::WorkPlane> controlPointSnapPlane_;
    QPoint hoverScreenPosition_;
    std::optional<double> splitPreviewParameter_;
    std::vector<WireEndpointPick> coincidencePicks_;
    std::optional<kachakacha::model::PlateSplitAxis> plateSplitPreviewAxis_;
    double plateSplitPreviewParameter_ = 0.5;
    std::vector<kachakacha::model::Wire> wireOffsetPreviews_;
    MeasurementMode measurementMode_ = MeasurementMode::TwoPoints;
    std::vector<MeasurementPick> measurementPicks_;
    std::optional<kachakacha::geometry::Vector3> measurementOverlayFirst_;
    std::optional<kachakacha::geometry::Vector3> measurementOverlaySecond_;
    QString measurementOverlayText_;
    std::vector<ReferenceDimensionOverlay> referenceDimensionOverlays_;
    kachakacha::geometry::Vector3 target_;
    double yawRadians_ = 0.75;
    double pitchRadians_ = 0.48;
    double rollRadians_ = 0.0;
    std::optional<std::array<kachakacha::geometry::Vector3, 3>> alignedViewBasis_;
    double pixelsPerMillimeter_ = 14.0;
    QPoint lastMousePosition_;
    Qt::MouseButton dragButton_ = Qt::NoButton;
    bool mouseMoved_ = false;
    bool viewCubeInteraction_ = false;
    int hoveredViewCubeFace_ = 0;
};
