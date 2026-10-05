#include <QPointF>
#include <QString>
#include <QWidget>
#include "V2InstructionScene.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVector3D>
#include <QLineF>
#include <algorithm>
#include <cmath>
using namespace kachakacha::v2;
V2InstructionScene::V2InstructionScene(QWidget* parent):QWidget(parent){setObjectName("instructionCanvas3d");setMinimumSize(300,260);setMouseTracking(true);}
void V2InstructionScene::SetPage(InstructionPage* page){page_=page;selected_=selectedMark_=-1;arrowPending_=false;Refresh();}
void V2InstructionScene::Refresh(){cache_={};update();}
void V2InstructionScene::Fit(){
    if(!page_)return;geometry::Vector3 lo{1e100,1e100,1e100},hi{-1e100,-1e100,-1e100};bool any=false;
    for(const auto& part:page_->parts)if(part.visible)for(const auto& t:part.asset->mesh.triangles)for(auto p:t.points){
        const auto v=part.rotation.rotatedVector(QVector3D(float(p.x),float(p.y),float(p.z)));p=geometry::Vector3{v.x(),v.y(),v.z()}+part.offset;
        lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};any=true;}
    if(any){page_->center=(lo+hi)*.5;page_->span=std::max(1.0,(hi-lo).Length())*1.35*std::max(1.0,double(width())/std::max(1,height()));}Refresh();
}
void V2InstructionScene::SetTool(const QString& tool,const QString& text){tool_=tool;text_=text;arrowPending_=false;Refresh();}
void V2InstructionScene::SetSelected(int index){selected_=index;selectedMark_=-1;Refresh();}
QPointF V2InstructionScene::Project(geometry::Vector3 p) const{const auto q=mapping_.Project(p);return q?QPointF(q->x,q->y):QPointF();}
geometry::Vector3 V2InstructionScene::WorldAt(QPointF p) const{
    if(!page_)return {};const int x=int(p.x()),y=int(p.y());
    const auto ray=mapping_.RayThrough({p.x(),p.y()});
    if(ray&&x>=0&&x<width()&&y>=0&&y<height()&&depth_.size()==std::size_t(width()*height())&&std::isfinite(depth_[y*width()+x])){
        const double d=geometry::Dot(ray->direction,forward_);if(std::abs(d)>1e-10)return ray->origin+ray->direction*((depth_[y*width()+x]-geometry::Dot(ray->origin,forward_))/d);
    }
    return mapping_.UnprojectOntoPlane({p.x(),p.y()},page_->center,forward_).value_or(page_->center);
}
void V2InstructionScene::paintEvent(QPaintEvent*){if(cache_.size()!=size())cache_=Render(size(),true);QPainter painter(this);painter.fillRect(rect(),Qt::white);painter.drawImage(0,0,cache_);}
void V2InstructionScene::mousePressEvent(QMouseEvent* event){
    if(!page_)return;if(cache_.isNull())cache_=Render(size(),true);last_=event->position();
    if(event->button()!=Qt::LeftButton){if(beginChange)beginChange();return;}
    if(tool_=="arrow"){
        if(!arrowPending_){if(beginChange)beginChange();start_=preview_=WorldAt(last_);arrowPending_=true;}
        else if((WorldAt(last_)-start_).Length()>1e-6){if(beginChange)beginChange();page_->marks.push_back({{},start_,WorldAt(last_),true});arrowPending_=false;if(endChange)endChange();}
        Refresh();return;
    }
    if(tool_=="text"){if(beginChange)beginChange();page_->marks.push_back({text_.isEmpty()?QStringLiteral("1"):text_,WorldAt(last_),{},false});if(endChange)endChange();Refresh();return;}
    selectedMark_=-1;double best=12;
    for(std::size_t i=0;i<page_->marks.size();++i){const auto& m=page_->marks[i];const auto a=Project(m.start),b=m.arrow?Project(m.end):a;
        const auto delta=b-a;const double l=delta.x()*delta.x()+delta.y()*delta.y();const double t=l>0?std::clamp(((last_-a).x()*delta.x()+(last_-a).y()*delta.y())/l,0.0,1.0):0;
        const double distance=QLineF(last_,a+delta*t).length();if(distance<best){best=distance;selectedMark_=int(i);}}
    const int x=int(last_.x()),y=int(last_.y());selected_=selectedMark_<0&&x>=0&&x<width()&&y>=0&&y<height()?pick_[y*width()+x]:-1;
    if(selected_<0&&selectedMark_<0){for(int radius=1;radius<=4&&selected_<0;++radius)for(int dy=-radius;dy<=radius&&selected_<0;++dy)for(int dx=-radius;dx<=radius;++dx){
        const int px=x+dx,py=y+dy;if(px>=0&&px<width()&&py>=0&&py<height()&&pick_[py*width()+px]>=0){selected_=pick_[py*width()+px];break;}}}
    if(selectedChanged)selectedChanged(selected_);if(beginChange)beginChange();dragging_=tool_!="pick"&&(selected_>=0||selectedMark_>=0);Refresh();
}
void V2InstructionScene::mouseMoveEvent(QMouseEvent* event){
    if(!page_)return;const auto at=event->position(),delta=at-last_;last_=at;
    if(event->buttons()&Qt::RightButton){page_->yaw-=delta.x()*.01;page_->pitch=std::clamp(page_->pitch+delta.y()*.01,-1.55,1.55);}
    else if(event->buttons()&Qt::MiddleButton){const auto a=mapping_.UnprojectOntoPlane({at.x(),at.y()},page_->center,forward_);
        const auto b=mapping_.UnprojectOntoPlane({at.x()-delta.x(),at.y()-delta.y()},page_->center,forward_);if(a&&b)page_->center=page_->center+*b-*a;}
    else if(dragging_&&(event->buttons()&Qt::LeftButton)){
        const auto a=mapping_.UnprojectOntoPlane({at.x(),at.y()},page_->center,forward_),b=mapping_.UnprojectOntoPlane({at.x()-delta.x(),at.y()-delta.y()},page_->center,forward_);
        if(a&&b){if(selected_>=0)page_->parts[selected_].offset=page_->parts[selected_].offset+*a-*b;
            else if(selectedMark_>=0){auto& mark=page_->marks[selectedMark_];mark.start=mark.start+*a-*b;mark.end=mark.end+*a-*b;}}
    }else if(arrowPending_)preview_=WorldAt(at);else return;Refresh();
}
void V2InstructionScene::mouseReleaseEvent(QMouseEvent*){dragging_=false;if(endChange)endChange();}
void V2InstructionScene::wheelEvent(QWheelEvent* event){if(!page_)return;if(beginChange)beginChange();page_->span=std::clamp(page_->span/std::pow(1.15,event->angleDelta().y()/120.0),.001,1e9);Refresh();if(endChange)endChange();}
