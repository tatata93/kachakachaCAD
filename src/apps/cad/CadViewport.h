#pragma once

#include "kachakacha/model/Project.h"

#include <QColor>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <array>
#include <functional>
#include <optional>
#include <vector>

class QKeyEvent;
class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QTimer;

enum class CadSelectionKind {
    None,
    WorkPlane,
    Point,
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
    MoveGridOrigin,
    DrawPoint,
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
    TrimWire,
    ExtendWire,
    Coincident,
    Tangent,
    Curvature,
    Measure,
};

enum class DrawingSnapKind {
    None,
    Point,
    Intersection,
    Endpoint,
    //! 作業平面上にない点・端点を、平面へ法線投影した位置(ジオメトリ投影)。
    ProjectedPoint,
    //! 既存線分の延長線上(作図補助)。guideAnchor に線分側の端点が入る。
    Extension,
    Grid,
};

enum class ArcDrawingMode {
    ThreePoints,
    EndpointsRadius,
    StartTangent,
};

struct DrawingSnapCandidate {
    DrawingSnapKind kind = DrawingSnapKind::None;
    kachakacha::geometry::Vector3 point;
    double distancePixels = 0.0;
    //! Extension のとき、破線ガイドの根元(線分の端点)。
    std::optional<kachakacha::geometry::Vector3> guideAnchor;
};

enum class MeasurementMode {
    TwoPoints,
    ThreePointsAngle,
    Elements,
};

enum class ViewportDisplayMode {
    Design,
    FinishedModel,
    IsolatedSelection,
};

enum class SurfaceDiagnosticMode {
    Normal,
    Wireframe,
    GaussianCurvature,
};

