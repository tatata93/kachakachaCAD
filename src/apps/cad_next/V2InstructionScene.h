#pragma once
#include "kachakacha/modeling/ShapeMesh.h"
#include "kachakacha/geometry/ScreenMapping.h"
#include <QWidget>
#include <QImage>
#include <QJsonObject>
#include <QQuaternion>
#include <QUuid>
#include <QPointF>
#include <QString>
#include <functional>
#include <memory>
#include <vector>
struct InstructionAsset {
    QUuid id=QUuid::createUuid();
    QString name;
    mutable QJsonObject encoded;
    kachakacha::v2::modeling::ShapeMesh mesh;
};
struct InstructionPart {
    QUuid id=QUuid::createUuid();
    std::shared_ptr<InstructionAsset> asset;
    kachakacha::v2::geometry::Vector3 offset{};
    QQuaternion rotation;
    bool visible=true;
};
struct InstructionMark {
    QString text;
    kachakacha::v2::geometry::Vector3 start{},end{};
    bool arrow=true;
};
struct InstructionPage {
    QString title;
    QString legacyPng;
    QImage legacyImage;
    std::vector<InstructionPart> parts;
    std::vector<InstructionMark> marks;
    double yaw=-0.785398,pitch=0.61548,span=100;
    kachakacha::v2::geometry::Vector3 center{};
};
class V2InstructionScene final:public QWidget {
public:
    explicit V2InstructionScene(QWidget*);
    void SetPage(InstructionPage*);
    void Refresh();
    void Fit();
    void SetTool(const QString&,const QString& text={});
    void SetSelected(int);
    int Selected() const{return selected_;}
    int SelectedMark() const{return selectedMark_;}
    QImage Render(QSize,bool selection=false);
    kachakacha::v2::geometry::Vector3 WorldAt(QPointF) const;
    QPointF Project(kachakacha::v2::geometry::Vector3) const;
    std::function<void()> beginChange,endChange;
    std::function<void(int)> selectedChanged;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    InstructionPage* page_=nullptr;
    QString tool_,text_;
    int selected_=-1,selectedMark_=-1;
    bool dragging_=false,arrowPending_=false;
    QPointF last_;
    kachakacha::v2::geometry::Vector3 start_{},preview_{};
    QImage cache_;
    std::vector<int> pick_;
    std::vector<double> depth_;
    kachakacha::v2::geometry::ScreenMapping mapping_;
    kachakacha::v2::geometry::Vector3 forward_{};
};
