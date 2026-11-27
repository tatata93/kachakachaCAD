#include "V2FaceActions.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2ToolSelection.h"
#include "V2DrawingSelection.h"
#include "V2WireSource.h"
#include "V2QuickDisplayControls.h"
#include "kachakacha/app/GroupTree.h"
#include "kachakacha/app/PlaneFocus.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include <QAction>
#include <QMenu>
#include <algorithm>
#include <set>

namespace {
using namespace kachakacha::v2;
struct SourcePlan {
    app::SelectionSet lines;
    std::vector<base::EntityId> hidden;
    QString refusal;
    bool partial = false;
};

SourcePlan Resolve(const document::Document& document,const modeling::SnapScene& scene,
    const app::SelectionSet& selected,const std::function<bool(base::EntityId)>& singleFace)
{
    SourcePlan plan;
    const auto fail=[&](const QString& why) { SourcePlan failed;failed.refusal=why;return failed; };
    if (selected.entityIds.empty()) return fail(QStringLiteral("元の輪郭を選ぶ: 形状ガイドを選んでください。"));
    std::set<std::pair<base::EntityId,base::SegmentId>> seen;
    for (const auto id:selected.entityIds) {
        const auto* entity=document.FindEntity(id);
        const auto* feature=entity?document.FindFeature(entity->createdBy):nullptr;
        const auto* definition=feature?std::get_if<domain::CreateGuideSurfaceDefinition>(&feature->definition):nullptr;
        if (!entity || entity->kind!=domain::EntityKind::GuideSurface || !feature || !feature->enabled || !definition)
            return fail(QStringLiteral("元の輪郭を選ぶ: 元の線の参照がある形状ガイドだけを選んでください。"));
        if (definition->chains.size()!=definition->roles.size())
            return fail(QStringLiteral("元の輪郭の参照が不正です。選択と表示は変更していません。"));
        const bool faceOnly=std::any_of(selected.ordered.begin(),selected.ordered.end(),[&](const auto& ref) {
            return ref.entityId==id && ref.kind==app::SelectionElementKind::Face;
        });
        if (std::any_of(selected.ordered.begin(),selected.ordered.end(),[&](const auto& ref) {
            return ref.entityId==id && ((ref.kind!=app::SelectionElementKind::Face && ref.kind!=app::SelectionElementKind::Object)
                || (ref.kind==app::SelectionElementKind::Face && (!ref.pickedFaceIndex || *ref.pickedFaceIndex!=0)));
        })) return fail(QStringLiteral("元の輪郭を選ぶ: 左一覧から形状ガイド全体、または単一の面を選んでください。"));
        if (faceOnly && !singleFace(id)) return fail(QStringLiteral("複数面のうち1面だけの元線は特定できません。左一覧から形状ガイド全体を選んでください。"));
        bool boundaryFound=false;
        for (std::size_t i=0;i<definition->chains.size();++i) {
            const auto role=static_cast<modeling::ChainRole>(definition->roles[i]);
            if (role!=modeling::ChainRole::OuterBoundary && role!=modeling::ChainRole::HoleBoundary
                && role!=modeling::ChainRole::BoundarySide) continue;
            const auto& chain=definition->chains[i];
            const auto checked=app::ResolveGuideSegmentChain(document,scene,chain);
            if (!checked.HasValue() || chain.segments.empty()) return fail(QStringLiteral("元の輪郭の区間を確認できません。選択と表示は変更していません。"));
            boundaryFound=true;
            for (const auto& ref:chain.segments) {
                const auto* wire=document.FindEntity(ref.entityId);
                const auto* owner=wire?document.FindFeature(wire->createdBy):nullptr;
                if (!wire || wire->kind!=domain::EntityKind::Wire || !owner || !owner->enabled)
                    return fail(QStringLiteral("元の線が無効または削除されています。選択と表示は変更していません。"));
                if (!app::GroupChainVisible(document.Snapshot(),wire->groupId))
                    return fail(QStringLiteral("元の線のグループが非表示です。左一覧でグループを表示してください。グループの表示は変更していません。"));
                plan.partial=plan.partial || ref.startParameter!=0 || ref.endParameter!=1;
                if (!seen.emplace(ref.entityId,ref.segmentId).second) continue;
                const auto curves=v2ui::SourceWireCurves(document,scene,ref.entityId);
                const auto source=std::find_if(curves.begin(),curves.end(),[&](const auto& curve){return curve.segmentId==ref.segmentId;});
                if (source==curves.end()) return fail(QStringLiteral("元の線の形状を確認できません。選択と表示は変更していません。"));
                app::SelectionRef picked;
                picked.entityId=ref.entityId;picked.segmentId=ref.segmentId;
                picked.kind=app::SelectionElementKind::Edge;picked.curveParameter=.5;
                picked.hitPoint=source->segment.Evaluate(.5);
                plan.lines.ordered.push_back(picked);
                if (std::find(plan.lines.entityIds.begin(),plan.lines.entityIds.end(),ref.entityId)==plan.lines.entityIds.end()) {
                    plan.lines.entityIds.push_back(ref.entityId);
                    if (wire->visibility==domain::Visibility::Hidden) plan.hidden.push_back(ref.entityId);
                }
            }
        }
        if (!boundaryFound) return fail(QStringLiteral("この形状ガイドには元の輪郭の線が記録されていません。輪郭のコピーは作成していません。"));
    }
    return plan;
}
}

