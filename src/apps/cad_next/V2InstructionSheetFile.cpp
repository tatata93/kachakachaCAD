#include "V2InstructionSheet.h"
#include <QJsonObject>
#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>
#include <QSet>
#include <QByteArray>
#include <QFont>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonValue>
#include <QRectF>
#include <QString>
#include <QUuid>
#include <cmath>
namespace {
QJsonObject Font(const QFont& font) {
    return {{"family",font.family()},{"sizePt",font.pointSizeF()>0?font.pointSizeF():12},
        {"bold",font.bold()},{"italic",font.italic()}};
}
bool ReadFont(const QJsonValue& value,QFont& font) {
    if(!value.isObject())return false;const auto node=value.toObject();const auto size=node["sizePt"].toDouble(-1);
    if(!std::isfinite(size)||size<1||size>500||!node["family"].isString()||
        !node["bold"].isBool()||!node["italic"].isBool())return false;
    font.setFamily(node["family"].toString());font.setPointSizeF(size);font.setBold(node["bold"].toBool());
    font.setItalic(node["italic"].toBool());return true;
}
QJsonObject Item(const InstructionSheetItem& item) {
    const auto& r=item.rectMm;
    QJsonObject node{{"id",item.id.toString()},{"rect",QJsonArray{r.x(),r.y(),r.width(),r.height()}}};
    if(item.kind==InstructionItemKind::Scene){node["kind"]="scene";node["sceneId"]=item.sceneId.toString();}
    else if(item.kind==InstructionItemKind::Text){node["kind"]="text";node["text"]=item.text;node["font"]=Font(item.font);}
    else {auto bytes=item.imageBytes;auto format=item.imageFormat;
        if(bytes.isEmpty()){QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);item.image.save(&buffer,"PNG");format="png";}
        node["kind"]="image";node["imageFormat"]=format;node["imageData"]=QString::fromLatin1(bytes.toBase64());}
    return node;
}
bool ReadRect(const QJsonValue& value,QRectF& rect) {
    const auto a=value.toArray();if(a.size()!=4)return false;
    for(const auto& n:a)if(!n.isDouble()||!std::isfinite(n.toDouble())||std::abs(n.toDouble())>100000)return false;
    rect={a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble()};
    return rect.width()>=.01&&rect.height()>=.01;
}
bool ReadImage(const QJsonObject& node,InstructionSheetItem& item) {
    item.imageFormat=node["imageFormat"].toString().toLower();
    if(item.imageFormat!="png"&&item.imageFormat!="jpg"&&item.imageFormat!="jpeg")return false;
    if(!node["imageData"].isString())return false;
    item.imageBytes=QByteArray::fromBase64(node["imageData"].toString().toLatin1());
    if(item.imageBytes.isEmpty()||item.imageBytes.size()>64*1024*1024)return false;
    QBuffer buffer(&item.imageBytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer,item.imageFormat.toLatin1());
    const auto size=reader.size();if(size.width()<1||size.height()<1||qint64(size.width())*size.height()>16000000)return false;
    item.image=reader.read();return !item.image.isNull();
}
bool ReadItem(const QJsonObject& node,const QSet<QUuid>& scenes,InstructionSheetItem& item) {
    item.id=QUuid(node["id"].toString());if(item.id.isNull()||!ReadRect(node["rect"],item.rectMm))return false;
    const auto kind=node["kind"].toString();
    if(kind=="scene"){item.kind=InstructionItemKind::Scene;item.sceneId=QUuid(node["sceneId"].toString());return scenes.contains(item.sceneId);}
    if(kind=="image"){item.kind=InstructionItemKind::Image;return ReadImage(node,item);}
    if(kind!="text"||!node["text"].isString()||!ReadFont(node["font"],item.font))return false;
    item.kind=InstructionItemKind::Text;item.text=node["text"].toString();return item.text.size()<=100000;
}
}
QJsonArray EncodeInstructionSheets(const std::vector<InstructionSheet>& sheets) {
    QJsonArray made;
    for(const auto& sheet:sheets){QJsonArray items;for(const auto& item:sheet.items)items.push_back(Item(item));
        made.push_back(QJsonObject{{"id",sheet.id.toString()},{"title",sheet.title},{"widthMm",sheet.widthMm},
            {"heightMm",sheet.heightMm},{"items",items}});}
    return made;
}
bool DecodeInstructionSheets(const QJsonArray& nodes,const std::vector<InstructionPage>& scenes,std::vector<InstructionSheet>& output) {
    if(nodes.empty()||nodes.size()>200)return false;QSet<QUuid> sceneIds,sheetIds,itemIds;
    for(const auto& scene:scenes)sceneIds.insert(scene.id);std::vector<InstructionSheet> made;
    for(const auto& value:nodes){if(!value.isObject())return false;const auto node=value.toObject();InstructionSheet sheet;
        sheet.id=QUuid(node["id"].toString());sheet.title=node["title"].toString();
        if(sheet.id.isNull()||sheetIds.contains(sheet.id)||!node["widthMm"].isDouble()||!node["heightMm"].isDouble()||
            !node["items"].isArray())return false;
        sheetIds.insert(sheet.id);sheet.widthMm=node["widthMm"].toDouble();sheet.heightMm=node["heightMm"].toDouble();
        if(!std::isfinite(sheet.widthMm)||!std::isfinite(sheet.heightMm)||sheet.widthMm<10||sheet.heightMm<10||
            sheet.widthMm>2000||sheet.heightMm>2000||node["items"].toArray().size()>1000)return false;
        for(const auto& v:node["items"].toArray()){InstructionSheetItem item;
            if(!v.isObject()||!ReadItem(v.toObject(),sceneIds,item)||itemIds.contains(item.id))return false;
            itemIds.insert(item.id);sheet.items.push_back(std::move(item));}
        made.push_back(std::move(sheet));}
    output=std::move(made);return true;
}
