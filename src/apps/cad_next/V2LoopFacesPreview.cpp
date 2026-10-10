#include "V2LoopFacesTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <QTimer>
#include <QString>
#include <QObject>
#include <algorithm>

using namespace kachakacha::v2;

V2LoopFacesTool::~V2LoopFacesTool()
{
    delete previewTimer_;
}

std::vector<app::LoopFaceMethod> V2LoopFacesTool::MethodChoices(std::size_t face) const
{
    const auto& item=plan_->faces[face];
    if (!networkTables_.empty()) {
        if (item.method==app::LoopFaceMethod::FourEdge)
            return {app::LoopFaceMethod::FourEdge,app::LoopFaceMethod::BoundaryFill};
        return {item.method};
    }
    return app::LoopFaceMethodChoices(item.sideCount,item.method==app::LoopFaceMethod::Planar,
        item.method==app::LoopFaceMethod::Loft);
}

modeling::GuideTable V2LoopFacesTool::NetworkTable(std::size_t face) const
{
    auto table=networkTables_[face];
    if (MethodOf(face)==app::LoopFaceMethod::BoundaryFill)
        table.method=modeling::GuideSurfaceMethod::BoundaryFill;
    return table;
}

void V2LoopFacesTool::ResetSurfacePreview()
{
    if (previewTimer_) previewTimer_->stop();
    if (previewPending_||previewReady_) window_.viewport_->SetToolPreviewFaces({});
    previewPending_=false;
    previewReady_=false;
    previewSummary_.clear();
    previewFaceStatus_.clear();
}

void V2LoopFacesTool::ScheduleSurfacePreview()
{
    ResetSurfacePreview();
    if (!plan_||plan_->faces.empty()) return;
    if (!previewTimer_) {
        previewTimer_=new QTimer(&window_);
        previewTimer_->setSingleShot(true);
        QObject::connect(previewTimer_,&QTimer::timeout,&window_,[this]{BuildSurfacePreview();});
    }
    previewPending_=true;
    previewSummary_=QStringLiteral("面を計算中…（入力を変更すると計算し直します）");
    previewFaceStatus_.assign(plan_->faces.size(),QStringLiteral("計算中…"));
    previewTimer_->start(0);
}

namespace {
struct PreviewShapes {
    std::vector<modeling::KernelShapeHandle> handles;
    ~PreviewShapes() {for (const auto handle:handles) kernel::ReleaseShape(handle);}
};
}

void V2LoopFacesTool::BuildSurfacePreview()
{
    if (previewTimer_) previewTimer_->stop();
    if (!Active()||!plan_||!previewPending_) return;
    previewPending_=false;
    dock_->SetProgressText(QStringLiteral("面を計算中…"));
    auto handles=window_.guideShapes_;
    PreviewShapes temporary;
    base::DeterministicIdGenerator ids{9271};
    std::vector<base::EntityId> builtIds(plan_->faces.size());
    std::vector<modeling::GuideTableSelection> pieces;
    for (const auto& piece:plan_->pieces)
        pieces.push_back({selections_[piece.source].sourceWireId,selections_[piece.source].label,piece.segments});
    modeling::ShapeMesh combined;
    bool failed=false;
    for (const auto at:BuildOrder()) {
        auto face=plan_->faces[at];face.method=MethodOf(at);
        std::vector<modeling::SurfaceContinuity> continuity;
        std::vector<base::EntityId> supports;
        EdgeSupports(at,builtIds,continuity,supports);
        const auto table=networkTables_.empty()?app::LoopFaceTable(pieces,face,ToleranceNow(),continuity,supports)
            :base::Result<modeling::GuideTable>::Success(NetworkTable(at));
        auto error=table.FirstSummaryJa();
        if (table.HasValue()) {
            auto styled=table.Value();styled.fourEdgeStyle=styles_[at];
            auto request=modeling::ToGuideSurfaceRequest(styled,ToleranceNow());
            error=request.FirstSummaryJa();
            if (request.HasValue()) {
                auto filled=request.Value();
                for (auto& chain:filled.chains) {
                    const auto found=handles.find(chain.supportSurfaceId.ToString());
                    if (found!=handles.end()) chain.supportShapeHandle=found->second.value;
                }
                const auto analysis=modeling::AnalyzeGuideSurfaceRequest(filled,ToleranceNow());
                error=analysis.FirstSummaryJa();
                if (analysis.HasValue()) {
                    const auto built=kernel::BuildGuideSurface(filled,analysis.Value(),ToleranceNow());
                    error=built.FirstSummaryJa();
                    if (built.HasValue()) {
                        temporary.handles.push_back(built.Value().handle);
                        builtIds[at]=ids.NextTyped<base::IdKind::Entity>();
                        handles[builtIds[at].ToString()]=built.Value().handle;
                        const auto mesh=kernel::BuildShapeMesh(built.Value().handle,0,8);
                        error=mesh.FirstSummaryJa();
                        if (mesh.HasValue()) {
                            combined.triangles.insert(combined.triangles.end(),mesh.Value().triangles.begin(),mesh.Value().triangles.end());
                            previewFaceStatus_[at]=QStringLiteral("下見OK / 最大ずれ %1 mm").arg(built.Value().maximumDeviationMm,0,'g',3);
                            continue;
                        }
                    }
                }
            }
        }
        failed=true;
        previewFaceStatus_[at]=QStringLiteral("生成不可: ")+QString::fromStdString(error);
    }
    for (std::size_t at=0;at<make_.size();++at) if (!make_[at]) previewFaceStatus_[at]=QStringLiteral("作らない");
    previewReady_=!failed&&!combined.triangles.empty();
    previewSummary_=previewReady_?QStringLiteral("面の下見を表示中。形を確認してEnterで確定します。")
        :QStringLiteral("面の下見を作れません。各候補の理由を確認し、入力・作り方を変更してください。");
    std::vector<std::vector<geometry::Vector3>> triangles;
    if (previewReady_) for (const auto& t:combined.triangles) triangles.push_back({t.points[0],t.points[1],t.points[2]});
    window_.viewport_->SetToolPreviewFaces(std::move(triangles),previewReady_?&combined:nullptr);
    ShowDock();
    window_.SetStatus(previewSummary_);
}
