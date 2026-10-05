#include "V2InstructionMode.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QLabel>
#include <QImage>
#include <algorithm>
#include <QColor>
#include <QGraphicsItem>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QString>
void V2InstructionMode::Capture(){
    struct Part {QString name;std::vector<QPolygonF> faces,edges;QRectF bounds;};
    std::vector<Part> parts;QRectF all;
    const auto& selected=window_.Viewport().Selection().entityIds;
    for(const auto& shape:window_.Viewport().ShapeViews()){
        if(!window_.Viewport().EntityShown(shape.entityId))continue;
        if(!selected.empty()&&std::find(selected.begin(),selected.end(),shape.entityId)==selected.end())continue;
        Part part;const auto* entity=window_.Session().GetDocument().FindEntity(shape.entityId);
        part.name=entity?QString::fromStdString(entity->displayName):QStringLiteral("部品");
        const auto project=[&](const auto& points){QPolygonF polygon;for(const auto& point:points){
            const auto screen=window_.Viewport().Mapping().Project(point);if(!screen)return QPolygonF{};polygon<<QPointF(screen->x,screen->y);}return polygon;};
        for(const auto& triangle:shape.mesh.triangles){auto polygon=project(triangle.points);if(polygon.size()!=3)continue;
            part.bounds=part.bounds.united(polygon.boundingRect());part.faces.push_back(std::move(polygon));}
        for(const auto& edge:shape.mesh.edges)part.edges.push_back(project(edge));
        if(!part.faces.empty()){all=all.united(part.bounds);parts.push_back(std::move(part));}
    }
    if(parts.empty()||parts.size()>128){if(hint_)hint_->setText(QStringLiteral("取り込む部品・面を1～128個選んでください。選択なしでは表示中の部品・面を使います。"));return;}
    if(!selected.empty()&&parts.size()!=selected.size()){
        if(hint_)hint_->setText(QStringLiteral("選択に取り込めないものがあります。表示中の部品・面だけを選んでください。"));return;}
    CancelArrow();arrowTool_=false;before_=Snapshot();const double scale=std::min(960/std::max(1.0,all.width()),600/std::max(1.0,all.height()));
    for(const auto& part:parts){
        const QRectF bounds=part.bounds.adjusted(-3,-3,3,3);
        const double resolution=2*scale;QImage image(std::max(1,int(std::ceil(bounds.width()*resolution))),
            std::max(1,int(std::ceil(bounds.height()*resolution))),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.scale(resolution,resolution);painter.translate(-bounds.topLeft());
        painter.setPen(Qt::NoPen);painter.setBrush(QColor(220,230,239));for(const auto& polygon:part.faces)painter.drawPolygon(polygon);
        painter.setPen(QPen(QColor(40,50,65),1.1/scale));painter.setBrush(Qt::NoBrush);for(const auto& edge:part.edges)painter.drawPolyline(edge);painter.end();
        auto* item=scenes_[current_]->addPixmap(QPixmap::fromImage(image));item->setScale(.5);
        item->setPos(80+(bounds.left()-all.left())*scale,100+(bounds.top()-all.top())*scale);
        item->setFlags(QGraphicsItem::ItemIsMovable|QGraphicsItem::ItemIsSelectable);item->setData(0,"part");item->setData(2,part.name);item->setToolTip(part.name);
    }
    Remember();if(hint_)hint_->setText(QStringLiteral("%1個を取り込みました。各部品をドラッグして分解図を配置できます。元モデルは変更しません。").arg(parts.size()));
}
