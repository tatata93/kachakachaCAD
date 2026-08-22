#pragma once

#include "kachakacha/model/Project.h"

#include <QPoint>
#include <QWidget>

#include <functional>

enum class CadSelectionKind {
    None,
    WorkPlane,
    Wire,
};

struct CadSelection {
    CadSelectionKind kind = CadSelectionKind::None;
    int index = -1;
};

class CadViewport final : public QWidget {
public:
    explicit CadViewport(QWidget* parent = nullptr);

    void SetProject(const kachakacha::model::Project* project);
    void SetSelection(CadSelection selection);
    [[nodiscard]] CadSelection Selection() const noexcept { return selection_; }
    void SetSelectionChangedCallback(std::function<void(CadSelection)> callback);
    void FitAll();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    [[nodiscard]] QPointF ProjectPoint(kachakacha::geometry::Vector3 point) const;
    [[nodiscard]] CadSelection HitTest(QPointF position) const;
    void NotifySelection();

    const kachakacha::model::Project* project_ = nullptr;
    CadSelection selection_;
    std::function<void(CadSelection)> selectionChanged_;
    kachakacha::geometry::Vector3 target_;
    double yawRadians_ = 0.75;
    double pitchRadians_ = 0.48;
    double pixelsPerMillimeter_ = 14.0;
    QPoint lastMousePosition_;
    Qt::MouseButton dragButton_ = Qt::NoButton;
    bool mouseMoved_ = false;
};
