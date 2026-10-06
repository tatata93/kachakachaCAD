#include <QImage>
#include <QString>
#include <QWidget>
#include "V2ImageTool.h"
#include "V2MainWindow.h"
#include "V2OperationPanelHost.h"
#include "V2Viewport.h"
#include "kachakacha/app/ExplorerModel.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/kernel/OcctOutput.h"
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCloseEvent>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QImageReader>
#include <QColor>
#include <QLabel>
#include <QSignalBlocker>
#include <QIODevice>
#include <QSize>
#include <algorithm>
using namespace kachakacha::v2;
V2ImageTool* V2ImageTool::Open(V2MainWindow& window) {
    if (auto* active=dynamic_cast<V2ImageTool*>(window.operationHost_->TemporaryPage())) {
        if (active->image_.isNull()) (void)active->EditSelectedImage();
        return active;
    }
    const auto selection=window.viewport_->Selection();window.EndArmedTools();window.ClearPendingCommand();
    window.SelectTool(modeling::DrawingTool::Select);
    window.operationHost_->SetShelves({});
    auto* tool=new V2ImageTool(window);window.operationHost_->ShowTemporaryPage(tool,QStringLiteral("画像を貼る / 編集"));
    if(tool->EditSelectedImage())return tool;
    if(selection.ordered.size()==1)tool->SetTarget(selection.ordered.front());
    return tool;
}
V2ImageTool::V2ImageTool(V2MainWindow& window):QWidget(&window),window_(window),savedViews_(window.viewport_->ImageViews()) {
    setObjectName(QStringLiteral("imagePlacementPanel"));BuildUi();UseWorkPlane();qApp->installEventFilter(this);
}
void V2ImageTool::closeEvent(QCloseEvent* event) {
    window_.viewport_->SetImageViews({});Refresh(window_);window_.viewport_->HideToolRoleLabels();
    window_.viewport_->HideToolPreview();QWidget::closeEvent(event);
}
bool V2ImageTool::LoadImage(const QString& path) {
    QImageReader reader(path);reader.setAutoTransform(true);const auto size=reader.size();
    if(size.width()>16384||size.height()>16384||double(size.width())*size.height()>16777216){status_->setText(QStringLiteral("画像は1600万画素・各辺16384画素以内にしてください。"));return false;}
    const auto image=reader.read();if(image.isNull()){status_->setText(reader.errorString());return false;}
    if(double(image.width())*image.height()>16777216){status_->setText(QStringLiteral("画像が1600万画素を超えています。"));return false;}
    QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);
    if(!image.save(&buffer,"PNG")){status_->setText(QStringLiteral("画像をPNGで保存できません。"));return false;}
    image_=image;definition_.pngBase64=png.toBase64().toStdString();definition_.pixelWidth=image.width();definition_.pixelHeight=image.height();
    definition_.anchorPixel={0,double(image.height()),0};definition_.mmPerPixel=100.0/image.width();pixels_.clear();points_.clear();
    imageMarks_.clear();imageHover_.reset();role_=0;
    UpdateFields();Preview();return previewOk_;
}
void V2ImageTool::Preview() {
    if(updating_)return;
    if(definition_.pngBase64.empty()){previewOk_=false;status_->setText(QStringLiteral("画像ファイルを選んでください。"));return;}
    definition_.mmPerPixel=width_->value()/std::max(1,definition_.pixelWidth);
    definition_.rotationRad=rotation_->value()*3.141592653589793/180;definition_.opacity=1-opacity_->value()/100;
    definition_.mirrorHorizontal=mirror_->isChecked();
    definition_.followSurface=mode_->currentIndex()==1;
    auto images=savedViews_;if(!editing_.IsNil())std::erase_if(images,[&](const auto& v){return v.entityId==editing_;});
    V2ImageView view;view.entityId=editing_;QString error;previewOk_=MakeView(definition_,view,error);
    if(previewOk_){images.push_back(std::move(view));status_->setText(QStringLiteral("画像をドラッグして位置を調整できます。基準点・長さ合わせ・幅・角度を指定し、Enterで確定。"));}
    else status_->setText(error);
    window_.viewport_->SetImageViews(std::move(images));
    std::vector<V2Viewport::PlacedRoleLabel> labels{{definition_.origin,QStringLiteral("画像 基準点"),QColor(255,220,40),{},true}};
    for(std::size_t i=0;i<points_.size();++i)labels.push_back({points_[i],QStringLiteral("長さ %1").arg(i+1),QColor(255,220,40),{},true});
    for(std::size_t i=0;i<imageMarks_.size();++i)labels.push_back({imageMarks_[i],QStringLiteral("画像の点 %1").arg(i+1),QColor(80,230,255),{},true});
    window_.viewport_->ShowToolRoleLabels(std::move(labels));
    RefreshFitLine();
}
void V2ImageTool::RefreshFitLine() {
    if(previewOk_&&(role_==4||role_==5)&&imageMarks_.size()==2)
        window_.viewport_->ShowToolPreview({{imageMarks_[0],imageMarks_[1]}});
    else if(previewOk_&&role_==4&&imageMarks_.size()==1&&imageHover_)
        window_.viewport_->ShowToolPreview({{imageMarks_[0],*imageHover_}});
    else window_.viewport_->HideToolPreview();
}
bool V2ImageTool::Commit() {
    Preview();if(!previewOk_)return false;auto& window=window_;auto& doc=window.session_->GetDocument();
    if(!editing_.IsNil()){
        const auto* entity=doc.FindEntity(editing_);if(!entity){status_->setText(QStringLiteral("編集中の画像が削除されています。"));return false;}
        const auto result=doc.Run(document::UpdateFeatureDefinitionCommand(entity->createdBy,definition_,{},"画像を編集"));
        if(!result.committed){window.ReportDiagnostics(result.diagnostics);return false;}
    }else{
        domain::Feature f;f.id=window.ids_->NextTyped<base::IdKind::Feature>();f.type=domain::FeatureType::CreateImage;
        f.displayName=app::UniqueDisplayName(doc.Snapshot(),domain::EntityKind::Image,"画像");f.definition=definition_;
        domain::Entity e;e.id=window.ids_->NextTyped<base::IdKind::Entity>();e.kind=domain::EntityKind::Image;e.createdBy=f.id;e.displayName=f.displayName;
        f.outputs.push_back({"image",e.id,e.kind});const auto result=doc.Run(document::AddFeatureCommand(f,{e},"画像を貼る"));
        if(!result.committed){window.ReportDiagnostics(result.diagnostics);return false;}}
    window.operationHost_->SetShelves({});window.AdoptCurrentDocument();Refresh(window);return true;
}

bool V2ImageTool::EditSelectedImage() {
    const auto& selection=window_.viewport_->Selection();
    if(selection.entityIds.size()!=1)return false;
    const auto* entity=window_.session_->GetDocument().FindEntity(selection.entityIds.front());
    const auto* feature=entity?window_.session_->GetDocument().FindFeature(entity->createdBy):nullptr;
    const auto* image=feature?std::get_if<domain::CreateImageDefinition>(&feature->definition):nullptr;
    if(!image)return false;
    definition_=*image;editing_=entity->id;
    image_=QImage::fromData(QByteArray::fromBase64(QByteArray::fromStdString(image->pngBase64)),"PNG");
    face_={};
    if(!image->faceBrep.empty()){const auto shape=kernel::RestoreOutputShape(image->faceBrep);if(shape.HasValue())face_=shape.Value();}
    UpdateFields();Preview();return true;
}