enum class ViewRotationAxis {
    X,
    Y,
    Z,
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
    void SetDisplayMode(
        ViewportDisplayMode mode,
        std::vector<CadSelection> isolatedSelections = {});
    [[nodiscard]] ViewportDisplayMode DisplayMode() const noexcept { return displayMode_; }
    void SetReference(CadSelection reference);
    [[nodiscard]] CadSelection Selection() const noexcept { return selection_; }
    [[nodiscard]] const std::vector<CadSelection>& Selections() const noexcept { return selections_; }
    [[nodiscard]] CadSelection HoveredSelection() const noexcept { return hoveredSelection_; }
    void SetSelectionChangedCallback(std::function<void(const std::vector<CadSelection>&)> callback);
    void SetPointCreatedCallback(std::function<void(kachakacha::geometry::Vector3)> callback);
    void SetLineCreatedCallback(std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> callback);
    void SetPolylineCreatedCallback(std::function<void(const std::vector<kachakacha::geometry::Vector3>&)> callback);
    void SetRectangleCreatedCallback(std::function<void(const std::array<kachakacha::geometry::Vector3, 4>&)> callback);
    void SetCircleCreatedCallback(std::function<void(kachakacha::geometry::Vector3, double)> callback);
    void SetArcCreatedCallback(std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> callback);
    void SetArcWireCreatedCallback(std::function<void(const kachakacha::model::Wire&)> callback);
    //! 選択ツール中の右クリックで呼ばれるコンテキストメニュー要求(ADR 0021)。
    //! 作図ツール中の右クリックは従来どおり近傍スナップ開始に使う。
    std::function<void(const QPoint&)> onSelectContextMenu;
    void SetBezierCreatedCallback(std::function<void(const std::array<kachakacha::geometry::Vector3, 4>&)> callback);
    void SetSplineCreatedCallback(std::function<void(const std::vector<kachakacha::geometry::Vector3>&)> callback);
    void SetWireControlPointMovedCallback(
        std::function<void(int, const kachakacha::model::Wire&)> callback);
    void SetTranslationRequestedCallback(std::function<void(kachakacha::geometry::Vector3, bool)> callback);
    void SetMirrorRequestedCallback(std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> callback);
    void SetRotationRequestedCallback(std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, double)> callback);
    void SetSplitRequestedCallback(std::function<void(int, double)> callback);
    void SetTrimRequestedCallback(std::function<void(int, double)> callback);
    void SetExtendRequestedCallback(std::function<void(int, double)> callback);
    void SetToolExitRequestedCallback(std::function<void()> callback);
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
    void SetGridSubdivision(int subdivision);
    void SetGridOrigin(double u, double v);
    void SetGridOriginChangedCallback(std::function<void(double, double)> callback);
    void SetArcDrawingMode(ArcDrawingMode mode);
    [[nodiscard]] ArcDrawingMode CurrentArcDrawingMode() const noexcept { return arcDrawingMode_; }
    void SetConfiguredArc(
        double radiusMillimeters,
        double tangentAngleDegrees,
        double sweepAngleDegrees,
        bool bulgeLeft);
    [[nodiscard]] bool CommitConfiguredArc();
    [[nodiscard]] int GridSubdivision() const noexcept { return gridSubdivision_; }
    [[nodiscard]] double GridOriginU() const noexcept { return gridOriginU_; }
    [[nodiscard]] double GridOriginV() const noexcept { return gridOriginV_; }
    [[nodiscard]] bool IsDisplayed(CadSelectionKind kind, int index, bool projectVisible) const
    {
        return ShouldDisplay(kind, index, projectVisible);
    }
    [[nodiscard]] QPointF ScreenPoint(kachakacha::geometry::Vector3 point) const
    {
        return ProjectPoint(point);
    }
    [[nodiscard]] kachakacha::geometry::Vector3 ViewTarget() const noexcept { return target_; }
    [[nodiscard]] double ViewScale() const noexcept { return pixelsPerMillimeter_; }
    void RestoreViewFraming(kachakacha::geometry::Vector3 target, double pixelsPerMillimeter);
    [[nodiscard]] const std::optional<DrawingSnapCandidate>& DrawingSnapHover() const noexcept
    {
        return drawingSnapHover_;
    }
    [[nodiscard]] bool DirectLineEditPreviewReady() const noexcept
    {
        return directLineEditPreviewWire_.has_value();
    }
    void SetWireAppearance(const QColor& color, double width, Qt::PenStyle style);
    void SetConstructionWireAppearance(const QColor& color, double width, Qt::PenStyle style);
    void SetSurfaceAppearance(
        const QColor& fillColor,
        int opacityPercent,
        const QColor& edgeColor,
        double edgeWidth,
        Qt::PenStyle edgeStyle);
    void SetSurfaceDiagnosticMode(SurfaceDiagnosticMode mode);
    void SetPlateAppearance(
        const QColor& fillColor,
        int opacityPercent,
        const QColor& edgeColor,
        double edgeWidth,
        Qt::PenStyle edgeStyle);
    void SetBackgroundColor(const QColor& color);
    void SetGridColors(const QColor& majorColor, const QColor& minorColor);
    [[nodiscard]] QColor BackgroundColor() const { return backgroundColor_; }
    [[nodiscard]] QColor WireColorSetting() const { return wireColor_; }
    [[nodiscard]] double WireWidthSetting() const noexcept { return wireWidth_; }
    [[nodiscard]] Qt::PenStyle WireStyleSetting() const noexcept { return wireStyle_; }
    void SetPlateSplitPreview(std::optional<kachakacha::model::PlateSplitAxis> axis, double parameter);
    void SetPlateAssemblyGuidePreview(
        std::optional<int> plateIndex,
        std::vector<std::vector<kachakacha::geometry::Vector3>> foldLines,
        std::vector<std::vector<kachakacha::geometry::Vector3>> reliefCuts);
    void SetPlateAssemblyApproximationPreview(
        std::optional<int> plateIndex,
        std::vector<std::array<kachakacha::geometry::Vector3, 3>> panels,
        std::vector<int> pieceIndices,
        std::vector<double> deviations,
        double maximumDeviationMillimeters,
        bool smoothPaper = false);
    //! 部材近似モデルの曲げ状態プレビュー。rails は行(レール)ごとの点列、
    //! creaseDirections は内部レールの山谷(+1/-1/0, サイズ rails-2)。
    //! rails を空にすると消える。
    void SetPartFoldPreview(
        std::vector<std::vector<kachakacha::geometry::Vector3>> rails,
        std::vector<int> creaseDirections);
    [[nodiscard]] std::size_t PlateAssemblyFoldGuideCount() const noexcept
    {
        return plateAssemblyFoldLines_.size();
    }
    [[nodiscard]] std::size_t PlateAssemblyReliefGuideCount() const noexcept
    {
        return plateAssemblyReliefCuts_.size();
    }
    [[nodiscard]] std::size_t PlateAssemblyApproximationPanelCount() const noexcept
    {
        return plateAssemblyApproximationPanels_.size();
    }
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
        QString text,
        QStringList componentTexts = {});
    void SetMeasurementAngleOverlay(
        kachakacha::geometry::Vector3 vertex,
        kachakacha::geometry::Vector3 firstPoint,
        kachakacha::geometry::Vector3 secondPoint,
        QString text);
    void SetReferenceDimensionOverlays(std::vector<ReferenceDimensionOverlay> overlays);
    [[nodiscard]] std::size_t ReferenceDimensionOverlayCount() const noexcept
    {
        return referenceDimensionOverlays_.size();
    }
    [[nodiscard]] const std::vector<MeasurementPick>& MeasurementPicks() const noexcept { return measurementPicks_; }
    [[nodiscard]] int MeasurementComponentOverlayCount() const noexcept
    {
        return measurementOverlayComponentTexts_.size();
    }
    void AlignToActiveWorkPlane();
    void AlignToWorkPlane(const kachakacha::model::WorkPlane& plane);
    [[nodiscard]] bool AlignToSelection();
    void SetIsometricView();
    void SetCornerView(kachakacha::geometry::Vector3 direction);
    //! 指定方向から見るビューへアニメーション付きで遷移する。面ビューでは現在に
    //! 最も近いアップ方向を保存する(視点キューブのクリック用)。
    void SetDirectionView(kachakacha::geometry::Vector3 direction);
    //! 現在のビュー基底から目標基底へ約180msかけて補間する。
    void AnimateViewTo(const std::array<kachakacha::geometry::Vector3, 3>& targetBasis);
    //! 進行中のビュー遷移アニメーションを打ち切る(手動操作が勝つ)。
    void StopViewAnimation();
    void RotateViewAroundWorldAxis(ViewRotationAxis axis, double angleRadians);
    void RotateViewAroundRelativeAxis(ViewRotationAxis axis, double angleRadians);
    void RotateViewYaw(double angleRadians);
    void RollView(double angleRadians);
    [[nodiscard]] kachakacha::geometry::Vector3 ViewDirection() const;
    [[nodiscard]] kachakacha::geometry::Vector3 ViewRightDirection() const;
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
    void contextMenuEvent(QContextMenuEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] QPointF ProjectPoint(kachakacha::geometry::Vector3 point) const;
    [[nodiscard]] std::array<kachakacha::geometry::Vector3, 3> CurrentViewBasis() const;
    void OrbitViewByPixels(double horizontalPixels, double verticalPixels);
    [[nodiscard]] CadSelection HitTestWire(QPointF position, double maximumDistance = 9.0) const;
    [[nodiscard]] CadSelection HitTest(QPointF position) const;
    [[nodiscard]] bool IsSelected(CadSelectionKind kind, int index) const;
    [[nodiscard]] bool HiddenBySet(CadSelectionKind kind, int index) const;
    [[nodiscard]] bool ShouldDisplay(
        CadSelectionKind kind,
        int index,
        bool projectVisible) const;
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
    [[nodiscard]] std::optional<DrawingSnapCandidate> FindDrawingSnap(
        kachakacha::geometry::Vector3 point,
        QPointF screenPosition,
        bool nearbyStructuralOnly = false) const;
    [[nodiscard]] kachakacha::geometry::Vector3 SnapPoint(
        kachakacha::geometry::Vector3 point,
        QPointF screenPosition,
        Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    [[nodiscard]] kachakacha::geometry::Vector3 SnapGridAlignmentTarget(
        kachakacha::geometry::Vector3 point,
        QPointF screenPosition) const;
    [[nodiscard]] kachakacha::geometry::Vector3 SnapDraggedControlPoint(
        kachakacha::geometry::Vector3 point,
        QPointF screenPosition) const;
    [[nodiscard]] kachakacha::geometry::Vector3 ApplyDrawingConstraint(
        kachakacha::geometry::Vector3 point,
        Qt::KeyboardModifiers modifiers) const;
    [[nodiscard]] bool HasDynamicDimensions() const noexcept;
    void UpdateDynamicDimensionEditor();
    void PositionDynamicDimensionEditor();
    void BeginDynamicDimensionInput(const QString& initialText, bool secondary = false);
    bool CommitDynamicDimensionInput();
    bool ValidateDynamicDimensionField(QLineEdit* field, bool positiveOnly);
    void SetDynamicDimensionFieldError(QLineEdit* field, bool error);
    void CommitDrawingPoint(kachakacha::geometry::Vector3 point);
    void CommitMeasurementPick(QPointF position);
    void CommitCoincidencePick(QPointF position);
    void UpdateHover(QPointF position);
    void UpdateDirectLineEditPreview();
    void ClearHover();
    void CancelControlPointDrag();
    void NotifyDrawingState();
    void NotifySelection();

    const kachakacha::model::Project* project_ = nullptr;
    CadSelection selection_;
    CadSelection reference_;
    CadSelection hoveredSelection_;
    std::vector<CadSelection> selections_;
    ViewportDisplayMode displayMode_ = ViewportDisplayMode::Design;
    std::vector<CadSelection> isolatedSelections_;
    std::function<void(const std::vector<CadSelection>&)> selectionChanged_;
    std::function<void(kachakacha::geometry::Vector3)> pointCreated_;
    std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> lineCreated_;
    std::function<void(const std::vector<kachakacha::geometry::Vector3>&)> polylineCreated_;
    std::function<void(const std::array<kachakacha::geometry::Vector3, 4>&)> rectangleCreated_;
    std::function<void(kachakacha::geometry::Vector3, double)> circleCreated_;
    std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> arcCreated_;
    std::function<void(const kachakacha::model::Wire&)> arcWireCreated_;
    std::function<void(const std::array<kachakacha::geometry::Vector3, 4>&)> bezierCreated_;
    std::function<void(const std::vector<kachakacha::geometry::Vector3>&)> splineCreated_;
    std::function<void(int, const kachakacha::model::Wire&)> wireControlPointMoved_;
    std::function<void(kachakacha::geometry::Vector3, bool)> translationRequested_;
    std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> mirrorRequested_;
    std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, double)> rotationRequested_;
    std::function<void(int, double)> splitRequested_;
    std::function<void(int, double)> trimRequested_;
    std::function<void(int, double)> extendRequested_;
    std::function<void()> toolExitRequested_;
    std::function<void(WireEndpointPick, WireEndpointPick)> coincidenceRequested_;
    std::function<void(WireEndpointPick, WireEndpointPick)> tangentRequested_;
    std::function<void(WireEndpointPick, WireEndpointPick)> curvatureRequested_;
    std::function<void(const std::vector<MeasurementPick>&)> measurementChanged_;
    std::function<void(ViewportTool, std::size_t)> drawingStateChanged_;
    std::function<void(double, double)> gridOriginChanged_;
    std::optional<kachakacha::model::WorkPlane> activePlane_;
    ViewportTool tool_ = ViewportTool::Select;
    bool snapEnabled_ = true;
    double snapStep_ = 1.0;
    bool gridPointsVisible_ = true;
    int gridSubdivision_ = 1;
    ArcDrawingMode arcDrawingMode_ = ArcDrawingMode::ThreePoints;
    double configuredArcRadius_ = 5.0;
    double configuredArcTangentAngleDegrees_ = 0.0;
    double configuredArcSweepAngleDegrees_ = 90.0;
    bool configuredArcBulgeLeft_ = true;
    double gridOriginU_ = 0.0;
    double gridOriginV_ = 0.0;
    std::optional<kachakacha::geometry::Vector3> gridOriginDragSource_;
    std::optional<kachakacha::geometry::Vector3> gridOriginDragTarget_;
    double gridOriginDragBaseU_ = 0.0;
    double gridOriginDragBaseV_ = 0.0;
    QColor backgroundColor_{"#f5f6f7"};
    QColor wireColor_{"#263b44"};
    double wireWidth_ = 2.0;
    Qt::PenStyle wireStyle_ = Qt::SolidLine;
    QColor constructionWireColor_{"#697984"};
    double constructionWireWidth_ = 1.7;
    Qt::PenStyle constructionWireStyle_ = Qt::DashLine;
    QColor surfaceFillColor_{"#1f848a"};
    SurfaceDiagnosticMode surfaceDiagnosticMode_ = SurfaceDiagnosticMode::Normal;
    int surfaceOpacityPercent_ = 26;
    QColor surfaceEdgeColor_{"#277b80"};
    double surfaceEdgeWidth_ = 1.4;
    Qt::PenStyle surfaceEdgeStyle_ = Qt::SolidLine;
    QColor plateFillColor_{"#b2c2cb"};
    int plateOpacityPercent_ = 62;
    QColor plateEdgeColor_{"#586970"};
    double plateEdgeWidth_ = 1.0;
    Qt::PenStyle plateEdgeStyle_ = Qt::SolidLine;
    QColor majorGridColor_{"#9aa8b0"};
    QColor minorGridColor_{"#c5cdd2"};
    std::vector<kachakacha::geometry::Vector3> drawingPoints_;
    std::optional<kachakacha::geometry::Vector3> hoverDrawingPoint_;
    std::optional<DrawingSnapCandidate> drawingSnapHover_;
    QFrame* dynamicDimensionEditor_ = nullptr;
    QLabel* dynamicPrimaryLabel_ = nullptr;
    QLabel* dynamicSecondaryLabel_ = nullptr;
    QLineEdit* dynamicPrimaryField_ = nullptr;
    QLineEdit* dynamicSecondaryField_ = nullptr;
    std::optional<kachakacha::geometry::Vector3> hoveredWirePoint_;
    std::optional<double> hoveredWireParameter_;
    std::optional<WireControlPointPick> hoveredControlPoint_;
    std::optional<WireControlPointPick> draggedControlPoint_;
    std::optional<kachakacha::model::Wire> draggedWirePreview_;
    std::optional<kachakacha::model::WorkPlane> controlPointDragPlane_;
    std::optional<kachakacha::model::WorkPlane> controlPointSnapPlane_;
    QPoint hoverScreenPosition_;
    std::optional<double> splitPreviewParameter_;
    std::optional<kachakacha::model::Wire> directLineEditPreviewWire_;
    std::optional<kachakacha::geometry::Vector3> directLineEditPreviewIntersection_;
    std::vector<WireEndpointPick> coincidencePicks_;
    std::optional<kachakacha::model::PlateSplitAxis> plateSplitPreviewAxis_;
    double plateSplitPreviewParameter_ = 0.5;
    std::optional<int> plateAssemblyGuideIndex_;
    std::vector<std::vector<kachakacha::geometry::Vector3>> plateAssemblyFoldLines_;
    std::vector<std::vector<kachakacha::geometry::Vector3>> plateAssemblyReliefCuts_;
    std::optional<int> plateAssemblyApproximationIndex_;
    std::vector<std::array<kachakacha::geometry::Vector3, 3>> plateAssemblyApproximationPanels_;
    std::vector<int> plateAssemblyApproximationPieceIndices_;
    std::vector<double> plateAssemblyApproximationDeviations_;
    double plateAssemblyApproximationMaximumDeviationMillimeters_ = 0.0;
    bool plateAssemblyApproximationSmoothPaper_ = false;
    std::vector<std::vector<kachakacha::geometry::Vector3>> partFoldPreviewRails_;
    std::vector<int> partFoldPreviewCreases_;
    std::vector<kachakacha::model::Wire> wireOffsetPreviews_;
    MeasurementMode measurementMode_ = MeasurementMode::TwoPoints;
    std::vector<MeasurementPick> measurementPicks_;
    std::optional<kachakacha::geometry::Vector3> measurementOverlayFirst_;
    std::optional<kachakacha::geometry::Vector3> measurementOverlaySecond_;
    std::optional<kachakacha::geometry::Vector3> measurementOverlayThird_;
    QString measurementOverlayText_;
    QStringList measurementOverlayComponentTexts_;
    std::vector<ReferenceDimensionOverlay> referenceDimensionOverlays_;
    kachakacha::geometry::Vector3 target_;
    double yawRadians_ = 0.75;
    double pitchRadians_ = 0.48;
    double rollRadians_ = 0.0;
    std::optional<std::array<kachakacha::geometry::Vector3, 3>> alignedViewBasis_;
    double pixelsPerMillimeter_ = 14.0;
    QPoint mousePressPosition_;
    QPoint lastMousePosition_;
    Qt::MouseButton dragButton_ = Qt::NoButton;
    bool mouseMoved_ = false;
    bool orbitInteraction_ = false;
    bool viewCubeInteraction_ = false;
    bool viewCubeDragActive_ = false;
    int pressedViewCubeFace_ = 0;
    int hoveredViewCubeFace_ = 0;
    kachakacha::geometry::Vector3 pressedViewCubeDirection_;
    kachakacha::geometry::Vector3 hoveredViewCubeDirection_;
    bool navigatorHot_ = false;                     //!< カーソルがナビゲータ近傍にあるか(遠いと半透明表示)
    QTimer* viewAnimationTimer_ = nullptr;          //!< ビュー遷移アニメーション(約180ms)
    double viewAnimationProgress_ = 1.0;
    std::array<kachakacha::geometry::Vector3, 3> viewAnimationStart_{};
    std::array<kachakacha::geometry::Vector3, 3> viewAnimationTarget_{};
};
