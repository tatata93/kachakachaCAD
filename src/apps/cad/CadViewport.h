#pragma once

#include "kachakacha/model/Project.h"

#include <QPoint>
#include <QWidget>

#include <array>
#include <functional>
#include <optional>
#include <vector>

class QKeyEvent;

enum class CadSelectionKind {
    None,
    WorkPlane,
    Wire,
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
    MoveSelection,
    CopySelection,
    MirrorSelection,
};

class CadViewport final : public QWidget {
public:
    explicit CadViewport(QWidget* parent = nullptr);

    void SetProject(const kachakacha::model::Project* project, bool fitView = true);
    void SetSelection(CadSelection selection);
    void SetSelections(std::vector<CadSelection> selections);
    [[nodiscard]] CadSelection Selection() const noexcept { return selection_; }
    [[nodiscard]] const std::vector<CadSelection>& Selections() const noexcept { return selections_; }
    void SetSelectionChangedCallback(std::function<void(const std::vector<CadSelection>&)> callback);
    void SetLineCreatedCallback(std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> callback);
    void SetPolylineCreatedCallback(std::function<void(const std::vector<kachakacha::geometry::Vector3>&)> callback);
    void SetRectangleCreatedCallback(std::function<void(const std::array<kachakacha::geometry::Vector3, 4>&)> callback);
    void SetCircleCreatedCallback(std::function<void(kachakacha::geometry::Vector3, double)> callback);
    void SetArcCreatedCallback(std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> callback);
    void SetBezierCreatedCallback(std::function<void(const std::array<kachakacha::geometry::Vector3, 4>&)> callback);
    void SetTranslationRequestedCallback(std::function<void(kachakacha::geometry::Vector3, bool)> callback);
    void SetMirrorRequestedCallback(std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> callback);
    void SetDrawingStateChangedCallback(std::function<void(ViewportTool, std::size_t)> callback);
    void SetActiveWorkPlane(std::optional<kachakacha::model::WorkPlane> plane);
    void SetTool(ViewportTool tool);
    [[nodiscard]] ViewportTool Tool() const noexcept { return tool_; }
    void SetSnapEnabled(bool enabled);
    void SetSnapStep(double stepMillimeters);
    void AlignToActiveWorkPlane();
    void FinishDrawing();
    void CancelDrawing();
    [[nodiscard]] std::size_t DrawingPointCount() const noexcept { return drawingPoints_.size(); }
    void FitAll();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] QPointF ProjectPoint(kachakacha::geometry::Vector3 point) const;
    [[nodiscard]] std::array<kachakacha::geometry::Vector3, 3> CurrentViewBasis() const;
    [[nodiscard]] CadSelection HitTest(QPointF position) const;
    [[nodiscard]] bool IsSelected(CadSelectionKind kind, int index) const;
    [[nodiscard]] std::optional<kachakacha::geometry::Vector3> PointOnActivePlane(QPointF position) const;
    [[nodiscard]] kachakacha::geometry::Vector3 SnapPoint(kachakacha::geometry::Vector3 point, QPointF screenPosition) const;
    void CommitDrawingPoint(kachakacha::geometry::Vector3 point);
    void NotifyDrawingState();
    void NotifySelection();

    const kachakacha::model::Project* project_ = nullptr;
    CadSelection selection_;
    std::vector<CadSelection> selections_;
    std::function<void(const std::vector<CadSelection>&)> selectionChanged_;
    std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> lineCreated_;
    std::function<void(const std::vector<kachakacha::geometry::Vector3>&)> polylineCreated_;
    std::function<void(const std::array<kachakacha::geometry::Vector3, 4>&)> rectangleCreated_;
    std::function<void(kachakacha::geometry::Vector3, double)> circleCreated_;
    std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> arcCreated_;
    std::function<void(const std::array<kachakacha::geometry::Vector3, 4>&)> bezierCreated_;
    std::function<void(kachakacha::geometry::Vector3, bool)> translationRequested_;
    std::function<void(kachakacha::geometry::Vector3, kachakacha::geometry::Vector3, kachakacha::geometry::Vector3)> mirrorRequested_;
    std::function<void(ViewportTool, std::size_t)> drawingStateChanged_;
    std::optional<kachakacha::model::WorkPlane> activePlane_;
    ViewportTool tool_ = ViewportTool::Select;
    bool snapEnabled_ = true;
    double snapStep_ = 1.0;
    std::vector<kachakacha::geometry::Vector3> drawingPoints_;
    std::optional<kachakacha::geometry::Vector3> hoverDrawingPoint_;
    kachakacha::geometry::Vector3 target_;
    double yawRadians_ = 0.75;
    double pitchRadians_ = 0.48;
    std::optional<std::array<kachakacha::geometry::Vector3, 3>> alignedViewBasis_;
    double pixelsPerMillimeter_ = 14.0;
    QPoint lastMousePosition_;
    Qt::MouseButton dragButton_ = Qt::NoButton;
    bool mouseMoved_ = false;
};
