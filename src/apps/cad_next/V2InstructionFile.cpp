#include "V2InstructionMode.h"
#include <QImageReader>
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QFile>
#include <QComboBox>
#include <QLabel>
#include <QFileDialog>
#include <QImageWriter>
#include <QSpinBox>
#include <QFileInfo>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <QPainter>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QByteArray>
#include <QFont>
#include <QIODevice>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <QMarginsF>
#include <QQuaternion>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QUuid>
#include <cmath>
#include <algorithm>
using namespace kachakacha::v2;
QJsonObject UpgradeInstructionDocument(const QJsonObject&);
namespace {
QJsonArray Vec(geometry::Vector3 p) { return {p.x,p.y,p.z}; }
bool ReadVec(const QJsonValue& value,geometry::Vector3& p) {
    const auto a=value.toArray();if(a.size()!=3)return false;
    for(const auto& n:a)if(!n.isDouble()||!std::isfinite(n.toDouble())||std::abs(n.toDouble())>1e9)return false;
    p={a[0].toDouble(),a[1].toDouble(),a[2].toDouble()};return true;
}
QJsonObject Font(const QFont& font) {
    return {{"family",font.family()},{"sizePt",font.pointSizeF()>0?font.pointSizeF():18},
        {"bold",font.bold()},{"italic",font.italic()}};
}
bool ReadFont(const QJsonValue& value,QFont& font) {
    if(value.isUndefined())return true;if(!value.isObject())return false;
    const auto node=value.toObject();const auto size=node["sizePt"].toDouble(-1);
    if(!std::isfinite(size)||size<1||size>500||!node["family"].isString()||
        !node["bold"].isBool()||!node["italic"].isBool())return false;
    font.setFamily(node["family"].toString());font.setPointSizeF(size);
    font.setBold(node["bold"].toBool());font.setItalic(node["italic"].toBool());return true;
}
QJsonObject Asset(const InstructionAsset& asset) {
    if(!asset.encoded.isEmpty())return asset.encoded;QJsonArray triangles,edges;
    for(const auto& t:asset.mesh.triangles)triangles.push_back(QJsonArray{Vec(t.points[0]),Vec(t.points[1]),Vec(t.points[2])});
    for(const auto& edge:asset.mesh.edges){QJsonArray points;for(const auto p:edge)points.push_back(Vec(p));edges.push_back(points);}
    asset.encoded={{"id",asset.id.toString()},{"name",asset.name},{"triangles",triangles},{"edges",edges}};
    return asset.encoded;
}
std::shared_ptr<InstructionAsset> ReadAsset(const QJsonObject& node,std::size_t& count) {
    auto asset=std::make_shared<InstructionAsset>();asset->id=QUuid(node["id"].toString());asset->name=node["name"].toString();
    if(asset->id.isNull()||!node["triangles"].isArray()||!node["edges"].isArray())return {};
    const auto triangles=node["triangles"].toArray();count+=triangles.size();if(count>500000||triangles.isEmpty())return {};
    for(const auto& value:triangles){const auto a=value.toArray();if(a.size()!=3)return {};modeling::MeshTriangle triangle;
        for(int i=0;i<3;++i)if(!ReadVec(a[i],triangle.points[i]))return {};asset->mesh.triangles.push_back(triangle);}
    std::size_t points=0;
    for(const auto& value:node["edges"].toArray()){const auto a=value.toArray();points+=a.size();if(!value.isArray()||points>2000000)return {};
        std::vector<geometry::Vector3> edge;for(const auto& p:a){geometry::Vector3 v;if(!ReadVec(p,v))return {};edge.push_back(v);}
        asset->mesh.edges.push_back(std::move(edge));}
    modeling::RefreshBounds(asset->mesh);asset->encoded=node;return asset;
}
bool ReadPart(const QJsonObject& node,const QHash<QUuid,std::shared_ptr<InstructionAsset>>& assets,InstructionPart& part) {
    part.id=QUuid(node["id"].toString());part.asset=assets.value(QUuid(node["asset"].toString()));
    if(part.id.isNull()||!part.asset||!ReadVec(node["offset"],part.offset))return false;
    const auto q=node["rotation"].toArray();if(q.size()!=4)return false;
    for(const auto& x:q)if(!x.isDouble()||!std::isfinite(x.toDouble())||std::abs(x.toDouble())>1)return false;
    part.rotation=QQuaternion(float(q[0].toDouble()),float(q[1].toDouble()),float(q[2].toDouble()),float(q[3].toDouble()));
    if(part.rotation.lengthSquared()<.5)return false;part.rotation.normalize();
    if(!node["visible"].isBool())return false;part.visible=node["visible"].toBool();return true;
}
bool ReadPage(const QJsonObject& node,const QHash<QUuid,std::shared_ptr<InstructionAsset>>& assets,InstructionPage& page) {
    page.id=QUuid(node["id"].toString());if(page.id.isNull())return false;
    page.title=node["title"].toString();page.legacyPng=node["legacy"].toString();
    if(!page.legacyPng.isEmpty()){auto bytes=QByteArray::fromBase64(page.legacyPng.toLatin1());QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer,"PNG");const auto size=reader.size();
        if(size.width()<1||size.height()<1||qint64(size.width())*size.height()>16000000)return false;
        page.legacyImage=reader.read();if(page.legacyImage.isNull())return false;}
    if(!node["yaw"].isDouble()||!node["pitch"].isDouble()||!node["span"].isDouble())return false;
    page.yaw=node["yaw"].toDouble();page.pitch=node["pitch"].toDouble();page.span=node["span"].toDouble();
    if(!std::isfinite(page.yaw)||!std::isfinite(page.pitch)||!std::isfinite(page.span)||page.span<.001||page.span>1e9||
        !ReadVec(node["center"],page.center)||!node["parts"].isArray()||!node["marks"].isArray())return false;
    if(node["parts"].toArray().size()>128||node["marks"].toArray().size()>1000)return false;
    QSet<QUuid> ids;
    for(const auto& v:node["parts"].toArray()){InstructionPart part;
        if(!v.isObject()||!ReadPart(v.toObject(),assets,part)||ids.contains(part.id))return false;
        ids.insert(part.id);page.parts.push_back(part);}
    for(const auto& v:node["marks"].toArray()){if(!v.isObject())return false;const auto m=v.toObject();InstructionMark mark;
        if(!m["arrow"].isBool()||!m["text"].isString()||!ReadFont(m["font"],mark.font)||
            !ReadVec(m["start"],mark.start)||!ReadVec(m["end"],mark.end))return false;
        mark.arrow=m["arrow"].toBool();mark.text=m["text"].toString();page.marks.push_back(mark);}
    return true;
}
QJsonObject Page(const InstructionPage& scene,QJsonArray& assets,QHash<QUuid,bool>& saved) {
    QJsonArray parts,marks;
    for(const auto& part:scene.parts){if(!saved.contains(part.asset->id)){assets.push_back(Asset(*part.asset));saved.insert(part.asset->id,true);}
        const auto q=part.rotation;parts.push_back(QJsonObject{{"id",part.id.toString()},{"asset",part.asset->id.toString()},
            {"offset",Vec(part.offset)},{"rotation",QJsonArray{q.scalar(),q.x(),q.y(),q.z()}},{"visible",part.visible}});}
    for(const auto& mark:scene.marks)marks.push_back(QJsonObject{{"text",mark.text},{"arrow",mark.arrow},
        {"start",Vec(mark.start)},{"end",Vec(mark.end)},{"font",Font(mark.font)}});
    return {{"id",scene.id.toString()},{"title",scene.title},{"legacy",scene.legacyPng},{"parts",parts},{"marks",marks},
        {"yaw",scene.yaw},{"pitch",scene.pitch},{"span",scene.span},{"center",Vec(scene.center)}};
}
}
QJsonObject V2InstructionMode::Snapshot() const {
    QJsonArray pages,assets;QHash<QUuid,bool> saved;
    for(const auto& scene:scenes_)pages.push_back(Page(scene,assets,saved));
    return {{"format","kachakacha-instructions"},{"version",3},{"current",current_},{"assets",assets},{"pages",pages},
        {"sheets",EncodeInstructionSheets(sheets_)},{"currentSheet",currentSheet_},{"workspace",layoutEditing_?"sheet":"scene"}};
}
bool V2InstructionMode::Restore(const QJsonObject& source) {
    const auto doc=UpgradeInstructionDocument(source);const auto pages=doc["pages"].toArray();
    if(doc["format"]!="kachakacha-instructions"||doc["version"].toDouble()!=3||!doc["pages"].isArray()||
        !doc["assets"].isArray()||pages.empty()||pages.size()>200)return false;
    const auto workspace=doc["workspace"].toString();if(workspace!="scene"&&workspace!="sheet")return false;
    QHash<QUuid,std::shared_ptr<InstructionAsset>> assets;std::size_t count=0;
    for(const auto& node:doc["assets"].toArray()){if(!node.isObject())return false;auto asset=ReadAsset(node.toObject(),count);
        if(!asset||assets.contains(asset->id))return false;assets.insert(asset->id,asset);}
    std::vector<InstructionPage> made;QSet<QUuid> ids;
    for(const auto& value:pages){InstructionPage page;if(!value.isObject()||!ReadPage(value.toObject(),assets,page)||ids.contains(page.id))return false;
        ids.insert(page.id);made.push_back(std::move(page));}
    std::vector<InstructionSheet> sheets;
    if(!doc["sheets"].isArray()||!DecodeInstructionSheets(doc["sheets"].toArray(),made,sheets))return false;
    view_->SetPage(nullptr);sheetView_->SetDocument(nullptr,nullptr);
    scenes_=std::move(made);sheets_=std::move(sheets);
    current_=std::clamp(doc["current"].toInt(),0,int(scenes_.size())-1);
    currentSheet_=std::clamp(doc["currentSheet"].toInt(),0,int(sheets_.size())-1);
    SwitchWorkspace(workspace=="sheet");return true;
}
bool V2InstructionMode::Save(const QString& path) {
    CancelInput();
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
    const auto bytes=QJsonDocument(Snapshot()).toJson(QJsonDocument::Compact);
    if(bytes.size()>128*1024*1024){if(hint_)hint_->setText(QStringLiteral("説明書が128 MBを超えます。場面・画像を減らすか説明書を分けてください。"));return false;}
    if(file.write(bytes)!=bytes.size()||!file.commit())return false;path_=path;dirty_=false;return true;
}
bool V2InstructionMode::Load(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>128*1024*1024)return false;QJsonParseError error;
    const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||!Restore(doc.object()))return false;
    path_=path;undo_.clear();redo_.clear();dirty_=false;return true;
}
bool V2InstructionMode::ExportImage(const QString& path,QSize size) {
    if(size.width()<64||size.height()<64||size.width()>4096||size.height()>4096)return false;
    const auto format=QFileInfo(path).suffix().toLatin1().toLower();
    if(!QImageWriter::supportedImageFormats().contains(format))return false;QImage image;
    if(layoutEditing_){V2InstructionSheetView render;render.SetDocument(&sheets_[currentSheet_],&scenes_);image=render.Render(size);}
    else {V2InstructionScene render(nullptr);render.SetPage(&scenes_[current_]);image=render.Render(size);}
    if(image.isNull())return false;QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
    QImageWriter writer(&file,format);writer.setQuality(95);if(!writer.write(image))return false;return file.commit();
}
bool V2InstructionMode::ExportPdf(const QString& path) {
    if(sheets_.empty())return false;QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
    {QPdfWriter pdf(&file);pdf.setResolution(144);QPainter painter;
        V2InstructionSheetView render;
        for(std::size_t i=0;i<sheets_.size();++i){auto& sheet=sheets_[i];
            pdf.setPageSize(QPageSize(QSizeF(std::min(sheet.widthMm,sheet.heightMm),std::max(sheet.widthMm,sheet.heightMm)),
                QPageSize::Millimeter,QString(),QPageSize::ExactMatch));
            pdf.setPageOrientation(sheet.widthMm>sheet.heightMm?QPageLayout::Landscape:QPageLayout::Portrait);
            pdf.setPageMargins(QMarginsF(0,0,0,0));
            if(i){if(!pdf.newPage())return false;}else if(!painter.begin(&pdf))return false;
            render.SetDocument(&sheet,&scenes_);const double scale=std::min(144/25.4,4096/std::max(sheet.widthMm,sheet.heightMm));
            const QSize size(std::max(64,int(std::round(sheet.widthMm*scale))),std::max(64,int(std::round(sheet.heightMm*scale))));
            const auto image=render.Render(size);if(image.isNull())return false;
            painter.drawImage(QRectF(0,0,pdf.width(),pdf.height()),image);}
        painter.end();}
    return file.commit();
}
void V2InstructionMode::FileAction(std::string_view id) {
    CancelInput();
    const bool open=id=="instructions.open",model=id=="instructions.model_file",pdf=id=="instructions.pdf",image=id=="instructions.image";
    QString filter=model?QStringLiteral("CADモデル (*.kcd *.kcd2)"):pdf?QStringLiteral("PDF (*.pdf)"):QStringLiteral("説明書 (*.kci)");
    if(image){QStringList formats;for(const auto& f:QImageWriter::supportedImageFormats())formats<<QString::fromLatin1(f).toUpper()+QStringLiteral(" (*.")+QString::fromLatin1(f)+")";filter=formats.join(";;");}
    QString selectedFilter=image?QStringLiteral("PNG (*.png)"):QString();
    auto path=pathChooser_?pathChooser_(!(open||model)):(open||model)?QFileDialog::getOpenFileName(this,model?QStringLiteral("説明書に使うモデルを選択"):QStringLiteral("説明書を開く"),{},filter):
        QFileDialog::getSaveFileName(this,image?QStringLiteral("現在の場面・用紙を画像出力"):QStringLiteral("説明書を出力"),image?QStringLiteral("step.png"):pdf?QStringLiteral("instructions.pdf"):path_,filter,&selectedFilter);
    if(path.isEmpty())return;
    if(image&&!pathChooser_){const auto begin=selectedFilter.indexOf("*.");const auto end=selectedFilter.indexOf(')',begin);
        if(begin>=0&&end>begin){const auto suffix=selectedFilter.mid(begin+2,end-begin-2);const QFileInfo info(path);path=info.path()+"/"+info.completeBaseName()+"."+suffix;}}
    if(model){ReadModel(path);return;}if(open&&!CheckSaved())return;
    if(!open&&QFileInfo(path).suffix().isEmpty())path+=image?".png":pdf?".pdf":".kci";
    auto* w=settings_?settings_->findChild<QSpinBox*>("instructionImageWidth"):nullptr;
    auto* h=settings_?settings_->findChild<QSpinBox*>("instructionImageHeight"):nullptr;
    if(hint_)hint_->clear();
    const bool ok=open?Load(path):pdf?ExportPdf(path):image?ExportImage(path,QSize(w?w->value():1600,h?h->value():1131)):Save(path);
    if(hint_){if(ok)hint_->setText(QStringLiteral("完了：")+path);
        else if(hint_->text().isEmpty())hint_->setText(QStringLiteral("読み書きできません。形式・保存先・空き容量を確認してください。旧版の説明書も読み込めます。"));}
}
