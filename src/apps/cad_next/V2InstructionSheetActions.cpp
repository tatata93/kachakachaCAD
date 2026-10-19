#include <QWidget>
#include "V2InstructionSheet.h"
#include <QKeyEvent>
#include <QWheelEvent>
#include <QString>
#include <QPointF>
#include <QRectF>
#include <QUuid>
#include <algorithm>
#include <cmath>

void V2InstructionSheetView::ZoomAt(double value,QPointF anchor) {
    if(!sheet_)return;
    CancelInput();const auto mm=ToMm(anchor);
    zoom_=std::clamp(value,.25,8.0);pan_+=anchor-ToScreen(mm);
    Refresh();if(viewChanged)viewChanged();
}
void V2InstructionSheetView::SetZoom(double value) {ZoomAt(value,QPointF(width()/2.0,height()/2.0));}
void V2InstructionSheetView::FitView() {CancelInput();zoom_=1;pan_={};Refresh();if(viewChanged)viewChanged();}
void V2InstructionSheetView::wheelEvent(QWheelEvent* event) {
    ZoomAt(zoom_*std::pow(1.15,event->angleDelta().y()/120.0),event->position());event->accept();
}
void V2InstructionSheetView::keyPressEvent(QKeyEvent* event) {
    if(!sheet_||tool_!="move"||selected_<0||selected_>=int(sheet_->items.size())||
        (event->modifiers()&(Qt::ControlModifier|Qt::AltModifier))){QWidget::keyPressEvent(event);return;}
    QPointF delta;const double step=event->modifiers()&Qt::ShiftModifier?10:1;
    switch(event->key()) {
    case Qt::Key_Left:delta={-step,0};break;case Qt::Key_Right:delta={step,0};break;
    case Qt::Key_Up:delta={0,-step};break;case Qt::Key_Down:delta={0,step};break;
    default:QWidget::keyPressEvent(event);return;
    }
    CancelInput();if(beginChange)beginChange();auto& rect=sheet_->items[selected_].rectMm;
    rect.moveTopLeft({std::clamp(rect.x()+delta.x(),0.0,std::max(0.0,sheet_->widthMm-rect.width())),
        std::clamp(rect.y()+delta.y(),0.0,std::max(0.0,sheet_->heightMm-rect.height()))});
    if(endChange)endChange();if(selectionChanged)selectionChanged();Refresh();event->accept();
}
void V2InstructionSheetView::AdjustItem(const QString& action) {
    if(!sheet_||selected_<0||selected_>=int(sheet_->items.size()))return;
    CancelInput();if(beginChange)beginChange();auto& items=sheet_->items;auto& r=items[selected_].rectMm;
    if(action=="left")r.moveLeft(0);else if(action=="right")r.moveRight(sheet_->widthMm);
    else if(action=="top")r.moveTop(0);else if(action=="bottom")r.moveBottom(sheet_->heightMm);
    else if(action=="center_x")r.moveCenter({sheet_->widthMm/2,r.center().y()});
    else if(action=="center_y")r.moveCenter({r.center().x(),sheet_->heightMm/2});
    else if(action=="front"||action=="back") {
        auto item=items[selected_];items.erase(items.begin()+selected_);
        if(action=="front"){items.push_back(std::move(item));selected_=int(items.size())-1;}
        else {items.insert(items.begin(),std::move(item));selected_=0;}
    } else if(action=="duplicate") {
        if(items.size()>=1000){if(refused)refused(QStringLiteral("1枚の用紙には1000個まで配置できます。"));return;}
        auto item=items[selected_];item.id=QUuid::createUuid();
        item.rectMm.moveTopLeft({std::clamp(item.rectMm.x()+5,0.0,std::max(0.0,sheet_->widthMm-item.rectMm.width())),
            std::clamp(item.rectMm.y()+5,0.0,std::max(0.0,sheet_->heightMm-item.rectMm.height()))});
        items.push_back(std::move(item));selected_=int(items.size())-1;
    } else return;
    if(endChange)endChange();if(selectionChanged)selectionChanged();Refresh();
}
