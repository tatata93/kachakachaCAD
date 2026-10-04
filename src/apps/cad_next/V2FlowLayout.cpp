#include <QLayout>
#include <QRect>
#include <QSize>
#include "V2FlowLayout.h"
#include <QWidget>
#include <QLayoutItem>
#include <QWidgetItem>
#include <QMargins>
#include <QPoint>
#include <algorithm>
V2FlowLayout::~V2FlowLayout(){while(auto* item=takeAt(0))delete item;}
void V2FlowLayout::addItem(QLayoutItem* item){items_.push_back(item);invalidate();}
void V2FlowLayout::insertWidget(int index,QWidget* widget){
    addChildWidget(widget);items_.insert(items_.begin()+std::clamp(index,0,count()),new QWidgetItem(widget));invalidate();
}
int V2FlowLayout::count() const{return static_cast<int>(items_.size());}
QLayoutItem* V2FlowLayout::itemAt(int i) const{return i>=0&&i<count()?items_[i]:nullptr;}
QLayoutItem* V2FlowLayout::takeAt(int i){if(i<0||i>=count())return nullptr;auto* item=items_[i];items_.erase(items_.begin()+i);invalidate();return item;}
QSize V2FlowLayout::minimumSize() const{
    QSize size;for(auto* item:items_)if(!item->isEmpty())size=size.expandedTo(item->minimumSize());
    const auto m=contentsMargins();return size+QSize(m.left()+m.right(),m.top()+m.bottom());
}
QSize V2FlowLayout::sizeHint() const{return minimumSize();}
int V2FlowLayout::heightForWidth(int width) const{return Arrange(QRect(0,0,width,0),true);}
void V2FlowLayout::setGeometry(const QRect& rect){QLayout::setGeometry(rect);Arrange(rect,false);}
int V2FlowLayout::Arrange(const QRect& rect,bool measure) const{
    const auto m=contentsMargins();const auto area=rect.adjusted(m.left(),m.top(),-m.right(),-m.bottom());
    int x=area.x(),y=area.y(),rowHeight=0;const int gap=std::max(4,spacing());
    for(auto* item:items_){
        if(item->isEmpty())continue;
        auto size=item->sizeHint();
        if(x>area.x()&&x+size.width()>area.right()+1){x=area.x();y+=rowHeight+gap;rowHeight=0;}
        if(!measure)item->setGeometry(QRect(QPoint(x,y),size));
        x+=size.width()+gap;rowHeight=std::max(rowHeight,size.height());
    }
    return y+rowHeight-rect.y()+m.bottom();
}
