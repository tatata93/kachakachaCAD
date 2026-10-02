#include <QPointF>
#include <QCheckBox>
#include <QString>
#include "V2MainWindow.h"
#include "V2OverlapBrowser.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/modeling/MeshPick.h"
#include "kachakacha/document/Commands.h"
#include <algorithm>
#include <limits>

namespace {
using namespace kachakacha::v2;
bool IsContact(app::BooleanKind kind)
{
    return kind == app::BooleanKind::ContactWire || kind == app::BooleanKind::TrimOverlap || kind == app::BooleanKind::SplitOverlap;
}
std::vector<QString> RegionNames(const kernel::ContactResult& result, bool wires)
{
    std::vector<QString> names;
    if (wires) {
        for (std::size_t i = 0; i < result.wires.size(); ++i) names.push_back(QStringLiteral("境界 %1").arg(i+1));
    } else {
        for (const auto& piece : result.pieces) names.push_back(QStringLiteral("%1 %2 / %3 mm³ (%4, %5, %6)")
            .arg(piece.inside ? QStringLiteral("重なり") : QStringLiteral("外側")).arg(piece.fragmentIndex+1)
            .arg(piece.volumeMm3,0,'f',3).arg(piece.center.x,0,'f',2).arg(piece.center.y,0,'f',2).arg(piece.center.z,0,'f',2));
    }
    return names;
}
void DrawContact(V2Viewport& viewport, const kernel::ContactResult& result,
    const std::vector<bool>& chosen, bool wireOnly, const kernel::ContactPiece* highlight)
{
    std::vector<std::vector<geometry::Vector3>> lines, faces;
    for (std::size_t i=0; i<result.wires.size(); ++i) {
        if (wireOnly && (i >= chosen.size() || !chosen[i])) continue;
        for (const auto& curve : result.wires[i]) {
            std::vector<geometry::Vector3> points;
            const int steps = curve.Kind() == geometry::CurveKind::Line ? 1 : 64;
            for (int k=0; k<=steps; ++k) points.push_back(curve.Evaluate(static_cast<double>(k)/steps));
            lines.push_back(std::move(points));
        }
    }
    for (std::size_t i=0; i<result.pieces.size(); ++i) {
        if (!chosen.empty() && (i >= chosen.size() || !chosen[i])) continue;
        const auto mesh = kernel::BuildShapeMesh(result.pieces[i].handle);
        if (!mesh.HasValue()) continue;
        lines.insert(lines.end(), mesh.Value().edges.begin(), mesh.Value().edges.end());
        if (highlight == nullptr) for (const auto& t : mesh.Value().triangles) faces.push_back({t.points[0],t.points[1],t.points[2]});
    }
    if (highlight != nullptr) {
        const auto mesh = kernel::BuildShapeMesh(highlight->handle);
        if (mesh.HasValue()) for (const auto& t : mesh.Value().triangles) faces.push_back({t.points[0],t.points[1],t.points[2]});
    }
    viewport.ShowToolPreview(std::move(lines));
    viewport.SetToolPreviewFaces(std::move(faces));
}
}