void V2FaceActions::AddSourceOutlineAction(V2MainWindow& window,QMenu& menu)
{
    const auto selected=window.viewport_->Selection();
    if (selected.entityIds.empty() || !std::all_of(selected.entityIds.begin(),selected.entityIds.end(),[&](const auto id) {
        const auto* entity=window.session_->GetDocument().FindEntity(id);
        return entity && entity->kind==kachakacha::v2::domain::EntityKind::GuideSurface;
    })) return;
    auto* action=menu.addAction(QStringLiteral("元の輪郭の線を表示して選ぶ"));
    action->setObjectName("selectSourceOutline");
    action->setToolTip(QStringLiteral("面を作るときに使った元の線を選びます。コピーは作りません。部分区間の場合は元セグメント全体を選びます。"));
    const auto document=window.session_->GetDocument().Snapshot().id;
    QObject::connect(action,&QAction::triggered,&window,[&window,selected,document] {
        if (window.session_->GetDocument().Snapshot().id!=document) {
            window.SetStatus(QStringLiteral("文書が変わりました。形状ガイドを選び直してください。"));return;
        }
        SelectSourceOutline(window,selected);
    });
}

bool V2FaceActions::SelectSourceOutline(V2MainWindow& window,const kachakacha::v2::app::SelectionSet& selected)
{
    using namespace kachakacha::v2;
    const auto plan=Resolve(window.session_->GetDocument(),window.session_->Scene(),selected,[&](const auto id) {
        const auto shape=DrawingShape(window,id);if(!shape)return false;
        const auto count=kernel::ShapeFaceCount(*shape);return count.HasValue() && count.Value()==1;
    });
    if (!plan.refusal.isEmpty()) { window.SetStatus(plan.refusal);return false; }
    if (PlaneWireDisplayActive(*window.viewport_)) {
        for (const auto id:plan.lines.entityIds) {
            const auto curves=v2ui::SourceWireCurves(window.session_->GetDocument(),window.session_->Scene(),id);
            if (std::any_of(curves.begin(),curves.end(),[&](const auto& curve) {
                return !app::CurveLiesOnPlane(curve.segment,window.viewport_->WorkPlane());
            })) {
                window.SetStatus(QStringLiteral("元の線が作図面の外にあります。『平面ワイヤのみ』を解除してから選んでください。選択と表示は変更していません。"));
                return false;
            }
        }
    }
    if (!plan.hidden.empty()) {
        const auto shown=window.session_->GetDocument().Run(document::SetVisibilityCommand(plan.hidden,domain::Visibility::Visible));
        if (!shown.committed) { window.ReportDiagnostics(shown.diagnostics);return false; }
        window.AdoptCurrentDocument();
    }
    // Stay in the current tool without feeding these wires into its pending roles.
    if (V2DrawingSelection::CanSelect(*window.session_,*window.viewport_)) V2DrawingSelection::Of(*window.viewport_).Select();
    else if (V2ToolSelection::HasTool(*window.viewport_) || window.viewport_->PickPending()
        || window.viewport_->ReferencePickPending()) V2ToolSelection::Begin(*window.viewport_);
    window.viewport_->IncludeInIsolation(plan.lines.entityIds);
    window.viewport_->SetSelection(plan.lines);
    window.SetStatus(QStringLiteral("元の輪郭の線 %1区間を選びました。コピーは作成していません。%2")
        .arg(plan.lines.ordered.size()).arg(plan.partial?QStringLiteral("部分区間を含むため、元セグメント全体を選択しています。") : QString{}));
    return true;
}
