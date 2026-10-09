#include "V2Viewport.h"
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>
namespace {
void Arrow(QPainter& painter,QPointF tip,QPointF toward)
{
    const auto d=toward-tip;const double length=std::hypot(d.x(),d.y());if(length<1e-6)return;
    const auto u=d*(7.0/length), v=QPointF(-u.y(),u.x())*0.4;
    painter.drawLine(tip,tip+u+v);painter.drawLine(tip,tip+u-v);
}
}
void V2Viewport::SetDimensionPreview(std::optional<KeptDimensionView> dimension)
{dimensionPreview_=std::move(dimension);update();}
std::optional<kachakacha::v2::base::DimensionId> V2Viewport::DimensionAt(const QPointF& point) const
{
    const QFontMetrics metrics(font());
    for(const auto& dim:keptDimensions_) {
        if(dim.id.IsNil() || dim.anchors.size()<2)continue;
        const auto a=ToScreen(dim.anchors.front()), b=ToScreen(dim.anchors.back());
        if(!a||!b)continue;
        auto label=dim.labelPosition ? ToScreen(*dim.labelPosition) : std::optional<QPointF>{(*a+*b)*0.5+QPointF(6,-6)};
        if(label && QRectF(label->x()-5,label->y()-metrics.height()-5,
            metrics.horizontalAdvance(dim.text)+10,metrics.height()+10).contains(point))return dim.id;
    }
    return {};
}
void V2Viewport::DrawDimensionView(QPainter& painter,const KeptDimensionView& dimension) const
{
    std::vector<QPointF> screen;
    for(const auto& anchor:dimension.anchors){const auto p=ToScreen(anchor);if(!p)return;screen.push_back(*p);}
    if(screen.size()<2)return;
    painter.save();painter.setPen(QPen(palette_.text,1.0));painter.setBrush(Qt::NoBrush);
    const auto placed=dimension.labelPosition ? ToScreen(*dimension.labelPosition) : std::optional<QPointF>{};
    const QPointF label=placed.value_or((screen.front()+screen.back())*0.5+QPointF(6,-6));
    if(screen.size()==2) {
        const auto a=screen.front(), b=screen.back();auto d=b-a;
        if(dimension.measureDirection)if(const auto end=ToScreen(dimension.anchors.front()+*dimension.measureDirection))d=*end-a;
        const double length=std::hypot(d.x(),d.y());
        if(length>1e-6) {
            const QPointF normal(-d.y()/length,d.x()/length);
            const double offset=placed ? (label.x()-a.x())*normal.x()+(label.y()-a.y())*normal.y() : -12;
            const auto da=a+normal*offset;
            const double other=(b.x()-a.x())*normal.x()+(b.y()-a.y())*normal.y();
            const auto db=b+normal*(offset-other);
            painter.drawLine(a,da+normal*4);painter.drawLine(b,db+normal*4);painter.drawLine(da,db);
            Arrow(painter,da,db);Arrow(painter,db,da);
        }
    } else {
        for(std::size_t i=1;i<screen.size();++i)painter.drawLine(screen[i-1],screen[i]);
        using namespace kachakacha::v2::geometry;
        const auto center=dimension.anchors[1];
        const auto u=Normalized(dimension.anchors[0]-center), v=Normalized(dimension.anchors[2]-center);
        const double sweep=std::acos(std::clamp(Dot(u,v),-1.0,1.0));
        const auto across=Normalized(v-u*Dot(u,v));
        const double radius=dimension.labelPosition ? std::max(0.1,Distance(*dimension.labelPosition,center)) : 7;
        std::optional<QPointF> previous;
        for(int i=0;i<=32;++i) {
            const double angle=sweep*i/32;
            const auto point=ToScreen(center+(u*std::cos(angle)+across*std::sin(angle))*radius);
            if(point && previous)painter.drawLine(*previous,*point);
            previous=point;
        }
    }
    const QFontMetrics metrics(painter.font());
    painter.fillRect(QRectF(label.x()-3,label.y()-metrics.ascent()-3,metrics.horizontalAdvance(dimension.text)+6,metrics.height()+6),palette_.background);
    painter.drawText(label,dimension.text);painter.restore();
}
