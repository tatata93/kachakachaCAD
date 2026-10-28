#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2BooleanDock.h"
#include "V2TrimSourcePolicy.h"
#include <QCheckBox>
#include "V2Calculation.h"
#include "kachakacha/kernel/OcctRegionSplit.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/document/Commands.h"
#include <QComboBox>
#include <QString>
#include <QVariant>
#include <algorithm>
using namespace kachakacha::v2;
namespace {
struct TemporaryShapes {
    std::vector<modeling::KernelShapeHandle> handles;
    ~TemporaryShapes(){for(auto h:handles)kernel::ReleaseShape(h);}
};
}
bool V2MainWindow::RefreshMixedTrimPreview() {
    auto& document=session_->GetDocument();const auto& input=booleanInput_;
    const double tolerance=document.Snapshot().settings.tolerance.modelLinearMm;
    const auto* target=document.FindEntity(input.target);const auto* tool=document.FindEntity(input.tools.front());
    TemporaryShapes temporary;
    auto direction=viewport_->WorkPlane().normal;
    if(const auto* combo=findChild<QComboBox*>("trimCutDirection")) {
        if(combo->currentIndex()==1)direction={1,0,0};
        if(combo->currentIndex()==2)direction={0,1,0};
        if(combo->currentIndex()==3)direction={0,0,1};
    }
    const auto finish=[&](const QString& text,bool ready){
        booleanDock_->ShowInput(input,QString::fromStdString(target->displayName),QString::fromStdString(tool->displayName),{text},ready);
        if (auto* keep = findChild<QCheckBox*>(QStringLiteral("trimKeepOriginalMixed")))
            keep->setVisible(target->kind == domain::EntityKind::Wire);
        ShowRoleLabels({{input.target,"対象"},{input.tools.front(),"切断要素"}});ShowToolFooter(text);
    };
    const auto curves=[&](base::EntityId id){std::vector<geometry::CurveSegment> values;
        for(const auto& c:session_->Scene().curves)if(c.entityId==id)values.push_back(c.segment);return values;};
    const auto resolve=[&](base::EntityId id,modeling::KernelShapeHandle baseShape,bool isTool) {
        using Out=base::Result<modeling::KernelShapeHandle>;
        for(const auto* shapes:{&partShapes_,&guideShapes_})if(const auto found=shapes->find(id.ToString());found!=shapes->end()) {
            const auto* scope=findChild<QComboBox*>(isTool?"trimToolScope":"trimTargetScope");
            if(scope&&scope->currentIndex()==1) {
                const auto index=booleanDock_->property(("trimFace_"+id.ToString()).c_str());
                if(!index.isValid())return Out::Failure(base::MakeError("UI-TR02","3Dビューでフェイスの内側を選んでください。",{}));
                const auto face=kernel::OutputFace(found->second,index.toUInt());
                if(face.HasValue())temporary.handles.push_back(face.Value());return face;
            }
            return Out::Success(found->second);
        }
        Out made=Out::Failure(base::MakeError("UI-TR01","この種類を加工対象にはできません。",{}));
        const auto* entity=document.FindEntity(id);
        const auto* targetScope=findChild<QComboBox*>("trimTargetScope");
        if(entity->kind==domain::EntityKind::Wire)
            made=kernel::RegionSplitWire(curves(id),baseShape,direction,isTool&&target->kind==domain::EntityKind::Part
                &&(!targetScope||targetScope->currentIndex()==0),tolerance);
        else if(isTool) {
            if(const auto plane=WorkPlaneFrameOf(id))made=kernel::RegionSplitPoint(plane->origin,plane->normal,true);
            else for(const auto& point:session_->Scene().points)if(point.entityId==id)
                made=kernel::RegionSplitPoint(point.position,direction,target->kind!=domain::EntityKind::Wire);
        }
        if(made.HasValue())temporary.handles.push_back(made.Value());return made;
    };
    const auto revision=document.Revision();
    const auto scopeKey=[&](const char* name,base::EntityId id){const auto* scope=findChild<QComboBox*>(name);
        return QString::number(scope?scope->currentIndex():0)+":"+booleanDock_->property(("trimFace_"+id.ToString()).c_str()).toString();};
    const auto key=QString::fromStdString(input.target.ToString()+input.tools.front().ToString())
        +QStringLiteral("/%1/%2/%3/%4/%5/%6/%7").arg(revision).arg(direction.x).arg(direction.y).arg(direction.z)
            .arg(booleanDock_->WantsContactWire()).arg(scopeKey("trimTargetScope",input.target)).arg(scopeKey("trimToolScope",input.tools.front()));
    if(!contactBase_||!contactBase_->frozen||contactPreviewKey_!=key) {
        const auto a=resolve(input.target,{},false);
        if(!a.HasValue()){finish(QString::fromStdString(a.FirstMessageJa()),false);return true;}
        const auto b=resolve(input.tools.front(),a.Value(),true);
        if(!b.HasValue()){finish(QString::fromStdString(b.FirstMessageJa()),false);return true;}
        const auto made=V2Calculation::Call(this,"交点で区画を分割しています…",
            [](const auto&... args){return kernel::SplitNativeRegions(args...);},a.Value(),b.Value(),booleanDock_->WantsContactWire(),tolerance);
        if(document.Revision()!=revision){finish(QStringLiteral("文書が変更されました。選び直してください。"),false);return true;}
        if(!made.HasValue()){contactBase_.reset();finish(QString::fromStdString(made.FirstMessageJa()),false);return true;}
        contactBase_=made.Value();contactPreviewKey_=key;
    }
    contactBuilt_=*contactBase_;std::vector<QString> names;
    for(const auto& p:contactBuilt_->pieces)names.push_back(QStringLiteral("区画 %1 / %2 %3").arg(names.size()+1)
        .arg(p.volumeMm3,0,'g',5).arg(p.dimension==3?QStringLiteral("mm³"):p.dimension==2?QStringLiteral("mm²"):QStringLiteral("mm")));
    booleanDock_->SetContactRegions(key,names);const auto chosen=booleanDock_->ContactRegions();
    std::vector<std::vector<geometry::Vector3>> lines,faces;
    for(std::size_t i=0;i<contactBuilt_->pieces.size();++i)if(i<chosen.size()&&chosen[i]) {
        const auto mesh=kernel::BuildShapeMesh(contactBuilt_->pieces[i].handle);
        if(!mesh.HasValue()){contactBuilt_.reset();finish(QStringLiteral("区画を表示できません。")+QString::fromStdString(mesh.FirstMessageJa()),false);return true;}
        lines.insert(lines.end(),mesh.Value().edges.begin(),mesh.Value().edges.end());
        for(const auto& t:mesh.Value().triangles)faces.push_back({t.points[0],t.points[1],t.points[2]});
    }
    viewport_->ShowToolPreview(std::move(lines));viewport_->SetToolPreviewFaces(std::move(faces));
    finish(target->kind == domain::EntityKind::Wire
        ? QStringLiteral("残す区画をチェックしてください。元の線は「元の線を残す」がオフなら削除します。切断要素は残し、結果は独立した線にします。")
        : QStringLiteral("残す区画をチェックしてください。切断要素と元形状は保持し、確定結果を独立形状として保存します。"),
        std::any_of(chosen.begin(),chosen.end(),[](bool value){return value;}));return true;
}
bool V2MainWindow::ConfirmMixedTrim() {
    RefreshContactPreview();
    if(!contactBuilt_||!contactBuilt_->frozen)return true;
    const auto built=*contactBuilt_;const auto chosen=booleanDock_->ContactRegions();
    if(std::none_of(chosen.begin(),chosen.end(),[](bool value){return value;}))return true;
    auto& document=session_->GetDocument();
    const auto* original = document.FindEntity(booleanInput_.target);
    const bool wireTarget = original && original->kind == domain::EntityKind::Wire;
    const bool keep = v2ui::TrimKeepsOriginal(*this, "trimKeepOriginalMixed");
    auto removal = v2ui::PrepareTrimSourceRemoval(*this, {booleanInput_.target}, !wireTarget || keep,
        [this] { RefreshShapeViews(); });
    if (!removal.accepted) { SetStatus(removal.message); return true; }
    v2ui::TrimGenerationTransaction transaction(*session_,"区画トリム");
    if (removal.command && !document.Run(*removal.command).committed) return true;
    std::vector<std::pair<base::EntityId,modeling::KernelShapeHandle>> surfaces;
    for(std::size_t i=0;i<built.pieces.size();++i)if(i<chosen.size()&&chosen[i]) {
        const auto& piece=built.pieces[i];
        if(piece.dimension==1) {
            const auto curves=kernel::OutputEdges(piece.handle,document.Snapshot().settings.tolerance.modelLinearMm);
            if(!curves.HasValue()||!session_->AddWire(curves.Value(),false,"トリムしたワイヤー").committed)return true;
            continue;
        }
        const auto bytes=kernel::CaptureOutputShape(piece.handle);if(!bytes.HasValue())return true;
        domain::FreezeDerivedDefinition definition;definition.frozenBrep=bytes.Value();
        definition.sources={booleanInput_.target,booleanInput_.tools.front()};
        domain::Feature feature;feature.id=ids_->NextTyped<base::IdKind::Feature>();feature.type=domain::FeatureType::FreezeDerived;
        feature.definition=definition;feature.inputEntityIds=definition.sources;feature.displayName="トリムした区画";
        domain::Entity entity;entity.id=ids_->NextTyped<base::IdKind::Entity>();entity.createdBy=feature.id;
        entity.kind=piece.dimension==3?domain::EntityKind::Part:domain::EntityKind::GuideSurface;
        entity.displayName=feature.displayName+" "+std::to_string(i+1);entity.editPolicy=domain::EditPolicy::Frozen;
        entity.groupId=document.Snapshot().settings.activeGroupId;feature.outputs.push_back({"region",entity.id,entity.kind});
        if(!document.Run(document::AddFeatureCommand(feature,{entity},"区画トリム")).committed)return true;
        surfaces.push_back({entity.id,piece.handle});
    }
    for(const auto& wire:built.wires)if(!session_->AddWire(wire,false,"切断境界ワイヤー（独立）").committed)return true;
    if(!wireTarget && !document.Run(document::SetVisibilityCommand({booleanInput_.target},domain::Visibility::Hidden)).committed)return true;
    if(!transaction.Commit())return true;
    for(const auto& [id,handle]:surfaces) {
        if(document.FindEntity(id)->kind==domain::EntityKind::Part)partShapes_[id.ToString()]=handle;
        else guideShapes_[id.ToString()]=handle;
        if(built.ownedShapes)std::erase_if(*built.ownedShapes,[&](auto owned){return owned.value==handle.value;});
    }
    EndBoolean();AdoptCurrentDocument();RefreshPartEdges();RefreshShapeViews();
    SetStatus(QStringLiteral("選んだ区画を保存しました。1回のUndoで元に戻せます。"));return true;
}