bool V2MainWindow::RefreshContactPreview()
{
    using namespace kachakacha::v2;
    if (!IsContact(booleanInput_.kind)) return false;
    contactBuilt_.reset(); booleanBuilt_.reset(); viewport_->HideToolPreview();
    std::vector<QString> status;
    booleanDock_->ShowInput(booleanInput_, {}, {}, {}, false); // configure selected tool before reading options
    const auto finish = [&](bool ready) {
        const auto name = [this](base::EntityId id) {
            const auto* e=session_->GetDocument().FindEntity(id);
            return e == nullptr ? QString() : QString::fromStdString(e->displayName);
        };
        booleanDock_->ShowInput(booleanInput_, name(booleanInput_.target),
            booleanInput_.tools.empty() ? QString() : name(booleanInput_.tools.front()), status, ready);
        ShowToolFooter(status.empty() ? QString() : status.front());
        ShowRoleLabels({{booleanInput_.target,"A"}});
        if (!booleanInput_.tools.empty()) ShowRoleLabels({{booleanInput_.target,"A"},{booleanInput_.tools.front(),"B"}});
    };
    if (booleanInput_.target.IsNil() || booleanInput_.tools.size()!=1) {
        status.push_back(QStringLiteral("対象Aと相手Bを1つずつ選んでください。複数の相手は一覧から組を選びます。"));
        contactBase_.reset(); finish(false); return true;
    }
    const auto a=partShapes_.find(booleanInput_.target.ToString()), b=partShapes_.find(booleanInput_.tools[0].ToString());
    if (a==partShapes_.end() || b==partShapes_.end()) { status.push_back(QStringLiteral("部品の実形状がありません。")); finish(false); return true; }
    const bool wire=booleanInput_.kind==app::BooleanKind::ContactWire, trim=booleanInput_.kind==app::BooleanKind::TrimOverlap;
    const bool wantWire=wire || booleanDock_->WantsContactWire();
    const QString selectionKey=QString::fromStdString(booleanInput_.target.ToString()+booleanInput_.tools[0].ToString())
        +QString::number(static_cast<int>(booleanInput_.kind));
    const QString key=selectionKey+QString::number(wantWire);
    const auto tolerance=session_->GetDocument().Snapshot().settings.tolerance.modelLinearMm;
    if (!contactBase_.has_value() || key!=contactPreviewKey_) {
        const auto built=kernel::BuildContact(a->second,b->second,wantWire,!wire&&!trim,!wire,tolerance);
        if (!built.HasValue()) { status.push_back(QString::fromStdString(built.FirstSummaryJa())); contactBase_.reset(); finish(false); return true; }
        contactBase_=built.Value(); contactPreviewKey_=key;
    }
    booleanDock_->SetContactRegions(selectionKey,RegionNames(*contactBase_,wire));
    const auto chosen=booleanDock_->ContactRegions();
    if (trim) {
        const auto built=kernel::BuildLocalTrim(a->second,b->second,booleanDock_->ContactRemovals(),tolerance);
        if (!built.HasValue()) { status.push_back(QString::fromStdString(built.FirstSummaryJa())); finish(false); return true; }
        contactBuilt_=built.Value(); contactBuilt_->wires=contactBase_->wires;
    } else contactBuilt_=*contactBase_;
    int index=booleanDock_->ContactRegionIndex();
    const auto* highlight=trim && index>=0 && index<static_cast<int>(contactBase_->pieces.size()) ? &contactBase_->pieces[index] : nullptr;
    DrawContact(*viewport_,*contactBuilt_,trim ? std::vector<bool>{} : chosen,wire,highlight);
    const bool any=trim || std::any_of(chosen.begin(),chosen.end(),[](bool value){return value;});
    status.push_back(any ? QStringLiteral("プレビュー中。確定するまで元の部品は変更しません。") : QStringLiteral("作る線／残す領域を選んでください。"));
    status.push_back(trim ? QStringLiteral("重なりをクリックまたは一覧で選び、削る側を指定してください。") : QStringLiteral("チェックしたものを生成します。境界ワイヤーは編集可能な独立コピーです。"));
    finish(any); return true;
}

bool V2MainWindow::ConfirmContact()
{
    using namespace kachakacha::v2;
    if (!IsContact(booleanInput_.kind)) return false;
    if (!contactBuilt_.has_value()) { RefreshContactPreview(); return true; }
    const auto built=*contactBuilt_; const auto input=booleanInput_;
    const auto chosen=booleanDock_->ContactRegions(); const auto masks=booleanDock_->ContactRemovals();
    const bool wire=input.kind==app::BooleanKind::ContactWire, trim=input.kind==app::BooleanKind::TrimOverlap;
    if (!trim && std::none_of(chosen.begin(),chosen.end(),[](bool v){return v;})) return true;
    document::Document::Transaction transaction(session_->GetDocument(),"接触部分の処理");
    std::vector<base::EntityId> hidden;
    for (std::size_t i=0; i<built.pieces.size(); ++i) {
        if (!trim && (i>=chosen.size() || !chosen[i])) continue;
        const auto& piece=built.pieces[i];
        domain::BooleanDefinition definition;
        definition.mode=trim ? 3 : (piece.inside ? 2 : 1);
        definition.targets={input.target}; definition.tools=input.tools;
        definition.fragmentIndex=piece.fragmentIndex; definition.contactSide=piece.sourceSide;
        if (trim) definition.contactRemovals=masks;
        if (AddPartFeature(domain::FeatureType::Boolean,definition,piece.handle,{},
            trim ? "めり込み除去" : "交わり分割",{input.target,input.tools[0]}).IsNil()) return true;
        const auto original=piece.sourceSide==0 ? input.target : input.tools[0];
        if (std::find(hidden.begin(),hidden.end(),original)==hidden.end()) hidden.push_back(original);
    }
    for (std::size_t i=0; i<built.wires.size(); ++i) {
        if (wire && (i>=chosen.size() || !chosen[i])) continue;
        const auto made=session_->AddWire(built.wires[i],false,"接触境界ワイヤー（独立）");
        if (!made.committed) { ReportDiagnostics(made.diagnostics); return true; }
    }
    if (!hidden.empty()) {
        const auto changed=session_->GetDocument().Run(document::SetVisibilityCommand(hidden,domain::Visibility::Hidden));
        if (!changed.committed) { ReportDiagnostics(changed.diagnostics); return true; }
    }
    if (!transaction.Commit()) return true;
    EndBoolean(); AdoptCurrentDocument(); SetStatus(QStringLiteral("接触部分の処理を確定しました。元の部品は履歴に残っています。"));
    return true;
}

