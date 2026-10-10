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
#include <QFontMetricsF>
#include <algorithm>
#include <cmath>
#include <utility>
using namespace kachakacha::v2;
namespace {
int MarkAt(const InstructionPage& page, QPointF point, int width,
           const std::function<QPointF(geometry::Vector3)>& project) {
    int selected = -1;
    double best = 12;
    for (std::size_t i = 0; i < page.marks.size(); ++i) {
        const auto& mark = page.marks[i];
        const auto a = project(mark.start), b = mark.arrow ? project(mark.end) : a;
        const auto delta = b - a;
        const double lengthSquared = delta.x() * delta.x() + delta.y() * delta.y();
        const double t = lengthSquared > 0 ? std::clamp(
            ((point - a).x() * delta.x() + (point - a).y() * delta.y()) / lengthSquared, 0.0, 1.0) : 0;
        double distance = QLineF(point, a + delta * t).length();
        if(!mark.arrow){
            auto font=mark.font;
            font.setPixelSize(std::max(1,int(std::round(mark.font.pointSizeF()*4/3*width/1400))));
            const auto bounds=QFontMetricsF(font).boundingRect(mark.text).translated(a).adjusted(-4,-4,4,4);
            if(bounds.contains(point))distance=0;
        }
        if (distance < best) { best = distance; selected = int(i); }
    }
    return selected;
}
int ArrowHandleAt(const InstructionPage& page, int index, QPointF point,
                  const std::function<QPointF(geometry::Vector3)>& project) {
    if (index < 0 || index >= int(page.marks.size()) || !page.marks[index].arrow) return -1;
    const auto& mark = page.marks[index];
    const double start = QLineF(point, project(mark.start)).length();
    const double end = QLineF(point, project(mark.end)).length();
    if (std::min(start, end) > 10) return -1;
    return start <= end ? 0 : 1;
}
}
V2InstructionScene::V2InstructionScene(QWidget* parent):QWidget(parent){setObjectName("instructionCanvas3d");setMinimumSize(300,260);setMouseTracking(true);}
void V2InstructionScene::SetPage(InstructionPage* page){
    // Callers may have reallocated scenes; never dereference the previous page here.
    editDragPending_=false;page_=page;selected_=selectedMark_=-1;CancelInput();
}
void V2InstructionScene::BeginEditDrag(){
    editDragPending_=dragging_;dragPart_=selected_;dragMark_=selectedMark_;
    if(!page_||!editDragPending_)return;
    if(dragPart_>=0&&dragPart_<int(page_->parts.size()))dragOriginalOffset_=page_->parts[dragPart_].offset;
    if(dragMark_>=0&&dragMark_<int(page_->marks.size()))dragOriginalMark_=page_->marks[dragMark_];
}
void V2InstructionScene::CancelInput(){
    if(page_&&editDragPending_){
        if(dragPart_>=0&&dragPart_<int(page_->parts.size()))page_->parts[dragPart_].offset=dragOriginalOffset_;
        if(dragMark_>=0&&dragMark_<int(page_->marks.size()))page_->marks[dragMark_]=dragOriginalMark_;
    }
    editDragPending_=false;dragPart_=dragMark_=-1;
    arrowPending_=false;arrowDrag_=false;dragging_=false;markHandle_=-1;Refresh();
}
bool V2InstructionScene::CanAddMark(){
    if(page_&&page_->marks.size()<1000)return true;
    CancelInput();
    if(refused)refused(QStringLiteral("1つの場面には矢印・文字を合計1000個まで配置できます。別の場面を追加してください。"));
    return false;
}
void V2InstructionScene::Refresh(){cache_={};update();}
void V2InstructionScene::Fit(){
    if(!page_)return;CancelInput();geometry::Vector3 lo{1e100,1e100,1e100},hi{-1e100,-1e100,-1e100};bool any=false;
    for(const auto& part:page_->parts)if(part.visible)for(const auto& t:part.asset->mesh.triangles)for(auto p:t.points){
        const auto v=part.rotation.rotatedVector(QVector3D(float(p.x),float(p.y),float(p.z)));p=geometry::Vector3{v.x(),v.y(),v.z()}+part.offset;
        lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};any=true;}
    if(any){page_->center=(lo+hi)*.5;page_->span=std::max(1.0,(hi-lo).Length())*1.35*std::max(1.0,double(width())/std::max(1,height()));}Refresh();
}
void V2InstructionScene::SetTool(const QString& tool,const QString& text){tool_=tool;text_=text;CancelInput();}
void V2InstructionScene::SetSelected(int index){CancelInput();selected_=index;selectedMark_=-1;Refresh();}
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
    if(!page_)return;
    if(editDragPending_)CancelInput();
    if(cache_.size()!=size())cache_=Render(size(),true);
    last_=event->position();
    if(event->button()!=Qt::LeftButton){if(beginChange)beginChange();return;}
    const auto project=[this](geometry::Vector3 p){return Project(p);};
    markHandle_=tool_=="move"&&!arrowPending_?ArrowHandleAt(*page_,selectedMark_,last_,project):-1;
    if(markHandle_>=0){if(beginChange)beginChange();dragging_=true;BeginEditDrag();Refresh();return;}
    if(tool_=="arrow"){
        if(!CanAddMark())return;
        if(beginChange)beginChange();
        if(!arrowPending_){
            start_=preview_=WorldAt(last_);arrowPress_=last_;arrowPending_=arrowDrag_=true;
            selected_=selectedMark_=-1;
        }else if((WorldAt(last_)-start_).Length()>1e-6){
            page_->marks.push_back({{},start_,WorldAt(last_),true});
            selected_=-1;selectedMark_=int(page_->marks.size())-1;arrowPending_=arrowDrag_=false;
        }
        if(selectedChanged)selectedChanged(selected_);Refresh();return;
    }
    if(tool_=="text"){
        if(!CanAddMark())return;
        if(beginChange)beginChange();
        InstructionMark mark{text_.isEmpty()?QStringLiteral("1"):text_,WorldAt(last_),{},false};
        mark.font=textFont_;page_->marks.push_back(std::move(mark));
        selected_=-1;selectedMark_=int(page_->marks.size())-1;
        if(selectedChanged)selectedChanged(selected_);
        if(endChange)endChange();Refresh();return;
    }
    selectedMark_=MarkAt(*page_,last_,width(),project);
    markHandle_=tool_=="move"?ArrowHandleAt(*page_,selectedMark_,last_,project):-1;
    const int x=int(last_.x()),y=int(last_.y());
    selected_=selectedMark_<0&&x>=0&&x<width()&&y>=0&&y<height()?pick_[y*width()+x]:-1;
    if(selected_<0&&selectedMark_<0){
        for(int radius=1;radius<=4&&selected_<0;++radius)
            for(int dy=-radius;dy<=radius&&selected_<0;++dy)for(int dx=-radius;dx<=radius;++dx){
                const int px=x+dx,py=y+dy;
                if(px>=0&&px<width()&&py>=0&&py<height()&&pick_[py*width()+px]>=0){selected_=pick_[py*width()+px];break;}
            }
    }
    if(selectedChanged)selectedChanged(selected_);
    if(beginChange)beginChange();
    dragging_=tool_=="move"&&(selected_>=0||selectedMark_>=0);BeginEditDrag();Refresh();
}
void V2InstructionScene::mouseMoveEvent(QMouseEvent* event){
    if(!page_)return;const auto at=event->position(),delta=at-last_;last_=at;
    if(event->buttons()&Qt::RightButton){page_->yaw-=delta.x()*.01;page_->pitch=std::clamp(page_->pitch+delta.y()*.01,-1.55,1.55);}
    else if(event->buttons()&Qt::MiddleButton){const auto a=mapping_.UnprojectOntoPlane({at.x(),at.y()},page_->center,forward_);
        const auto b=mapping_.UnprojectOntoPlane({at.x()-delta.x(),at.y()-delta.y()},page_->center,forward_);if(a&&b)page_->center=page_->center+*b-*a;}
    else if(dragging_&&(event->buttons()&Qt::LeftButton)){
        if(selectedMark_>=0&&markHandle_>=0){
            auto& mark=page_->marks[selectedMark_];auto& point=markHandle_==0?mark.start:mark.end;
            const auto moved=mapping_.UnprojectOntoPlane({at.x(),at.y()},point,forward_);if(moved)point=*moved;
        }else{
            const auto a=mapping_.UnprojectOntoPlane({at.x(),at.y()},page_->center,forward_);
            const auto b=mapping_.UnprojectOntoPlane({at.x()-delta.x(),at.y()-delta.y()},page_->center,forward_);
            if(a&&b){if(selected_>=0)page_->parts[selected_].offset=page_->parts[selected_].offset+*a-*b;
                else if(selectedMark_>=0){auto& mark=page_->marks[selectedMark_];mark.start=mark.start+*a-*b;mark.end=mark.end+*a-*b;}}
        }
    }else if(arrowPending_)preview_=WorldAt(at);else return;Refresh();
}
void V2InstructionScene::mouseReleaseEvent(QMouseEvent* event){
    if(page_&&event->button()==Qt::LeftButton&&arrowPending_&&arrowDrag_){
        preview_=WorldAt(event->position());
        if(QLineF(arrowPress_,event->position()).length()>=4&&(preview_-start_).Length()>1e-6&&CanAddMark()){
            page_->marks.push_back({{},start_,preview_,true});selected_=-1;selectedMark_=int(page_->marks.size())-1;
            arrowPending_=false;if(selectedChanged)selectedChanged(selected_);
        }
        arrowDrag_=false;Refresh();
    }
    editDragPending_=false;dragPart_=dragMark_=-1;dragging_=false;markHandle_=-1;if(endChange)endChange();
}
void V2InstructionScene::wheelEvent(QWheelEvent* event){if(!page_)return;CancelInput();if(beginChange)beginChange();page_->span=std::clamp(page_->span/std::pow(1.15,event->angleDelta().y()/120.0),.001,1e9);Refresh();if(endChange)endChange();}
