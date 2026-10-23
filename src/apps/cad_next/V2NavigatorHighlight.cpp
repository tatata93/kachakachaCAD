#include "V2NavigatorHighlight.h"
#include <QPainter>
#include <QColor>
#include <QRectF>
#include <QPointF>
#include <QPolygonF>
#include <QPen>
#include <array>
#include <vector>

void V2DrawNavigatorHighlight(QPainter& painter,const QRectF& box,
    const kachakacha::v2::view::Quaternion& orientation,
    const kachakacha::v2::view::ViewCubeZone& zone,const QColor& color)
{
    using namespace kachakacha::v2;
    const auto right=view::RightOf(orientation),up=view::UpOf(orientation);
    const double scale=box.width()*.5/1.7320508075688772;
    const auto project=[&](const std::array<double,3>& p) {
        const geometry::Vector3 point{p[0],p[1],p[2]};
        return box.center()+QPointF(geometry::Dot(point,right)*scale,-geometry::Dot(point,up)*scale);
    };
    std::array<double,3> point{double(zone.x),double(zone.y),double(zone.z)};
    std::vector<int> free;
    for(int i=0;i<3;++i)if(point[i]==0)free.push_back(i);
    painter.save();painter.setPen(QPen(color,2.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    QColor fill=color;fill.setAlpha(65);painter.setBrush(fill);
    if(free.size()==2) {
        QPolygonF face;
        for(const auto& corner:std::array<std::array<int,2>,4>{{{-1,-1},{1,-1},{1,1},{-1,1}}}) {
            point[free[0]]=corner[0];point[free[1]]=corner[1];face<<project(point);
        }
        painter.drawPolygon(face);
    } else if(free.size()==1) {
        point[free[0]]=-1;const auto a=project(point);point[free[0]]=1;painter.drawLine(a,project(point));
    } else if(free.empty()) {
        const auto at=project(point);QPolygonF diamond;
        diamond<<at+QPointF(0,-6)<<at+QPointF(6,0)<<at+QPointF(0,6)<<at+QPointF(-6,0);
        painter.setBrush(color);painter.drawPolygon(diamond);
    }
    painter.restore();
}