void V2MainWindow::SwapContactInputs()
{
    if (booleanInput_.target.IsNil() || booleanInput_.tools.size()!=1) return;
    std::swap(booleanInput_.target,booleanInput_.tools[0]);
    contactBase_.reset(); MirrorBooleanToSelection(); RefreshBooleanAll();
}

bool V2MainWindow::PickContactRegion(const QPointF& point)
{
    using namespace kachakacha::v2;
    if (!booleanShelfShown_ || booleanInput_.kind!=app::BooleanKind::TrimOverlap
        || !contactBase_.has_value() || booleanInput_.activeSlot.has_value()) return false;
    const auto ray=viewport_->Mapping().RayThrough({point.x(),point.y()});
    if (!ray.has_value()) return false;
    int best=-1; double distance=std::numeric_limits<double>::max();
    for (std::size_t i=0;i<contactBase_->pieces.size();++i) {
        const auto mesh=kernel::BuildShapeMesh(contactBase_->pieces[i].handle);
        if (!mesh.HasValue()) continue;
        for (const auto& triangle:mesh.Value().triangles) {
            const auto hit=modeling::RayHitsTriangle(ray->origin,ray->direction,triangle);
            if (hit.has_value() && *hit<distance) {best=static_cast<int>(i);distance=*hit;}
        }
    }
    if (best<0) return false;
    booleanDock_->SelectContactRegion(best); return true;
}

void V2MainWindow::ShowOverlapBrowser()
{
    using namespace kachakacha::v2;
    std::vector<OverlapSource> sources;
    QString missing;
    for(const auto& entity:session_->GetDocument().Snapshot().entities) {
        if(entity.kind!=domain::EntityKind::Part) continue;
        const auto found=partShapes_.find(entity.id.ToString());
        if(found==partShapes_.end()) {missing+=QString::fromStdString(entity.displayName)+QStringLiteral(": 実形状なし。 ");continue;}
        const auto mesh=kernel::BuildShapeMesh(found->second);
        if(!mesh.HasValue()) {missing+=QString::fromStdString(entity.displayName)+QStringLiteral(": 表示形状なし。 ");continue;}
        sources.push_back({entity.id,QString::fromStdString(entity.displayName)
            +(entity.visibility==domain::Visibility::Hidden ? QStringLiteral(" [非表示]") : QString()),
            found->second,mesh.Value(),entity.visibility==domain::Visibility::Hidden});
    }
    EndArmedTools();
    SetStatus(QStringLiteral("めり込み部分を選び、残す／削る側を指定して Enter で確定します。"));
    OpenOverlapBrowser(*viewport_,*operationHost_,std::move(sources),session_->GetDocument().Snapshot().settings.tolerance.modelLinearMm,
        missing,[this](const OverlapSource& a,const OverlapSource& b,int region,app::BooleanKind kind,const std::vector<int>& masks) {
            const auto first=partShapes_.find(a.id.ToString()),second=partShapes_.find(b.id.ToString());
            if(first==partShapes_.end() || second==partShapes_.end() || first->second.value!=a.handle.value || second->second.value!=b.handle.value) {
                SetStatus(QStringLiteral("文書が変わったため、めり込み一覧を再検査してください。"));return;
            }
            EndArmedTools();RunBooleanTool(kind);
            booleanInput_.target=a.id;booleanInput_.tools={b.id};booleanInput_.activeSlot.reset();
            if(auto* option=booleanDock_->findChild<QCheckBox*>(QStringLiteral("contactWireAlso"))) {
                const bool blocked=option->blockSignals(true);option->setChecked(false);option->blockSignals(blocked);
            }
            MirrorBooleanToSelection();RefreshBooleanAll();
            if(kind==app::BooleanKind::TrimOverlap && contactBase_.has_value()) {
                for(std::size_t i=0;i<contactBase_->pieces.size();++i)booleanDock_->SetContactRemoval(static_cast<int>(i),i<masks.size() ? masks[i]:0);
                RefreshBooleanAll();
            }
            booleanDock_->SelectContactRegion(region);
            ConfirmContact();
        });
}
