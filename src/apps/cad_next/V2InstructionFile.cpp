#include "V2InstructionMode.h"
#include "V2MainWindow.h"
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsPathItem>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QComboBox>
#include <QLabel>
#include <QFileDialog>
#include <QFile>
#include <QSaveFile>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPdfWriter>
#include <QPageSize>
#include <QPainter>
#include <QImageReader>
#include <cmath>
#include <algorithm>
#include <QPainterPath>
#include <QPageLayout>
#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QGraphicsItem>
#include <QIODevice>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVariant>
QJsonObject V2InstructionMode::Snapshot() const{
    QJsonArray pages;
    for(std::size_t i=0;i<scenes_.size();++i){QJsonArray items;
        for(auto* item:scenes_[i]->items(Qt::AscendingOrder)){
            const auto kind=item->data(0).toString();if(kind.isEmpty())continue;
            QJsonObject node{{"kind",kind},{"x",item->pos().x()},{"y",item->pos().y()},{"z",item->zValue()},
                {"scale",item->scale()},{"name",item->data(2).toString()}};
            if(auto* part=dynamic_cast<QGraphicsPixmapItem*>(item)){
                auto encoded=item->data(3).toString();
                if(encoded.isEmpty()){QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);part->pixmap().save(&buffer,"PNG");
                    encoded=QString::fromLatin1(png.toBase64());item->setData(3,encoded);}
                node["png"]=encoded;
            }else if(auto* text=dynamic_cast<QGraphicsTextItem*>(item))node["text"]=text->toPlainText();
            else node["points"]=item->data(1).value<QJsonArray>();
            items.push_back(node);
        }
        pages.push_back(QJsonObject{{"title",titles_[i]},{"items",items}});
    }
    return {{"format","kachakacha-instructions"},{"version",1},{"current",current_},{"pages",pages}};
}
bool V2InstructionMode::Restore(const QJsonObject& doc){
    const auto pages=doc["pages"].toArray();
    if(doc["format"]!="kachakacha-instructions"||doc["version"].toInt()!=1||pages.empty()||pages.size()>200)return false;
    std::vector<QGraphicsScene*> made;std::vector<QString> titles;
    const auto fail=[&]{for(auto* scene:made)delete scene;return false;};
    for(const auto& page:pages){
        const auto items=page.toObject()["items"].toArray();if(items.size()>1000)return fail();
        auto* scene=new QGraphicsScene(this);made.push_back(scene);scene->setSceneRect(0,0,1120,792);scene->setBackgroundBrush(Qt::white);
        titles.push_back(page.toObject()["title"].toString());
        for(const auto& value:items){const auto node=value.toObject();const auto kind=node["kind"].toString();QGraphicsItem* item=nullptr;
            if(kind=="part"){
                auto png=QByteArray::fromBase64(node["png"].toString().toLatin1());QBuffer buffer(&png);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer,"PNG");
                const auto size=reader.size();if(size.width()<1||size.height()<1||qint64(size.width())*size.height()>16000000)return fail();
                const auto image=reader.read();if(image.isNull())return fail();item=scene->addPixmap(QPixmap::fromImage(image));item->setData(3,node["png"].toString());
            }else if(kind=="text"){
                auto* label=scene->addText(node["text"].toString(),QFont(QStringLiteral("Meiryo"),20));label->setDefaultTextColor(Qt::black);item=label;
            }else if(kind=="arrow"){
                const auto p=node["points"].toArray();if(p.size()!=4)return fail();
                const QPointF start(p[0].toDouble(),p[1].toDouble()),end(p[2].toDouble(),p[3].toDouble());
                const auto delta=end-start;const double length=std::hypot(delta.x(),delta.y());if(length<2)return fail();
                const auto d=delta/length;const QPointF side(-d.y(),d.x());QPainterPath path;path.moveTo(start);path.lineTo(end);
                path.moveTo(end-d*18+side*8);path.lineTo(end);path.lineTo(end-d*18-side*8);item=scene->addPath(path,QPen(QColor(190,40,20),3));item->setData(1,QVariant::fromValue(p));
            }else return fail();
            const double x=node["x"].toDouble(),y=node["y"].toDouble(),scale=node["scale"].toDouble(1),z=node["z"].toDouble();
            if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(scale)||!std::isfinite(z)||scale<=0||scale>100)return fail();
            item->setPos(x,y);item->setScale(scale);item->setZValue(z);item->setData(0,kind);item->setData(2,node["name"].toString());item->setToolTip(node["name"].toString());
            item->setFlags(QGraphicsItem::ItemIsMovable|QGraphicsItem::ItemIsSelectable);
        }
    }
    CancelArrow();view_->setScene(nullptr);for(auto* scene:scenes_)delete scene;scenes_=std::move(made);titles_=std::move(titles);
    loading_=true;pages_->clear();for(std::size_t i=0;i<titles_.size();++i)pages_->addItem(QString::number(i+1)+QStringLiteral("：")+titles_[i]);loading_=false;
    ShowPage(std::clamp(doc["current"].toInt(),0,static_cast<int>(scenes_.size())-1));return true;
}
bool V2InstructionMode::Save(const QString& path){
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;const auto bytes=QJsonDocument(Snapshot()).toJson(QJsonDocument::Compact);
    if(file.write(bytes)!=bytes.size()||!file.commit())return false;path_=path;dirty_=false;return true;
}
bool V2InstructionMode::Load(const QString& path){
    QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>128*1024*1024)return false;
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||!Restore(doc.object()))return false;
    path_=path;undo_.clear();redo_.clear();dirty_=false;return true;
}
bool V2InstructionMode::ExportPdf(const QString& path){
    CancelArrow();QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
    {QPdfWriter pdf(&file);pdf.setPageSize(QPageSize(QPageSize::A4));pdf.setPageOrientation(QPageLayout::Landscape);pdf.setResolution(144);
    QPainter painter(&pdf);if(!painter.isActive())return false;
    for(std::size_t i=0;i<scenes_.size();++i){if(i&&!pdf.newPage())return false;
        const auto selection=scenes_[i]->selectedItems();scenes_[i]->clearSelection();
        scenes_[i]->render(&painter,QRectF(0,0,pdf.width(),pdf.height()),scenes_[i]->sceneRect(),Qt::KeepAspectRatio);
        for(auto* item:selection)item->setSelected(true);
        painter.setPen(Qt::black);painter.setFont(QFont(QStringLiteral("Meiryo"),14));
        painter.drawText(QRectF(30,15,pdf.width()-60,60),Qt::AlignLeft,QString::number(i+1)+QStringLiteral(". ")+titles_[i]);
    }painter.end();}
    return file.commit();
}
void V2InstructionMode::FileAction(std::string_view id){
    const bool open=id=="instructions.open",pdf=id=="instructions.pdf";
    auto path=pathChooser_?pathChooser_(!open):open?QFileDialog::getOpenFileName(this,QStringLiteral("説明書を開く"),{},QStringLiteral("説明書 (*.kci)")):
        QFileDialog::getSaveFileName(this,pdf?QStringLiteral("手順ページをPDFへ"):QStringLiteral("説明書を保存"),pdf?QString():path_,pdf?QStringLiteral("PDF (*.pdf)"):QStringLiteral("説明書 (*.kci)"));
    if(path.isEmpty())return;const QString suffix=pdf?QStringLiteral(".pdf"):QStringLiteral(".kci");if(!open&&!path.endsWith(suffix,Qt::CaseInsensitive))path+=suffix;
    if(open&&!CheckSaved())return;
    const bool ok=open?Load(path):pdf?ExportPdf(path):Save(path);
    if(hint_)hint_->setText(ok?QStringLiteral("完了：")+path:QStringLiteral("読み書きできませんでした。形式・保存先・空き容量を確認してください。"));
}
