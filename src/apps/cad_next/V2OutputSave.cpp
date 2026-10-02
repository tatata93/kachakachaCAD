#include <QString>
#include "V2OutputTool.h"
#include "V2MainWindow.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/kernel/OcctOutput.h"
#include <QComboBox>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
using namespace kachakacha::v2;
namespace {
base::Result<domain::Feature> OutputFeature(const V2OutputAsset& asset,base::IdGenerator& ids,domain::Entity& entity) {
    using Out=base::Result<domain::Feature>;
    entity=asset.entity;entity.id=ids.NextTyped<base::IdKind::Entity>();entity.groupId.reset();entity.generatedFrom.reset();entity.revision=0;entity.visibility=domain::Visibility::Visible;
    domain::Feature feature;feature.id=ids.NextTyped<base::IdKind::Feature>();feature.displayName=entity.displayName;
    entity.createdBy=feature.id;feature.outputs.push_back({"output",entity.id,entity.kind});
    if(entity.kind==domain::EntityKind::Part || entity.kind==domain::EntityKind::GuideSurface){
        const auto brep=kernel::CaptureOutputShape(asset.shape);if(!brep.HasValue())return Out::Failure(brep.Diagnostics());
        domain::FreezeDerivedDefinition frozen;frozen.frozenBrep=brep.Value();feature.definition=frozen;feature.type=domain::FeatureType::FreezeDerived;entity.editPolicy=domain::EditPolicy::Frozen;
    }else if(entity.kind==domain::EntityKind::Wire){domain::CreateWireDefinition wire;wire.segments=asset.curves;wire.construction=entity.construction;
        for(std::size_t i=0;i<wire.segments.size();++i)wire.segmentIds.push_back(ids.NextTyped<base::IdKind::Segment>());
        feature.definition=wire;feature.type=domain::FeatureType::CreateWire;entity.editPolicy=domain::EditPolicy::Source;
    }else if(entity.kind==domain::EntityKind::Point){domain::CreatePointDefinition point;point.positionMm=asset.frame.origin;feature.definition=point;feature.type=domain::FeatureType::CreatePoint;entity.editPolicy=domain::EditPolicy::Source;
    }else if(entity.kind==domain::EntityKind::WorkPlane){domain::CreateWorkPlaneDefinition plane;plane.origin=asset.frame.origin;plane.normal=asset.frame.normal;plane.uDirection=asset.frame.xDirection;feature.definition=plane;feature.type=domain::FeatureType::CreateWorkPlane;entity.editPolicy=domain::EditPolicy::Source;
    }else return Out::Failure(base::MakeError("EXP-P003","この種類は独立コピーにできません。",entity.displayName));
    return Out::Success(std::move(feature));
}
bool AppendOutputs(document::Document& document,base::IdGenerator& ids,const std::vector<V2OutputAsset>& assets,QString& error) {
    document::Document::Transaction transaction(document,"選択形状を出力");
    for(const auto& asset:assets){domain::Entity entity;const auto feature=OutputFeature(asset,ids,entity);
        if(!feature.HasValue()){error=QString::fromStdString(feature.FirstSummaryJa());return false;}
        const auto added=document.Run(document::AddFeatureCommand(feature.Value(),{entity},"独立コピー"));
        if(!added.committed){error=QStringLiteral("独立コピーを文書に追加できません。");return false;}}
    return transaction.Commit();
}
}
bool V2OutputTool::CopyIntoDocument() {
    QString error;if(!AppendOutputs(window_.session_->GetDocument(),*window_.ids_,prepared_,error)){SetProblem(error);return false;}
    window_.AdoptCurrentDocument();window_.RebuildKernelShapes();window_.RefreshShapeViews();window_.SetStatus(QStringLiteral("選んだ形状の独立コピーを作成しました。元の対象は残しています。"));return true;
}
bool V2OutputTool::SaveTo(const QString& path) {
    if(window_.session_->GetDocument().Revision()!=revision_ || window_.session_->GetDocument().Snapshot().id!=documentId_){SetProblem(QStringLiteral("文書が変更されています。対象を取り込み直してプレビューを確認してください。"));return false;}
    RefreshPreview();if(!save_->isEnabled())return false;
    if(format_->currentIndex()==3)return CopyIntoDocument();
    if(path.isEmpty())return false;
    if(!window_.documentPath_.isEmpty() && QFileInfo(path).absoluteFilePath().compare(QFileInfo(window_.documentPath_).absoluteFilePath(),Qt::CaseInsensitive)==0){SetProblem(QStringLiteral("現在開いているKCDを出力先に上書きできません。別名にしてください。"));return false;}
    std::string bytes;
    if(format_->currentIndex()==2){auto settings=window_.session_->GetDocument().Snapshot().settings;settings.activeGroupId.reset();
        document::Document document(window_.ids_->NextTyped<base::IdKind::Document>(),settings);QString error;
        if(!AppendOutputs(document,*window_.ids_,prepared_,error)){SetProblem(error);return false;}
        io::DocumentFile file;file.snapshot=document.Snapshot();file.metadata.title=QFileInfo(path).fileName().toStdString();
        const auto archive=io::SaveDocument(file);if(!archive.HasValue()){SetProblem(QString::fromStdString(archive.FirstMessageJa()));return false;}bytes=archive.Value();
    }else {std::vector<modeling::KernelShapeHandle> shapes;
        for(const auto& asset:prepared_){if(!asset.curves.empty()){const auto shape=kernel::OutputCurveShape(asset.curves);if(!shape.HasValue()){SetProblem(QString::fromStdString(shape.FirstSummaryJa()));return false;}shapes.push_back(shape.Value());}else shapes.push_back(asset.shape);}
        const auto content=format_->currentIndex()==0?kernel::OutputStl(shapes,0.01):kernel::OutputStep(shapes);
        if(!content.HasValue()){SetProblem(QString::fromStdString(content.FirstSummaryJa()));return false;}bytes=content.Value();}
    const auto written=io::WriteFileAtomically(path.toStdString(),bytes);if(!written.HasValue()){SetProblem(QString::fromStdString(written.FirstMessageJa()));return false;}
    window_.SetStatus(QStringLiteral("プレビューした%1個の形状を%2へ出力しました。").arg(prepared_.size()).arg(path));return true;
}
