#include <QPointF>
#include <QWidget>
#include "V2OutputPreview.h"
#include <QColor>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
using namespace kachakacha::v2;
V2OutputPreview::V2OutputPreview(QWidget* parent):QWidget(parent){setObjectName("outputPreview3d");setMinimumSize(440,360);}
void V2OutputPreview::SetModels(std::vector<modeling::ShapeMesh> models){models_=std::move(models);bounds_={};for(const auto& m:models_){bounds_.Add(m.minimum);bounds_.Add(m.maximum);}update();}
void V2OutputPreview::Fit(){zoom_=1;pan_={};update();}
void V2OutputPreview::paintEvent(QPaintEvent*) {
    const geometry::Vector3 forward{-std::cos(pitch_)*std::cos(yaw_),-std::cos(pitch_)*std::sin(yaw_),-std::sin(pitch_)};
    const geometry::Vector3 up{-std::sin(pitch_)*std::cos(yaw_),-std::sin(pitch_)*std::sin(yaw_),std::cos(pitch_)};
    const double span=std::max(1.0,bounds_.DiagonalLength())*1.15*std::max(1.0,static_cast<double>(width())/height())/zoom_;
    mapping_=geometry::MakeOrthographicMapping((bounds_.minimum+bounds_.maximum)*0.5,forward,up,span,width(),height());
    const auto map=[&](geometry::Vector3 p){const auto s=mapping_.Project(p);return s.has_value()?QPointF(s->x,s->y)+pan_:QPointF();};
    QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.fillRect(rect(),QColor(30,35,42));
    struct Face{QPolygonF points;double depth=0,light=0;};std::vector<Face> faces;
    for(const auto& m:models_)for(const auto& t:m.triangles){Face face;geometry::Vector3 middle{};
        for(const auto& p:t.points){face.points<<map(p);middle=middle+p;}
        face.depth=geometry::Dot(middle,forward);face.light=std::abs(geometry::Dot(geometry::Normalized(geometry::Cross(t.points[1]-t.points[0],t.points[2]-t.points[0])),-forward));faces.push_back(face);}
    std::sort(faces.begin(),faces.end(),[](const Face& a,const Face& b){return a.depth>b.depth;});
    painter.setPen(Qt::NoPen);
    for(const auto& face:faces){painter.setBrush(QColor::fromRgbF(0.27+0.24*face.light,0.43+0.27*face.light,0.55+0.29*face.light));painter.drawPolygon(face.points);}
    painter.setPen(QPen(QColor(217,230,235),1));painter.setBrush(Qt::NoBrush);
    for(const auto& m:models_)for(const auto& edge:m.edges){if(edge.size()==1)painter.drawEllipse(map(edge[0]),3,3);for(std::size_t i=1;i<edge.size();++i)painter.drawLine(map(edge[i-1]),map(edge[i]));}
}
void V2OutputPreview::mousePressEvent(QMouseEvent* event){last_=event->position();}
void V2OutputPreview::mouseMoveEvent(QMouseEvent* event){const auto delta=event->position()-last_;last_=event->position();
    if(event->buttons()&Qt::MiddleButton)pan_+=delta;
    else if(event->buttons()&(Qt::LeftButton|Qt::RightButton)){yaw_-=delta.x()*0.01;pitch_=std::clamp(pitch_+delta.y()*0.01,-1.55,1.55);}update();}
void V2OutputPreview::wheelEvent(QWheelEvent* event){zoom_=std::clamp(zoom_*std::pow(1.15,event->angleDelta().y()/120.0),0.1,50.0);update();}
