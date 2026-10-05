#include <QImage>
#include <QPointF>
#include "V2InstructionScene.h"
#include <QPainter>
#include <QPen>
#include <QColor>
#include <QVector3D>
#include <QPolygonF>
#include <QSize>
#include <QRectF>
#include <algorithm>
#include <cmath>
#include <limits>
using namespace kachakacha::v2;
namespace {
geometry::Vector3 Transform(const InstructionPart& part,geometry::Vector3 p){
    const auto q=part.rotation.rotatedVector(QVector3D(float(p.x),float(p.y),float(p.z)));
    return geometry::Vector3{q.x(),q.y(),q.z()}+part.offset;
}
struct ScreenVertex{double x,y,z;};
void Raster(const ScreenVertex* p,int index,int w,int h,std::vector<double>& depth,std::vector<int>& ids){
    const double d=(p[1].y-p[2].y)*(p[0].x-p[2].x)+(p[2].x-p[1].x)*(p[0].y-p[2].y);
    if(std::abs(d)<1e-10)return;
    const int left=std::max(0,int(std::clamp(std::floor(std::min({p[0].x,p[1].x,p[2].x})),0.0,double(w)))),right=std::min(w-1,int(std::clamp(std::ceil(std::max({p[0].x,p[1].x,p[2].x})),-1.0,double(w-1))));
    const int top=std::max(0,int(std::clamp(std::floor(std::min({p[0].y,p[1].y,p[2].y})),0.0,double(h)))),bottom=std::min(h-1,int(std::clamp(std::ceil(std::max({p[0].y,p[1].y,p[2].y})),-1.0,double(h-1))));
    for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x){
        const double a=((p[1].y-p[2].y)*(x+.5-p[2].x)+(p[2].x-p[1].x)*(y+.5-p[2].y))/d;
        const double b=((p[2].y-p[0].y)*(x+.5-p[2].x)+(p[0].x-p[2].x)*(y+.5-p[2].y))/d,c=1-a-b;
        if(a<0||b<0||c<0)continue;const double z=a*p[0].z+b*p[1].z+c*p[2].z;const int at=y*w+x;
        if(z<depth[at]){depth[at]=z;ids[at]=index;}
    }
}
void Arrow(QPainter& painter,QPointF a,QPointF b){
    const auto delta=b-a;const double length=std::hypot(delta.x(),delta.y());if(length<1)return;
    const auto d=delta/length;const QPointF side(-d.y(),d.x());painter.drawLine(a,b);
    painter.drawPolyline(QPolygonF{b-d*14+side*6,b,b-d*14-side*6});
}
}
QImage V2InstructionScene::Render(QSize size,bool selection){
    if(!page_||size.width()<1||size.height()<1)return {};
    const int w=size.width(),h=size.height();std::vector<double> depth(w*h,std::numeric_limits<double>::infinity());std::vector<int> ids(w*h,-1);
    const geometry::Vector3 forward{-std::cos(page_->pitch)*std::cos(page_->yaw),-std::cos(page_->pitch)*std::sin(page_->yaw),-std::sin(page_->pitch)};
    const geometry::Vector3 up{-std::sin(page_->pitch)*std::cos(page_->yaw),-std::sin(page_->pitch)*std::sin(page_->yaw),std::cos(page_->pitch)};
    const auto mapping=geometry::MakeOrthographicMapping(page_->center,forward,up,page_->span,w,h);
    const auto project=[&](geometry::Vector3 p){const auto s=mapping.Project(p);return QPointF(s?s->x:0,s?s->y:0);};
    for(std::size_t i=0;i<page_->parts.size();++i){const auto& part=page_->parts[i];if(!part.visible)continue;
        for(const auto& triangle:part.asset->mesh.triangles){ScreenVertex vertices[3];
            for(int k=0;k<3;++k){const auto p=Transform(part,triangle.points[k]);const auto q=project(p);vertices[k]={q.x(),q.y(),geometry::Dot(p,forward)};}
            Raster(vertices,int(i),w,h,depth,ids);
        }
    }
    QImage image(size,QImage::Format_RGB32);image.fill(Qt::white);
    if(!page_->legacyImage.isNull()){QPainter background(&image);background.drawImage(QRectF(0,0,w,h),page_->legacyImage);}
    for(int y=1;y<h-1;++y){auto* row=reinterpret_cast<unsigned int*>(image.scanLine(y));for(int x=1;x<w-1;++x){const int at=y*w+x,id=ids[at];if(id<0)continue;row[x]=qRgb(255,255,255);
        if(ids[at-1]!=id||ids[at+1]!=id||ids[at-w]!=id||ids[at+w]!=id)row[x]=selection&&id==selected_?qRgb(20,100,210):qRgb(20,20,20);
    }}
    QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);
    for(std::size_t i=0;i<page_->parts.size();++i){const auto& part=page_->parts[i];if(!part.visible)continue;
        painter.setPen(QPen(selection&&int(i)==selected_?QColor(20,100,210):QColor(20,20,20),std::max(1.0,w/1400.0)));
        for(const auto& edge:part.asset->mesh.edges)for(std::size_t k=1;k<edge.size();++k){
            const auto a=Transform(part,edge[k-1]),b=Transform(part,edge[k]);const auto pa=project(a),pb=project(b);
            const int steps=std::min(2*(w+h),std::max(1,int(std::min(double(2*(w+h)),std::ceil(std::hypot(pb.x()-pa.x(),pb.y()-pa.y()))))));
            QPointF previous;bool active=false;
            for(int j=0;j<=steps;++j){const double t=double(j)/steps;const auto p=pa+(pb-pa)*t;const int x=int(std::clamp(std::round(p.x()),-1.0,double(w))),y=int(std::clamp(std::round(p.y()),-1.0,double(h)));
                const double z=geometry::Dot(a+(b-a)*t,forward);const bool visible=x>=0&&x<w&&y>=0&&y<h&&z<=depth[y*w+x]+page_->span*0.001;
                if(visible&&active)painter.drawLine(previous,p);active=visible;previous=p;
            }
        }
    }
    painter.setPen(QPen(Qt::black,std::max(1.5,w/700.0)));auto font=painter.font();font.setPixelSize(std::max(12,w/55));painter.setFont(font);
    for(std::size_t i=0;i<page_->marks.size();++i){const auto& mark=page_->marks[i];painter.setPen(QPen(selection&&int(i)==selectedMark_?QColor(20,100,210):QColor(Qt::black),std::max(1.5,w/700.0)));
        if(mark.arrow)Arrow(painter,project(mark.start),project(mark.end));else painter.drawText(project(mark.start),mark.text);
    }
    if(selection&&arrowPending_)Arrow(painter,project(start_),project(preview_));painter.end();
    if(selection){pick_=std::move(ids);depth_=std::move(depth);mapping_=mapping;forward_=forward;}
    return image;
}
