#include <QJsonObject>
#include <QJsonArray>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsTextItem>
#include <QGraphicsPathItem>
#include <QGraphicsItem>
#include <QPainter>
#include <QPainterPath>
#include <QImageReader>
#include <QBuffer>
#include <QImage>
#include <QPixmap>
#include <QFont>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QByteArray>
#include <QIODevice>
#include <QString>
#include <QUuid>
#include <cmath>
namespace {
QJsonObject UpgradeVersionOne(const QJsonObject& doc){
    if(doc["format"]!="kachakacha-instructions"||doc["version"].toInt()!=1)return doc;
    QJsonArray pages;const auto old=doc["pages"].toArray();if(old.empty()||old.size()>200)return {};
    for(const auto& value:old){QGraphicsScene scene;scene.setSceneRect(0,0,1120,792);const auto page=value.toObject();const auto items=page["items"].toArray();if(items.size()>1000)return {};
        for(const auto& v:items){const auto node=v.toObject();const auto kind=node["kind"].toString();QGraphicsItem* item=nullptr;
            if(kind=="part"){auto png=QByteArray::fromBase64(node["png"].toString().toLatin1());QBuffer buffer(&png);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer,"PNG");const auto size=reader.size();
                if(size.width()<1||size.height()<1||qint64(size.width())*size.height()>16000000)return {};const auto image=reader.read();if(image.isNull())return {};item=scene.addPixmap(QPixmap::fromImage(image));
            }else if(kind=="text")item=scene.addText(node["text"].toString(),QFont(QStringLiteral("Meiryo"),20));
            else if(kind=="arrow"){const auto p=node["points"].toArray();if(p.size()!=4)return {};for(const auto& n:p)if(!n.isDouble()||!std::isfinite(n.toDouble()))return {};
                const QPointF a(p[0].toDouble(),p[1].toDouble()),b(p[2].toDouble(),p[3].toDouble());const auto delta=b-a;const double length=std::hypot(delta.x(),delta.y());if(length<2)return {};
                const auto d=delta/length;const QPointF side(-d.y(),d.x());QPainterPath path;path.moveTo(a);path.lineTo(b);path.moveTo(b-d*18+side*8);path.lineTo(b);path.lineTo(b-d*18-side*8);item=scene.addPath(path,QPen(Qt::black,3));
            }else return {};
            const double x=node["x"].toDouble(),y=node["y"].toDouble(),z=node["z"].toDouble(),scale=node["scale"].toDouble(1);
            if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(scale)||scale<=0||scale>100)return {};
            item->setPos(x,y);item->setZValue(z);item->setScale(scale);
        }
        QImage image(1120,792,QImage::Format_RGB32);image.fill(Qt::white);QPainter painter(&image);scene.render(&painter);painter.end();
        QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);if(!image.save(&buffer,"PNG"))return {};
        pages.push_back(QJsonObject{{"title",page["title"]},{"parts",QJsonArray{}},{"marks",QJsonArray{}},{"center",QJsonArray{0,0,0}},
            {"yaw",-.785398},{"pitch",.61548},{"span",100},{"legacy",QString::fromLatin1(png.toBase64())}});
    }
    return {{"format","kachakacha-instructions"},{"version",2},{"current",doc["current"]},{"pages",pages},{"assets",QJsonArray{}}};
}
}
QJsonObject UpgradeInstructionDocument(const QJsonObject& source){
    auto doc=source;if(doc["format"]!="kachakacha-instructions")return doc;
    if(doc["version"].toDouble()==1)doc=UpgradeVersionOne(doc);
    if(doc["version"].toDouble()!=2)return doc;
    const auto old=doc["pages"].toArray();if(old.empty()||old.size()>200)return {};
    QJsonArray pages,sheets;
    for(const auto& value:old){if(!value.isObject())return {};auto page=value.toObject();
        const auto id=QUuid::createUuid().toString();page["id"]=id;pages.push_back(page);
        const QJsonObject item{{"id",QUuid::createUuid().toString()},{"kind","scene"},{"sceneId",id},
            {"rect",QJsonArray{15,15,267,180}}};
        sheets.push_back(QJsonObject{{"id",QUuid::createUuid().toString()},{"title",page["title"]},
            {"widthMm",297},{"heightMm",210},{"items",QJsonArray{item}}});}
    doc["version"]=3;doc["pages"]=pages;doc["sheets"]=sheets;
    doc["currentSheet"]=doc["current"];doc["workspace"]="scene";return doc;
}
