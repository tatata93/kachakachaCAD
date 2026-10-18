#include "V2ProjectionTool.h"
#include "V2GeneratedCurves.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include "V2Calculation.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/CurveIntersection.h"
#include "kachakacha/app/ExplorerModel.h"
#include "kachakacha/document/Commands.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QString>
#include <algorithm>
using namespace kachakacha::v2;
namespace {
geometry::Vector3 InitialDirection(const std::vector<geometry::CurveSegment>& curves,
    const geometry::Vector3& fallback,double tolerance) {
    std::vector<geometry::Vector3> points;
    for(const auto& c:curves)for(int i=0;i<=4;++i)points.push_back(c.Evaluate(i/4.0));
    const auto plane=geometry::FitPlane(points);
    if(plane.valid&&std::all_of(curves.begin(),curves.end(),[&](const auto& c){
        return geometry::CurveLiesInPlane(c,plane.origin,plane.normal,tolerance);}))return -plane.normal;
    return fallback;
}
}
V2ProjectionTool::~V2ProjectionTool() {ClearResult();for(const auto& target:targets_)kernel::ReleaseShape(target.shape);}
void V2ProjectionTool::ClearResult() {
    result_.reset();for(const auto& region:regions_)kernel::ReleaseShape(region);regions_.clear();
}
void V2ProjectionTool::Invalidate() {
    ClearResult();confirm_->setEnabled(false);window_.viewport_->HideToolPreview();
    if(!sources_.empty()&&!targets_.empty())update_->start();
    else status_->setText(QStringLiteral("元ワイヤーと投影先の面を追加してください。"));
}
void V2ProjectionTool::RefreshRows() {
    const int row=wireRows_->currentRow();wireRows_->clear();targetRows_->clear();
    for(const auto& source:sources_) {
        const auto* e=window_.session_->GetDocument().FindEntity(source.ref.entityId);const auto d=source.input.direction;
        const auto name=e?QString::fromStdString(e->displayName):QStringLiteral("ワイヤー");
        wireRows_->addItem(mode_->currentIndex()==0?QStringLiteral("元 %5: %1 → (%2, %3, %4)").arg(name)
            .arg(d.x,0,'g',3).arg(d.y,0,'g',3).arg(d.z,0,'g',3).arg(wireRows_->count()+1):QStringLiteral("元 %1: %2").arg(wireRows_->count()+1).arg(name));
    }
    for(const auto& target:targets_) {
        const auto* e=window_.session_->GetDocument().FindEntity(target.ref.entityId);
        targetRows_->addItem((e?QString::fromStdString(e->displayName):QStringLiteral("面"))
            +(target.ref.pickedFaceIndex?QStringLiteral(" / フェイス %1").arg(*target.ref.pickedFaceIndex+1):QString()));
    }
    if(row>=0&&row<wireRows_->count())wireRows_->setCurrentRow(row);
    RefreshMarks();
}
bool V2ProjectionTool::AddWire(const app::SelectionRef& ref,bool toggle) {
    if(window_.session_->GetDocument().Snapshot().id!=documentId_||((!sources_.empty()||!targets_.empty())&&window_.session_->GetDocument().Revision()!=revision_)) {
        status_->setText(QStringLiteral("文書が変更されています。入力を取消して選び直してください。"));return false;
    }
    const auto* e=window_.session_->GetDocument().FindEntity(ref.entityId);
    if(!e||e->kind!=domain::EntityKind::Wire){status_->setText(QStringLiteral("元ワイヤーには線・円・曲線を選んでください。"));return false;}
    const auto same=[&](const auto& s){return s.ref.entityId==ref.entityId&&s.ref.segmentId==ref.segmentId;};
    auto found=std::find_if(sources_.begin(),sources_.end(),same);
    if(found!=sources_.end()){if(toggle)sources_.erase(found);else return true;}
    else {
        app::SelectionSet selection;selection.entityIds={ref.entityId};selection.ordered={ref};
        const auto curves=app::SelectedCurves(selection,window_.session_->Scene());if(curves.empty())return false;
        if(sources_.empty())sourceOrigin_=curves.front().StartPoint();
        auto direction=InitialDirection(curves,-window_.viewport_->WorkPlane().normal,
            window_.session_->GetDocument().Snapshot().settings.tolerance.modelLinearMm);
        if(!targets_.empty()&&geometry::Dot(targetPoint_-curves.front().Evaluate(.5),direction)<0)direction=-direction;
        sources_.push_back({ref,{curves,direction}});
    }
    revision_=window_.session_->GetDocument().Revision();RefreshRows();Invalidate();return true;
}
bool V2ProjectionTool::AddTarget(const app::SelectionRef& ref,bool toggle) {
    if(window_.session_->GetDocument().Snapshot().id!=documentId_||((!sources_.empty()||!targets_.empty())&&window_.session_->GetDocument().Revision()!=revision_)) {
        status_->setText(QStringLiteral("文書が変更されています。入力を取消して選び直してください。"));return false;
    }
    const auto* e=window_.session_->GetDocument().FindEntity(ref.entityId);if(!e)return false;
    const auto same=[&](const auto& t){return t.ref.entityId==ref.entityId&&t.ref.pickedFaceIndex==ref.pickedFaceIndex;};
    auto found=std::find_if(targets_.begin(),targets_.end(),same);
    if(found!=targets_.end()) {
        if(!toggle)return true;
        kernel::ReleaseShape(found->shape);targets_.erase(found);RefreshRows();Invalidate();return true;
    }
    modeling::KernelShapeHandle shape;
    if(e->kind==domain::EntityKind::Part) {
        const auto part=window_.partShapes_.find(ref.entityId.ToString());
        if(part==window_.partShapes_.end()||!ref.pickedFaceIndex){status_->setText(QStringLiteral("立体の投影先フェイスを3Dビューで選んでください。"));return false;}
        const auto face=kernel::OutputFace(part->second,*ref.pickedFaceIndex);if(!face.HasValue()){status_->setText(QString::fromStdString(face.FirstMessageJa()));return false;}shape=face.Value();
    } else if(e->kind==domain::EntityKind::GuideSurface) {
        const auto guide=window_.guideShapes_.find(ref.entityId.ToString());if(guide==window_.guideShapes_.end()){status_->setText(QStringLiteral("選んだ面の形状がありません。面の生成状態を確認してください。"));return false;}
        const auto bytes=kernel::CaptureOutputShape(guide->second);if(!bytes.HasValue()){status_->setText(QString::fromStdString(bytes.FirstMessageJa()));return false;}
        const auto copy=kernel::RestoreOutputShape(bytes.Value());if(!copy.HasValue()){status_->setText(QString::fromStdString(copy.FirstMessageJa()));return false;}shape=copy.Value();
    } else {status_->setText(QStringLiteral("投影先には形状ガイドの面か、ソリッドのフェイスを選んでください。"));return false;}
    auto placed=ref;const auto frame=kernel::OutputSurfaceFrame(shape,ref.hitPoint);
    if(frame.HasValue())placed.hitPoint=frame.Value().origin;
    if(targets_.empty()) {
        targetPoint_=frame.HasValue()?frame.Value().origin:ref.hitPoint;
        for(auto& source:sources_)if(!source.directionExplicit&&!source.input.curves.empty()
            &&geometry::Dot(targetPoint_-source.input.curves.front().Evaluate(.5),source.input.direction)<0)
            source.input.direction=-source.input.direction;
    }
    targets_.push_back({placed,shape});revision_=window_.session_->GetDocument().Revision();RefreshRows();Invalidate();return true;
}
void V2ProjectionTool::SetDirection(geometry::Vector3 direction) {
    direction=geometry::Normalized(direction);
    const int row=wireRows_->currentRow();
    for(std::size_t i=0;i<sources_.size();++i)if(row<0||i==static_cast<std::size_t>(row)){sources_[i].input.direction=direction;sources_[i].directionExplicit=true;}
    RefreshRows();Invalidate();
}
void V2ProjectionTool::CancelInput() {
    update_->stop();sources_.clear();for(const auto& target:targets_)kernel::ReleaseShape(target.shape);targets_.clear();
    SetPickRole(0);RefreshRows();Invalidate();window_.viewport_->SetSelection({});
    status_->setText(QStringLiteral("入力のみ取り消しました。方式・数値を維持しています。次のワイヤーを選べます。"));
}
bool V2ProjectionTool::Preview() {
    update_->stop();ClearResult();window_.viewport_->HideToolPreview();confirm_->setEnabled(false);
    if(window_.session_->GetDocument().Snapshot().id!=documentId_||window_.session_->GetDocument().Revision()!=revision_) {
        status_->setText(QStringLiteral("文書が変更されています。入力を取消して選び直してください。"));return false;
    }
    if(sources_.empty()||targets_.empty()){status_->setText(QStringLiteral("元ワイヤーと投影先の面を指定してください。"));return false;}
    kernel::WireProjectionOptions options;options.mode=static_cast<kernel::WireProjectionMode>(mode_->currentIndex());
    options.hits=output_->currentIndex()==1?kernel::WireProjectionHits::NearestPositive:
        static_cast<kernel::WireProjectionHits>(hits_->currentIndex());
    options.toleranceMm=tolerance_->value();options.maximumDistanceMm=distance_->value();
    options.joinToleranceMm=window_.session_->GetDocument().Snapshot().settings.tolerance.modelLinearMm;
    options.sourceOrigin=sourceOrigin_;options.sourceU=sourceU_;options.sourceV=sourceV_;options.targetPoint=targetPoint_;
    options.wrapRotationRad=rotation_->value()*3.141592653589793/180;
    std::vector<kernel::WireProjectionInput> inputs;std::vector<modeling::KernelShapeHandle> faces;
    for(const auto& source:sources_)inputs.push_back(source.input);for(const auto& target:targets_)faces.push_back(target.shape);
    status_->setText(QStringLiteral("投影を計算中…"));
    if(output_->currentIndex()==1) return PreviewRegions(faces,inputs,options);
    const auto made=V2Calculation::Call(this,"ワイヤーの投影を計算しています…",
        [](const auto&... values){return kernel::ProjectNativeWires(values...);},faces,inputs,options);
    if(!made.HasValue()){status_->setText(QString::fromStdString(made.FirstMessageJa()));return false;}
    if(window_.session_->GetDocument().Snapshot().id!=documentId_||window_.session_->GetDocument().Revision()!=revision_) {
        status_->setText(QStringLiteral("計算中に文書が変更されました。結果を破棄しました。"));return false;
    }
    result_=made.Value();previewRevision_=revision_;
    for(auto& run:result_->wires)run=V2CompactGeneratedCurves(run);
    std::vector<std::vector<geometry::Vector3>> lines;
    for(const auto& run:result_->wires)lines.push_back(geometry::SampleChain(run,0.01));
    window_.viewport_->ShowToolPreview(lines);
    status_->setText(QStringLiteral("プレビュー: %1本 / 曲線偏差 %2 mm / 面上の標本偏差 %3 mm\n未接続の端点 %4個。Enterで確定。巻付けの曲線偏差は補間標本、他方式は変換上界です。")
        .arg(result_->wires.size()).arg(result_->maxCurveDeviationMm,0,'g',5).arg(result_->maxSampledSurfaceDeviationMm,0,'g',5).arg(result_->openEnds));
    if(result_->openEnds)status_->setText(status_->text()+QStringLiteral("\n閉じていない輪郭があります。面の範囲・穴・複数面の深さ差を確認してください。端点を勝手に直線で結びません。"));
    confirm_->setEnabled(true);return true;
}
bool V2ProjectionTool::AddResults() {
    auto& document=window_.session_->GetDocument();document::Document::Transaction transaction(document,"曲面へ投影");
    std::vector<base::EntityId> inputIds;
    for(const auto& s:sources_)inputIds.push_back(s.ref.entityId);for(const auto& t:targets_)inputIds.push_back(t.ref.entityId);
    std::sort(inputIds.begin(),inputIds.end(),[](const auto& a,const auto& b){return a.ToString()<b.ToString();});
    inputIds.erase(std::unique(inputIds.begin(),inputIds.end()),inputIds.end());
    for(const auto& run:result_->wires) {
        domain::Feature feature;feature.id=window_.ids_->NextTyped<base::IdKind::Feature>();
        const bool ordinary=wireForm_->currentIndex()==1;
        feature.type=ordinary?domain::FeatureType::CreateWire:domain::FeatureType::ProjectWire;
        if(!ordinary)feature.inputEntityIds=inputIds;
        feature.displayName=mode_->currentIndex()==1?"巻き付け投影":"曲面へ投影";
        domain::CreateWireDefinition definition;definition.segments=run;
        for(std::size_t i=0;i<run.size();++i)definition.segmentIds.push_back(window_.ids_->NextTyped<base::IdKind::Segment>());
        feature.definition=definition;domain::Entity entity;entity.id=window_.ids_->NextTyped<base::IdKind::Entity>();
        entity.kind=domain::EntityKind::Wire;entity.createdBy=feature.id;
        entity.editPolicy=ordinary?domain::EditPolicy::Source:domain::EditPolicy::Derived;
        entity.groupId=document.Snapshot().settings.activeGroupId;
        entity.displayName=app::UniqueDisplayName(document.Snapshot(),entity.kind,feature.displayName);
        feature.outputs.push_back({"wire",entity.id,entity.kind});
        const auto added=document.Run(document::AddFeatureCommand(feature,{entity},"曲面へ投影"));
        if(!added.committed){window_.ReportDiagnostics(added.diagnostics);return false;}
    }
    std::vector<base::EntityId> faceIds;
    if(!AddRegions(inputIds,faceIds)||!transaction.Commit())return false;
    // Only transfer ownership after the document transaction succeeds. CancelInput releases
    // remaining preview shapes, while the registered faces now belong to the window.
    for(std::size_t i=0;i<faceIds.size();++i)window_.guideShapes_[faceIds[i].ToString()]=regions_[i];
    regions_.clear();
    return true;
}
bool V2ProjectionTool::Commit() {
    if(!result_){Preview();return false;}
    if(previewRevision_!=window_.session_->GetDocument().Revision()){Invalidate();return false;}
    const bool ordinary=wireForm_->currentIndex()==1;
    const int count=static_cast<int>(result_->wires.size()),faceCount=static_cast<int>(regions_.size()),openEnds=result_->openEnds;if(!AddResults())return false;
    CancelInput();revision_=window_.session_->GetDocument().Revision();window_.AdoptCurrentDocument();
    status_->setText(QStringLiteral("%1本を面へ落とし、ワイヤーを生成しました（面 %2枚）。設定を維持して次の投影を続けられます。").arg(count).arg(faceCount));
    status_->setText(status_->text()+(ordinary?QStringLiteral("\n通常ワイヤーとして位置と曲線を保持しました。編集できます。"):
        QStringLiteral("\n投影結果として位置と曲線を保持しました。")));
    if(faceCount)status_->setText(status_->text()+QStringLiteral("\n押し出すには、左ペインの「面」または3Dビューで投影した面を選び「押し出し」を使います。"));
    if(openEnds)status_->setText(status_->text()+QStringLiteral("\n開いた輪郭が含まれます。投影先の面の範囲・穴・深さ差を確認してください。"));
    window_.SetStatus(status_->text());return true;
}
