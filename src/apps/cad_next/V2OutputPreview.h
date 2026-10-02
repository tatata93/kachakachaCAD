#pragma once
#include <QWidget>
#include <QPointF>
#include "kachakacha/modeling/ShapeMesh.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include <vector>
class QPaintEvent;
class QMouseEvent;
class QWheelEvent;
class V2OutputPreview final:public QWidget {
public:
    explicit V2OutputPreview(QWidget* parent=nullptr);
    void SetModels(std::vector<kachakacha::v2::modeling::ShapeMesh> models);
    void Fit();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    std::vector<kachakacha::v2::modeling::ShapeMesh> models_;
    kachakacha::v2::geometry::Bounds3 bounds_;
    kachakacha::v2::geometry::ScreenMapping mapping_;
    double yaw_=-0.7853981634,pitch_=0.6154797087,zoom_=1;
    QPointF last_,pan_;
};
