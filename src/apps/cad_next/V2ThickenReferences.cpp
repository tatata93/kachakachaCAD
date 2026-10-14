#include <QString>
#include <QStringList>
#include "V2MainWindow.h"
#include "V2ThickenPreviewState.h"
#include "V2ThickenDock.h"
#include "V2Viewport.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <algorithm>

void V2MainWindow::BindThickenInputs()
{
    using namespace kachakacha::v2;
    auto choices=ExtrudeTargets();
    const auto name=[this](const base::EntityId& id) {
        const auto* entity=session_->GetDocument().FindEntity(id);
        return entity ? QString::fromStdString(entity->displayName) : QStringLiteral("未選択");
    };
    QStringList surfaces;
    for(const auto& id:thicken_->input.surfaces) surfaces.push_back(name(id));
    thickenDock_->SetSurfaceItems(surfaces);
    for(const auto& entity:session_->GetDocument().Snapshot().entities)
        if(entity.kind==domain::EntityKind::GuideSurface
            &&std::find(thicken_->input.surfaces.begin(),thicken_->input.surfaces.end(),entity.id)==thicken_->input.surfaces.end())
            choices.push_back({entity.id,name(entity.id)});
    thickenDock_->SetTargets(choices);
    thickenDock_->ShowStop(name(thicken_->input.targetPlane)
        +(thicken_->input.targetFaceBrep.empty()?QString():QStringLiteral("：フェイス（選択時の形を保持）")));
    thickenDock_->armSurface=[this] {
        viewport_->CancelReferencePick(); thickenDock_->SetPickingStop(false);
        viewport_->SetToolPickActive(true);
        SetStatus(QStringLiteral("厚みを付ける面を追加してください。終端と設定は保持します。"));
    };
    thickenDock_->armStop=[this] {BeginThickenStopPick();};
    thickenDock_->removeSurface=[this](int i) {
        if(i<0||i>=static_cast<int>(thicken_->input.surfaces.size()))return;
        thicken_->input.surfaces.erase(thicken_->input.surfaces.begin()+i);
        MirrorThickenToSelection(); RefreshThickenAll();
    };
    thickenDock_->changeOffset=[this](double value) {thicken_->input.targetOffsetMm=value;RefreshThickenAll();};
}

void V2MainWindow::BeginThickenStopPick()
{
    using namespace kachakacha::v2;
    viewport_->CancelReferencePick(); thickenDock_->SetPickingStop(true);
    viewport_->BeginElementPick([this](const app::PickCandidate& pick) {
        const auto* entity=session_->GetDocument().FindEntity(pick.entityId);
        if(!entity)return false;
        if(entity->kind==domain::EntityKind::Part)
            return pick.kind==app::SelectionElementKind::Face&&pick.pickedFaceIndex.has_value();
        return entity->kind==domain::EntityKind::GuideSurface||WorkPlaneFrameOf(pick.entityId).has_value();
    },[this](std::optional<app::PickCandidate> pick) {
        thickenDock_->SetPickingStop(false);
        if(!pick)return;
        std::string brep;
        const auto* entity=session_->GetDocument().FindEntity(pick->entityId);
        if(!entity)return;
        if(entity->kind==domain::EntityKind::Part) {
            const auto shape=partShapes_.find(pick->entityId.ToString());
            if(shape==partShapes_.end()||!pick->pickedFaceIndex)return;
            const auto face=kernel::OutputFace(shape->second,*pick->pickedFaceIndex);
            if(!face.HasValue()){ReportDiagnostics(face.Diagnostics());return;}
            const auto captured=kernel::CaptureOutputShape(face.Value()); kernel::ReleaseShape(face.Value());
            if(!captured.HasValue()){ReportDiagnostics(captured.Diagnostics());return;}
            brep=captured.Value();
        }
        thicken_->input.targetPlane=pick->entityId;
        thicken_->input.targetFaceBrep=std::move(brep);
        RefreshThickenAll();
    });
    SetStatus(QStringLiteral("終端の面を選択中。3Dのフェイス／面、左ペインの面／作業平面を選んでください。"));
}
