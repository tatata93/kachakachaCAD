#include "V2CurvedEmbossTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include "kachakacha/app/SceneBuilder.h"
#include "kachakacha/app/ExplorerModel.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctBoolean.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/document/Commands.h"
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QString>
#include <algorithm>
#include <cmath>
using namespace kachakacha::v2;
V2CurvedEmbossTool::~V2CurvedEmbossTool()
{
    if(built_)kernel::ReleaseShape(built_->handle);
    if(supportShape_.Valid())kernel::ReleaseShape(supportShape_);
}
void V2CurvedEmbossTool::Invalidate()
{
    if(built_)kernel::ReleaseShape(built_->handle);
    built_.reset();window_.viewport_->HideToolPreview();confirm_->setEnabled(false);
    status_->setText(QStringLiteral("対象・設定を変更しました。プレビューを更新、またはEnterを押してください。"));
}
void V2CurvedEmbossTool::CancelInput()
{
    Invalidate();support_={};wires_.clear();
    if(supportShape_.Valid())kernel::ReleaseShape(supportShape_);supportShape_={};role_=0;
    sourceLabel_->setText(QStringLiteral("支持面: 未選択"));wireLabel_->setText(QStringLiteral("輪郭: 0本"));
    window_.viewport_->SetSelection({});status_->setText(QStringLiteral("入力を取り消しました。設定は維持しています。支持面を選んでください。"));
}
bool V2CurvedEmbossTool::SetSupport(const app::SelectionRef& ref)
{
    const auto* entity=window_.session_->GetDocument().FindEntity(ref.entityId);
    if(!entity)return false;
    modeling::KernelShapeHandle shape;
    if(entity->kind==domain::EntityKind::Part){
        const auto found=window_.partShapes_.find(ref.entityId.ToString());
        if(found==window_.partShapes_.end()||!ref.pickedFaceIndex){status_->setText(QStringLiteral("立体全体ではなく、支持面のフェイスを選んでください。"));return false;}
        const auto face=kernel::OutputFace(found->second,*ref.pickedFaceIndex);
        if(!face.HasValue()){status_->setText(QString::fromStdString(face.FirstSummaryJa()));return false;}shape=face.Value();
    }else if(entity->kind==domain::EntityKind::GuideSurface){
        const auto found=window_.guideShapes_.find(ref.entityId.ToString());if(found==window_.guideShapes_.end())return false;
        const auto brep=kernel::CaptureOutputShape(found->second);if(!brep.HasValue())return false;
        const auto copy=kernel::RestoreOutputShape(brep.Value());if(!copy.HasValue())return false;shape=copy.Value();
    }else {status_->setText(QStringLiteral("形状ガイドの面か、立体のフェイスを選んでください。"));return false;}
    if(supportShape_.Valid())kernel::ReleaseShape(supportShape_);
    support_=ref.entityId;supportShape_=shape;supportRevision_=window_.session_->GetDocument().Revision();Invalidate();
    sourceLabel_->setText(QStringLiteral("支持面: ")+QString::fromStdString(entity->displayName));role_=1;
    status_->setText(QStringLiteral("続けて曲面上の輪郭ワイヤーを選んでください。"));return true;
}
bool V2CurvedEmbossTool::AddWire(base::EntityId id)
{
    const auto* entity=window_.session_->GetDocument().FindEntity(id);
    if(!entity||entity->kind!=domain::EntityKind::Wire){status_->setText(QStringLiteral("輪郭にはワイヤーを選んでください。"));return false;}
    const auto found=std::find(wires_.begin(),wires_.end(),id);
    if(found==wires_.end())wires_.push_back(id);else wires_.erase(found);
    Invalidate();wireLabel_->setText(QStringLiteral("輪郭: %1本").arg(wires_.size()));return true;
}
bool V2CurvedEmbossTool::CollectBoundary(std::vector<geometry::CurveSegment>& boundary)
{
    std::vector<geometry::ChainInput> inputs;
    for(const auto& curve:window_.session_->Scene().curves)
        if(std::find(wires_.begin(),wires_.end(),curve.entityId)!=wires_.end())inputs.push_back({curve.entityId,curve.segmentId,curve.segment});
    const auto analysis=geometry::AnalyzeChain(inputs,window_.session_->GetDocument().Snapshot().settings.tolerance);
    if(!analysis.HasValue()||!analysis.Value().order.closed){status_->setText(QStringLiteral("輪郭は分岐のない閉じた輪にしてください。穴・複数の輪は別々に指定してください。"));return false;}
    for(const auto& item:analysis.Value().order.segments){
        const auto found=std::find_if(inputs.begin(),inputs.end(),[&](const auto& input){return input.segmentId==item.segmentId&&input.entityId==item.entityId;});
        if(found==inputs.end())return false;
        if(item.reversed){const auto reversed=geometry::ReverseCurve(found->segment);if(!reversed.HasValue())return false;boundary.push_back(reversed.Value());}
        else boundary.push_back(found->segment);
    }
    return true;
}
bool V2CurvedEmbossTool::Preview()
{
    Invalidate();
    if(window_.session_->GetDocument().Snapshot().id!=documentId_){status_->setText(QStringLiteral("文書が変わっています。道具を開き直してください。"));return false;}
    if(!supportShape_.Valid()||wires_.empty()){status_->setText(QStringLiteral("支持面と、その上の閉じたワイヤーを指定してください。"));return false;}
    if(supportRevision_!=window_.session_->GetDocument().Revision()){status_->setText(QStringLiteral("文書が変更されています。支持面を選び直してください。"));return false;}
    if(operation_->currentIndex()==1&&output_->currentIndex()==1){status_->setText(QStringLiteral("ワイヤーのみでは元の立体を変更しません。「新しい立体」を選んでください。"));return false;}
    std::vector<geometry::CurveSegment> boundary;if(!CollectBoundary(boundary))return false;
    constexpr double rad=3.141592653589793/180;
    kernel::CurvedEmbossOptions options;options.heightMm=height_->value();options.toleranceMm=tolerance_->value();
    options.draftRad=draftEnabled_->isChecked()?draft_->value()*rad:0;
    options.tiltRad=tiltEnabled_->isChecked()?tilt_->value()*rad:0;
    const auto frame=window_.viewport_->WorkPlane();
    options.tiltReference=frame.uAxis*std::cos(heading_->value()*rad)+frame.vAxis*std::sin(heading_->value()*rad);
    const auto result=kernel::BuildCurvedEmboss(supportShape_,boundary,options);
    if(!result.HasValue()){status_->setText(QString::fromStdString(result.FirstSummaryJa()+result.FirstDiagnostic().detailsJa));return false;}
    built_=result.Value();
    if(operation_->currentIndex()==1){
        const auto source=window_.partShapes_.find(support_.ToString());
        if(source==window_.partShapes_.end()){status_->setText(QStringLiteral("足す場合は立体のフェイスを支持面に選んでください。"));return false;}
        const auto united=kernel::BuildBoolean(kernel::BooleanOperation::Union,source->second,built_->handle,options.toleranceMm);
        if(!united.HasValue()){status_->setText(QString::fromStdString(united.FirstSummaryJa()));return false;}
        kernel::ReleaseShape(built_->handle);built_->handle=united.Value().handle;built_->volumeMm3=united.Value().volumeMm3;
    }
    const auto mesh=kernel::BuildShapeMesh(built_->handle);
    if(!mesh.HasValue()){status_->setText(QString::fromStdString(mesh.FirstSummaryJa()));return false;}
    window_.viewport_->ShowToolPreview(mesh.Value().edges);
    if(output_->currentIndex()!=1){std::vector<std::vector<geometry::Vector3>> faces;
        for(const auto& t:mesh.Value().triangles)faces.push_back({t.points[0],t.points[1],t.points[2]});
        window_.viewport_->SetToolPreviewFaces(std::move(faces),&mesh.Value());}
    status_->setText(QStringLiteral("曲面押し出し（近似）: 境界標本の最大偏差 %1 mm / 体積 %2 mm³。Enterで独立形状として確定。")
        .arg(built_->sampledBoundaryErrorMm,0,'g',5).arg(built_->volumeMm3,0,'g',7));confirm_->setEnabled(true);return true;
}
bool V2CurvedEmbossTool::Commit()
{
    if(!Preview())return false;
    const auto brep=kernel::CaptureOutputShape(built_->handle);
    const auto edges=kernel::OutputEdges(built_->handle,tolerance_->value());
    if(!brep.HasValue()||(output_->currentIndex()!=2&&!edges.HasValue())){status_->setText(QStringLiteral("生成形状を保存できません。"));return false;}
    auto& doc=window_.session_->GetDocument();document::Document::Transaction transaction(doc,"曲面押し出し");
    if(output_->currentIndex()!=2&&window_.AddPlainWire(edges.Value(),"曲面押し出し / 境界ワイヤー").IsNil())return false;
    if(output_->currentIndex()!=1){
        domain::Feature f;f.id=window_.ids_->NextTyped<base::IdKind::Feature>();f.type=domain::FeatureType::FreezeDerived;
        domain::FreezeDerivedDefinition definition;definition.frozenBrep=brep.Value();f.definition=definition;f.displayName="曲面押し出し（近似）";
        domain::Entity e;e.id=window_.ids_->NextTyped<base::IdKind::Entity>();e.kind=domain::EntityKind::Part;e.createdBy=f.id;
        e.editPolicy=domain::EditPolicy::Frozen;e.groupId=doc.Snapshot().settings.activeGroupId;
        e.displayName=app::UniqueDisplayName(doc.Snapshot(),e.kind,f.displayName);f.outputs.push_back({"part",e.id,e.kind});
        if(!doc.Run(document::AddFeatureCommand(f,{e},"曲面押し出し")).committed)return false;
        if(operation_->currentIndex()==1&&!doc.Run(document::SetVisibilityCommand({support_},domain::Visibility::Hidden)).committed)return false;
    }
    if(!transaction.Commit())return false;
    auto& window=window_;window.operationHost_->SetShelves({});window.AdoptCurrentDocument();window.RebuildKernelShapes();window.RefreshShapeViews();
    window.viewport_->SetSelection({});auto* next=Open(window);
    next->height_->setValue(height_->value());next->draft_->setValue(draft_->value());next->tilt_->setValue(tilt_->value());
    next->heading_->setValue(heading_->value());next->tolerance_->setValue(tolerance_->value());
    next->draftEnabled_->setChecked(draftEnabled_->isChecked());next->tiltEnabled_->setChecked(tiltEnabled_->isChecked());
    next->output_->setCurrentIndex(output_->currentIndex());next->operation_->setCurrentIndex(operation_->currentIndex());
    next->status_->setText(QStringLiteral("生成しました。設定を維持しています。次の支持面を選んでください。"));
    window.SetStatus(QStringLiteral("曲面押し出しを独立形状として生成しました。Undoでまとめて戻せます。"));return true;
}
