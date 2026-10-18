#include "V2ProjectionTool.h"
#include "V2GeneratedCurves.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2Calculation.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/app/ExplorerModel.h"
#include "kachakacha/document/Commands.h"
#include <QLabel>
#include <QPushButton>
#include <QString>
using namespace kachakacha::v2;
bool V2ProjectionTool::PreviewRegions(const std::vector<modeling::KernelShapeHandle>& faces,
    const std::vector<kernel::WireProjectionInput>& inputs,const kernel::WireProjectionOptions& options) {
    const auto made=V2Calculation::Call(this,"窓の閉領域を選択フェイスへ投影しています…",
        [](const auto&... values){return kernel::ProjectNativeRegions(values...);},faces,inputs,options);
    if(!made.HasValue()){status_->setText(QString::fromStdString(made.FirstMessageJa()));return false;}
    if(window_.session_->GetDocument().Snapshot().id!=documentId_||window_.session_->GetDocument().Revision()!=revision_) {
        V2Calculation::Discard(made.Value());status_->setText(QStringLiteral("文書が変更されました。結果を破棄しました。"));return false;
    }
    regions_=made.Value().faces;result_=made.Value().boundary;previewRevision_=revision_;
    for(auto& run:result_->wires)run=V2CompactGeneratedCurves(run);
    std::vector<std::vector<geometry::Vector3>> lines,polygons;
    for(const auto& run:result_->wires)lines.push_back(geometry::SampleChain(run,.01));
    for(const auto& region:regions_) {
        const auto mesh=kernel::BuildShapeMesh(region);
        if(!mesh.HasValue()){ClearResult();status_->setText(QString::fromStdString(mesh.FirstMessageJa()));return false;}
        for(const auto& t:mesh.Value().triangles)polygons.push_back({t.points[0],t.points[1],t.points[2]});
    }
    window_.viewport_->ShowToolPreview(std::move(lines));window_.viewport_->SetToolPreviewFaces(std::move(polygons));
    status_->setText(QStringLiteral("面＋ワイヤー: %1面 / 閉輪郭 %2本 / 曲線変換の最大偏差上界 %3 mm。\n選択したフェイスの曲率・穴を維持。Enterで確定。").arg(regions_.size()).arg(result_->wires.size()).arg(result_->maxCurveDeviationMm,0,'g',5));
    confirm_->setEnabled(true);return true;
}
bool V2ProjectionTool::AddRegions(const std::vector<base::EntityId>& inputIds,std::vector<base::EntityId>& faceIds) {
    auto& document=window_.session_->GetDocument();
    for(const auto& region:regions_) {
        const auto bytes=kernel::CaptureOutputShape(region);if(!bytes.HasValue())return false;
        domain::Feature f;f.id=window_.ids_->NextTyped<base::IdKind::Feature>();f.type=domain::FeatureType::FreezeDerived;
        f.inputEntityIds=inputIds;f.displayName="投影した面";
        domain::FreezeDerivedDefinition definition;definition.frozenBrep=bytes.Value();f.definition=definition;
        domain::Entity e;e.id=window_.ids_->NextTyped<base::IdKind::Entity>();e.kind=domain::EntityKind::GuideSurface;
        e.createdBy=f.id;e.editPolicy=domain::EditPolicy::Frozen;e.groupId=document.Snapshot().settings.activeGroupId;
        e.displayName=app::UniqueDisplayName(document.Snapshot(),e.kind,f.displayName);f.outputs.push_back({"face",e.id,e.kind});
        if(!document.Run(document::AddFeatureCommand(f,{e},"窓の面投影")).committed)return false;
        faceIds.push_back(e.id);
    }
    return true;
}
