#include <QIcon>
#include "V2ToolIcons.h"
#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRectF>
#include <QPixmap>
#include <QSize>
#include <QColor>
#include <QString>
#include <cmath>
#include <algorithm>
#include <QPoint>
#include <string>
namespace {
using P=QPointF;
void Line(QPainter& p,double x,double y,double a,double b){p.drawLine(P(x,y),P(a,b));}
void Dot(QPainter& p,double x,double y){p.save();p.setBrush(p.pen().color());p.drawEllipse(P(x,y),1.4,1.4);p.restore();}
void Arrow(QPainter& p,double x,double y,double a,double b){
    Line(p,x,y,a,b);const double t=std::atan2(b-y,a-x);
    Line(p,a,b,a-4*std::cos(t-.5),b-4*std::sin(t-.5));Line(p,a,b,a-4*std::cos(t+.5),b-4*std::sin(t+.5));
}
std::string Kind(std::string_view id,std::string_view label){
    if(id=="instructions.arrow")return "arrow";
    if(id=="instructions.text")return "text";
    if(id=="instructions.capture")return "image";
    if(id=="instructions.duplicate")return "copy";
    if(id=="instructions.remove")return "trim";
    if(id.starts_with("instructions.")&&id!="instructions.move")return "export";
    if(label=="ベジェ")return "bezier";
    if(label=="ポリライン")return "polyline";
    if(label=="作図点")return "point";
    if(label=="スケール")return "scale";
    if(label=="直線")return "line";
    if(label=="円")return "circle";
    if(label=="円弧")return "arc";
    if(label=="矩形")return "rectangle";
    if(label=="スプライン")return "spline";
    if(label=="楕円")return "ellipse";
    if(label=="テキスト")return "text";
    if(id.starts_with("draw."))return std::string(id.substr(5));
    if(id.find("fillet")!=id.npos||id.find("curvature")!=id.npos)return "fillet";
    if(id.find("chamfer")!=id.npos)return "chamfer";
    for(auto k:{"mirror","rotate","scale","copy","move","trim","extend","split","join","offset","array","project","point","revolve","sweep","loft","extrude","thicken"})
        if(id.find(k)!=id.npos)return k;
    if(id.starts_with("surface.")||id.starts_with("guide."))return "surface";
    if(id.starts_with("fabrication."))return "fabrication";
    if(id.starts_with("export.")||id.starts_with("output."))return "export";
    if(id.find("boolean")!=id.npos||id.starts_with("part."))return "solid";
    if(id.starts_with("workplane.")||id.starts_with("grid."))return "plane";
    if(id.starts_with("measure.")||id.starts_with("dimension."))return "measure";
    if(id.starts_with("image."))return "image";
    if(id=="tools.search"||id.find("fit_")!=id.npos)return "search";
    if(id.starts_with("view."))return "view";
    if(id=="selection.activate")return "select";
    return "edit";
}
void Curve(QPainter& p,bool spline){
    QPainterPath curve;curve.moveTo(3,20);curve.cubicTo(4,1,20,25,21,4);p.drawPath(curve);
    if(spline){Dot(p,3,20);Dot(p,12,12);Dot(p,21,4);}
    else{p.save();p.setPen(QPen(p.pen().color(),.8,Qt::DashLine));Line(p,3,20,4,3);Line(p,21,4,20,22);p.restore();Dot(p,4,3);Dot(p,20,22);}
}
void Cube(QPainter& p){p.drawPolygon(QPolygonF{P(4,8),P(13,3),P(21,7),P(12,12)});p.drawPolyline(QPolygonF{P(4,8),P(4,18),P(12,23),P(21,18),P(21,7)});Line(p,12,12,12,23);}
bool Basic(QPainter& p,const std::string& k,int variant){
    if(k=="arrow")Arrow(p,4,20,20,4);
    else if(k=="line"){Line(p,4,20,20,4);Dot(p,4,20);Dot(p,20,4);}
    else if(k=="point"){Line(p,12,3,12,21);Line(p,3,12,21,12);Dot(p,12,12);}
    else if(k=="circle"){p.drawEllipse(QRectF(4,4,16,16));if(variant==2){Dot(p,4,12);Dot(p,12,4);Dot(p,20,12);}else if(variant==1){Arrow(p,4,12,20,12);}else{Dot(p,12,12);Line(p,12,12,19,7);}}
    else if(k=="arc"){p.drawArc(QRectF(3,4,18,18),10*16,155*16);Dot(p,3.3,10.7);Dot(p,20.8,11.4);if(variant==0)Dot(p,12,4);if(variant==2){Dot(p,12,13);Line(p,12,13,12,4);}if(variant==3)Arrow(p,3,16,3,5);}
    else if(k=="ellipse")p.drawEllipse(QRectF(3,7,18,10));
    else if(k=="rectangle"){p.drawRect(QRectF(4,6,16,12));Dot(p,4,6);Dot(p,20,18);}
    else if(k=="polyline"){p.drawPolygon(QPolygonF{P(3,18),P(5,7),P(13,3),P(21,12),P(16,21)});Dot(p,5,7);}
    else if(k=="bezier"||k=="spline")Curve(p,k=="spline");
    else return false;return true;
}
bool Edit(QPainter& p,const std::string& k){
    if(k=="fillet"||k=="chamfer"){
        QPainterPath path;path.moveTo(4,21);path.lineTo(4,12);
        if(k=="fillet")path.quadTo(4,4,12,4);else path.lineTo(12,4);
        path.lineTo(21,4);p.drawPath(path);
    }else if(k=="trim"){Line(p,4,4,4,20);Line(p,4,12,20,12);Line(p,13,5,20,19);Line(p,13,19,20,5);}
    else if(k=="extend"){Line(p,4,19,12,11);Arrow(p,12,11,20,3);Line(p,3,3,22,3);}
    else if(k=="split"){Line(p,3,19,10,12);Line(p,14,8,21,1);Dot(p,10,12);Dot(p,14,8);}
    else if(k=="join"){Line(p,3,19,12,12);Line(p,12,12,21,19);p.drawEllipse(P(12,12),3,3);}
    else if(k=="offset"){p.drawPolyline(QPolygonF{P(3,20),P(3,5),P(18,5)});p.drawPolyline(QPolygonF{P(9,20),P(9,11),P(22,11)});}
    else if(k=="mirror"){Line(p,12,2,12,22);p.drawPolygon(QPolygonF{P(3,7),P(9,12),P(3,17)});p.drawPolygon(QPolygonF{P(21,7),P(15,12),P(21,17)});}
    else if(k=="move"){Arrow(p,12,12,12,2);Arrow(p,12,12,22,12);Arrow(p,12,12,12,22);Arrow(p,12,12,2,12);}
    else if(k=="rotate"||k=="revolve"){p.drawArc(QRectF(4,4,16,16),35*16,285*16);Arrow(p,20,12,20,5);}
    else if(k=="copy"||k=="scale"){p.drawRect(QRectF(3,11,10,10));p.drawRect(QRectF(11,3,10,10));if(k=="scale")Arrow(p,7,17,19,5);}
    else if(k=="array"){for(int x:{3,14})for(int y:{3,14})p.drawRect(QRectF(x,y,7,7));}
    else if(k=="project"){Line(p,3,20,21,20);p.drawArc(QRectF(4,3,16,10),0,180*16);Arrow(p,12,7,12,18);}
    else return false;return true;
}
void Other(QPainter& p,const std::string& k,int variant){
    if(k=="solid"||k=="extrude"||k=="thicken"){Cube(p);if(k!="solid")Arrow(p,3,11,3,2);}
    else if(k=="plane"||k=="surface"||k=="fabrication"||k=="loft"||k=="sweep"){
        p.drawPolygon(QPolygonF{P(3,9),P(17,3),P(22,16),P(8,22)});
        const int n=k=="fabrication"?4:2;
        for(int i=1;i<n+1;++i){const double t=i/double(n+1);Line(p,3+14*t,9-6*t,8+14*t,22-6*t);}
        if(k=="plane")Line(p,5.5,15.5,19.5,9.5);
        if(k=="surface"&&variant>=0){Dot(p,3,9);Dot(p,22,16);}
    }else if(k=="measure"){Line(p,3,18,21,18);Line(p,4,5,4,21);Line(p,20,5,20,21);Arrow(p,12,10,4,10);Arrow(p,12,10,20,10);}
    else if(k=="image"){p.drawRect(QRectF(3,3,18,18));p.drawPolyline(QPolygonF{P(4,18),P(9,10),P(14,16),P(18,12),P(21,18)});p.drawEllipse(P(16,7),2,2);}
    else if(k=="search"){p.drawEllipse(QRectF(3,3,12,12));Line(p,14,14,22,22);}
    else if(k=="view"){p.drawPolygon(QPolygonF{P(2,12),P(7,6),P(17,6),P(22,12),P(17,18),P(7,18)});p.drawEllipse(P(12,12),3,3);}
    else if(k=="export"){p.drawPolyline(QPolygonF{P(3,12),P(3,21),P(21,21),P(21,12)});Arrow(p,12,18,12,3);}
    else if(k=="select"){p.drawPolygon(QPolygonF{P(4,2),P(19,14),P(12,14),P(9,21)});}
    else if(k=="text"){Line(p,4,4,20,4);Line(p,12,4,12,21);Line(p,8,21,16,21);}
    else{Line(p,4,20,20,4);Line(p,4,20,10,19);Line(p,4,20,5,14);}
}
class Engine final:public QIconEngine {
public:
    Engine(std::string k,int v):kind(std::move(k)),variant(v){}
    QIconEngine* clone() const override{return new Engine(kind,variant);}
    void paint(QPainter* painter,const QRect& rect,QIcon::Mode mode,QIcon::State) override{
        painter->save();const double scale=std::min(rect.width(),rect.height())/26.;
        painter->translate(rect.center().x()-12*scale,rect.center().y()-12*scale);painter->scale(scale,scale);
        painter->setRenderHint(QPainter::Antialiasing);
        const auto palette=QApplication::palette();const auto group=mode==QIcon::Disabled?QPalette::Disabled:QPalette::Active;
        QPen pen(palette.color(group,QPalette::ButtonText),1.7);pen.setCapStyle(Qt::RoundCap);pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);painter->setBrush(Qt::NoBrush);
        if(!Basic(*painter,kind,variant)&&!Edit(*painter,kind))Other(*painter,kind,variant);
        painter->restore();
    }
    QPixmap pixmap(const QSize& size,QIcon::Mode mode,QIcon::State state) override{
        QPixmap result(size);result.fill(Qt::transparent);QPainter painter(&result);paint(&painter,QRect(QPoint(0,0),size),mode,state);return result;
    }
private:std::string kind;int variant;
};
}
QIcon V2ToolIcon(std::string_view command,std::string_view label,int variant){return QIcon(new Engine(Kind(command,label),variant));}
