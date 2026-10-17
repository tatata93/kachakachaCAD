#include "V2ToolIllustrations.h"
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QPen>
#include <QColor>
#include <cmath>
namespace {
using P=QPointF;
void Line(QPainter& p,double x,double y,double a,double b){p.drawLine(P(x,y),P(a,b));}
void Arrow(QPainter& p,P a,P b){p.drawLine(a,b);const double t=std::atan2(b.y()-a.y(),b.x()-a.x());
    Line(p,b.x(),b.y(),b.x()-3*std::cos(t-.5),b.y()-3*std::sin(t-.5));Line(p,b.x(),b.y(),b.x()-3*std::cos(t+.5),b.y()-3*std::sin(t+.5));}
void Fill(QPainter& p){auto color=p.pen().color();color.setAlpha(65);p.setBrush(color);}
void Arc(QPainter& p,double y){QPainterPath q;q.moveTo(3,y);q.cubicTo(6,y-9,17,y-9,21,y);p.drawPath(q);}
bool Common(QPainter& p,std::string_view kind){
    p.save();
    if(kind=="file-new"){
        p.drawPolyline(QPolygonF{P(5,22),P(5,2),P(15,2),P(21,8),P(21,22),P(5,22)});Line(p,15,2,15,8);Line(p,15,8,21,8);
    }else if(kind=="file-open"||kind.starts_with("group.")){
        p.drawPolyline(QPolygonF{P(2,21),P(2,4),P(9,4),P(12,7),P(22,7),P(22,21),P(2,21)});
        if(kind=="file-open")p.drawPolyline(QPolygonF{P(3,20),P(7,11),P(23,11),P(20,21)});
        else {Line(p,8,15,18,15);if(kind=="group.create")Line(p,13,10,13,20);if(kind=="group.rename")Line(p,9,19,18,10);}
    }else if(kind=="file-save"){
        p.drawRect(QRectF(3,2,19,21));p.drawRect(QRectF(7,2,10,7));p.drawRect(QRectF(7,14,12,9));Line(p,14,3,14,7);
    }else if(kind=="undo"||kind=="redo"){
        if(kind=="redo"){p.translate(24,0);p.scale(-1,1);}QPainterPath q;q.moveTo(21,20);q.cubicTo(23,7,11,4,4,9);p.drawPath(q);Arrow(p,{12,8},{3,9});
    }else if(kind=="delete"){
        Line(p,3,5,21,5);p.drawRect(QRectF(7,2,10,3));p.drawPolyline(QPolygonF{P(5,5),P(6,22),P(18,22),P(19,5)});Line(p,10,9,10,19);Line(p,14,9,14,19);
    }else if(kind=="hide"||kind=="show"){
        p.drawPolygon(QPolygonF{P(2,12),P(7,6),P(17,6),P(22,12),P(17,18),P(7,18)});p.drawEllipse(P(12,12),3,3);if(kind=="hide")Line(p,3,22,21,2);
    }else if(kind=="freeze"){
        p.drawRect(QRectF(4,11,17,12));p.drawArc(QRectF(7,1,11,19),0,180*16);Line(p,12,15,12,20);
    }else if(kind.starts_with("guide.")){
        p.drawRect(QRectF(2,3,14,19));Line(p,2,9,16,9);Line(p,2,15,16,15);
        if(kind=="guide.row_up")Arrow(p,{20,21},{20,4});else if(kind=="guide.row_down")Arrow(p,{20,4},{20,21});
        else if(kind=="guide.row_remove"||kind=="guide.clear"){Line(p,18,9,23,17);Line(p,18,17,23,9);}
        else {Line(p,17,13,23,13);Line(p,20,10,20,16);}
    }else{p.restore();return false;}
    p.restore();return true;
}

}
bool PaintToolIllustration(QPainter& p,std::string_view kind,int variant){
    if(Common(p,kind))return true;
    p.save();
    if(kind=="boolean-add"||kind=="boolean-cut"||kind=="boolean-common"){
        p.setPen(QPen(p.pen().color(),1,Qt::DashLine));p.drawRect(QRectF(2,4,13,13));p.drawEllipse(QRectF(9,9,13,13));
        p.setPen(QPen(p.pen().color(),1.7));Fill(p);QPainterPath a,b;a.addRect(QRectF(2,4,13,13));b.addEllipse(QRectF(9,9,13,13));
        p.drawPath(kind=="boolean-add"?a.united(b):kind=="boolean-cut"?a.subtracted(b):a.intersected(b));
    }else if(kind=="shell"){
        p.drawPolygon(QPolygonF{P(3,7),P(12,2),P(22,7),P(12,12)});p.drawPolygon(QPolygonF{P(7,7),P(12,4),P(18,7),P(12,10)});
        p.drawPolyline(QPolygonF{P(3,7),P(3,18),P(12,23),P(22,18),P(22,7)});Line(p,12,12,12,23);Arrow(p,{5,15},{8,15});
    }else if(kind=="revolve-surface"||kind=="revolve-solid"){
        p.setPen(QPen(p.pen().color(),.9,Qt::DashLine));Line(p,12,1,12,23);p.setPen(QPen(p.pen().color(),1.7));
        QPainterPath q;q.moveTo(5,20);q.cubicTo(11,15,3,8,7,4);p.drawPath(q);p.drawArc(QRectF(4,4,16,16),0,300*16);
        if(kind=="revolve-solid"){Fill(p);p.drawEllipse(QRectF(5,17,14,5));}else{p.drawArc(QRectF(5,17,14,5),0,180*16);}Arrow(p,{20,13},{20,7});
    }else if(kind=="measure"&&variant>=0){
        if(variant==2){Line(p,4,21,4,3);Line(p,4,21,22,21);p.drawArc(QRectF(0,9,24,24),0,90*16);}
        else if(variant==3){p.drawEllipse(QRectF(3,3,18,18));Arrow(p,{12,12},{20,7});}
        else if(variant==0){Arrow(p,{5,19},{5,3});Arrow(p,{5,19},{21,19});Line(p,5,19,16,8);p.drawEllipse(P(16,8),1.5,1.5);}
        else if(variant==4){Fill(p);p.drawPolygon(QPolygonF{P(3,20),P(5,5),P(18,3),P(22,16)});}
        else {Line(p,4,4,4,21);Line(p,20,4,20,21);Arrow(p,{12,11},{4,11});Arrow(p,{12,11},{20,11});}
    }else if(kind=="surface"&&variant>=0){
        if(variant==0){Fill(p);p.drawPolygon(QPolygonF{P(3,8),P(16,3),P(22,16),P(9,22)});}
        else if(variant==1){Arc(p,12);Arc(p,22);for(int i:{4,9,14,19})Line(p,i,8,i,18);}
        else if(variant==2||variant==3){Arc(p,10);Arc(p,16);Arc(p,22);p.drawPolyline(QPolygonF{P(3,10),P(3,16),P(3,22)});Line(p,21,10,21,22);}
        else if(variant==4){for(int y:{10,16,22})Arc(p,y);for(int x:{6,12,18})Line(p,x,3,x,19);}
        else if(variant==6){p.drawPolygon(QPolygonF{P(2,10),P(15,5),P(20,17),P(7,22)});p.drawPolyline(QPolygonF{P(4,5),P(17,0),P(23,12)});Arrow(p,{12,14},{12,5});}
        else if(variant==8){QPainterPath q;q.moveTo(3,21);q.quadTo(0,12,5,5);q.quadTo(14,0,21,7);q.quadTo(24,16,19,22);q.quadTo(10,17,3,21);p.drawPath(q);}
        else {QPainterPath q;q.moveTo(2,18);q.cubicTo(1,3,19,1,22,10);q.cubicTo(26,21,12,15,2,18);Fill(p);p.drawPath(q);}
    }else if(kind=="thicken"){
        Arc(p,12);Arc(p,19);Line(p,3,12,3,19);Line(p,21,12,21,19);Arrow(p,{12,18},{12,11});
    }else if(kind=="loft"){
        p.drawEllipse(QRectF(6,2,12,5));p.drawEllipse(QRectF(2,16,20,7));Line(p,6,4,2,19);Line(p,18,4,22,19);
    }else if(kind=="sweep"){
        QPainterPath q;q.moveTo(3,21);q.cubicTo(20,23,3,4,21,3);p.drawPath(q);p.drawEllipse(QRectF(1,17,7,6));Arrow(p,{16,6},{21,3});
    }else if(kind=="scene-editor"){
        p.drawPolygon(QPolygonF{P(2,7),P(9,3),P(16,7),P(9,11)});p.drawPolyline(QPolygonF{P(2,7),P(2,16),P(9,20),P(16,16),P(16,7)});Line(p,9,11,9,20);
        p.drawRect(QRectF(16,15,7,6));p.drawEllipse(P(19.5,18),1.5,1.5);
    }else if(kind=="sheet-editor"||kind=="paper-size"||kind=="place-scene"){
        p.drawRect(QRectF(4,2,16,21));
        if(kind=="paper-size"){Arrow(p,{12,11},{4,11});Arrow(p,{12,11},{20,11});Arrow(p,{12,11},{12,2});Arrow(p,{12,11},{12,23});}
        else {p.drawRect(QRectF(7,5,10,8));Line(p,7,17,17,17);Line(p,7,20,14,20);if(kind=="place-scene")Arrow(p,{1,11},{11,8});}
    }else if(kind=="project-surface"||kind=="wrap-project"){
        Arc(p,22);Line(p,3,22,3,12);Line(p,21,22,21,12);p.drawRect(QRectF(7,2,10,5));Arrow(p,{12,8},{12,18});
        if(kind=="wrap-project"){p.drawArc(QRectF(5,10,14,10),0,180*16);Arrow(p,{5,10},{3,16});}
    }else if(kind=="curved-emboss"){
        Arc(p,22);p.drawEllipse(QRectF(7,13,10,6));p.drawEllipse(QRectF(7,3,10,6));Line(p,7,6,7,16);Line(p,17,6,17,16);Arrow(p,{21,18},{21,5});
    }else {p.restore();return false;}
    p.restore();return true;
}
