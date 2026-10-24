#include "V2LoopFacesTool.h"
#include "V2MainWindow.h"
#include "kachakacha/kernel/OcctSurfaceRegion.h"
#include "kachakacha/app/GroupTree.h"
#include "kachakacha/app/SurfaceNetwork.h"
#include "kachakacha/geometry/CurveSampling.h"
#include <algorithm>
#include <QString>

bool V2LoopFacesTool::TryNetworkPlan()
{
    using namespace kachakacha::v2;
    const auto tolerance = ToleranceNow();
    bool compound = false;
    std::vector<app::RoleWire> wires;
    for (const auto& selection : selections_) {
        wires.push_back({selection.sourceWireId, selection.segments});
        compound = compound || selection.segments.size() > 1;
    }
    // Network welding is useful for exact intersections, but a merely near
    // endpoint needs the ordinary gap UI and its explicit correction first.
    std::vector<geometry::Vector3> ends;
    for(const auto& wire:wires)for(const auto& curve:wire.segments){ends.push_back(curve.StartPoint());ends.push_back(curve.EndPoint());}
    for(std::size_t i=0;i<ends.size();++i) {
        bool exact=false,near=false;
        for(std::size_t j=0;j<ends.size();++j)if(i!=j) {
            const auto d=geometry::Distance(ends[i],ends[j]);
            exact=exact||d<=tolerance.modelLinearMm;near=near||d<=tolerance.interactiveJoinMm;
        }
        if(near&&!exact)return false;
    }
    auto network = app::PlanSurfaceNetwork(wires, tolerance);
    if (network.tables.empty()) return false;
    const bool holes=std::any_of(network.tables.begin(),network.tables.end(),[](const auto& table){
        return std::any_of(table.rows.begin(),table.rows.end(),[](const auto& row){return row.role==modeling::ChainRole::HoleBoundary;});
    });
    if (!compound&&!holes) return false;
    networkTables_ = std::move(network.tables);
    for(auto& table:networkTables_) AttachHoleSupports(table);
    for (const auto& selection : selections_) {
        const auto count = std::count_if(network.unusedSegments.begin(), network.unusedSegments.end(),
            [&](const auto& part) { return part.sourceWireId == selection.sourceWireId; });
        if (count == 0) continue;
        if (!networkUnusedJa_.isEmpty()) networkUnusedJa_ += QStringLiteral("、");
        networkUnusedJa_ += QString::fromStdString(selection.label) + QStringLiteral("（%1区間）").arg(static_cast<int>(count));
    }
    app::LoopFacePlan plan;
    for (const auto& table : networkTables_) {
        app::LoopFace face;
        face.method = table.method == modeling::GuideSurfaceMethod::PlanarBoundary ? app::LoopFaceMethod::Planar
            : table.method == modeling::GuideSurfaceMethod::FourEdgePatch ? app::LoopFaceMethod::FourEdge
            : app::LoopFaceMethod::BoundaryFill;
        face.sideCount = table.rows.size();
        for (const auto& row : table.rows)
            face.previewLines.push_back(geometry::SampleChain(row.segments, tolerance.interactiveJoinMm*0.5));
        plan.faces.push_back(std::move(face));
    }
    plan.summaryJa = "接続区画 " + std::to_string(plan.faces.size()) + "（曲面は近似）。元の線の区間を使い、線を変更しません";
    if (!network.unusedSegments.empty())
        plan.summaryJa += "。輪に使わない区間 " + std::to_string(network.unusedSegments.size()) + "（元の線は保持）";
    plan_ = std::move(plan);
    styles_.assign(plan_->faces.size(),modeling::FourEdgeStyle::Coons);
    methodOverride_.assign(plan_->faces.size(), std::nullopt);
    make_.assign(plan_->faces.size(), true);
    continuity_.assign(plan_->faces.size(), {});
    leaveGap_.clear();
    return true;
}

void V2LoopFacesTool::AttachHoleSupports(kachakacha::v2::modeling::GuideTable& table)
{
    using namespace kachakacha::v2;
    const auto tolerance=ToleranceNow();
        if(table.method!=modeling::GuideSurfaceMethod::BoundaryFill)return;
        std::vector<std::vector<geometry::CurveSegment>> loops(1);
        for(const auto& row:table.rows)if(row.role==modeling::ChainRole::BoundarySide)
            loops.front().insert(loops.front().end(),row.segments.begin(),row.segments.end());
        for(const auto& row:table.rows)if(row.role==modeling::ChainRole::HoleBoundary)loops.push_back(row.segments);
        if(loops.size()<2)return;
        const auto& snapshot=window_.session_->GetDocument().Snapshot();
        bool found=false;
        // Prefer visible supports, but hiding a source must not turn a curved
        // projected window into an unrelated refilled surface.
        for(const bool visible:{true,false}) {
        for(const auto* shapes:{&window_.guideShapes_,&window_.partShapes_}) {
            for(const auto& [key,handle]:*shapes) {
                const auto id=base::EntityId::Parse(key);if(!id)continue;
                const auto* entity=window_.session_->GetDocument().FindEntity(*id);
                if(!entity||(entity->kind!=domain::EntityKind::Part&&entity->kind!=domain::EntityKind::GuideSurface)
                    ||app::EntityEffectivelyVisible(snapshot,*entity)!=visible)continue;
                const auto cut=kernel::TrimSurfaceRegion({handle},loops,tolerance,tolerance.interactiveJoinMm);
                if(!cut.HasValue())continue;
                kernel::ReleaseShape(cut.Value());
                modeling::GuideTableRow source;source.role=modeling::ChainRole::SourceSurface;
                source.sourceWireIds={*id};source.sourceLabels={entity->displayName};table.rows.push_back(source);found=true;break;
            }
            if(found)break;
        }
        if(found)break;
        }
        if(!found) {
            // A spatial hole can cross several adjacent support faces. Do not
            // refill its outer wire as an unrelated surface when no single
            // entity contains the complete boundary.
            for(const bool visible:{true,false}) {
                std::vector<modeling::KernelShapeHandle> handles;
                std::vector<base::EntityId> ids;
                for(const auto* shapes:{&window_.guideShapes_,&window_.partShapes_})for(const auto& [key,handle]:*shapes) {
                    const auto id=base::EntityId::Parse(key);if(!id)continue;
                    const auto* entity=window_.session_->GetDocument().FindEntity(*id);
                    if(!entity||(visible&&!app::EntityEffectivelyVisible(snapshot,*entity)))continue;
                    handles.push_back(handle);ids.push_back(*id);
                }
                if(handles.size()<2)continue;
                const auto cut=kernel::TrimSurfaceRegion(handles,loops,tolerance,tolerance.interactiveJoinMm);
                if(!cut.HasValue())continue;
                kernel::ReleaseShape(cut.Value());
                // Record only supports that are actually necessary. Otherwise
                // unrelated visible parts become dependencies of this window.
                for(std::size_t at=0;at<handles.size()&&handles.size()>1;) {
                    auto trial=handles;trial.erase(trial.begin()+at);
                    const auto reduced=kernel::TrimSurfaceRegion(trial,loops,tolerance,tolerance.interactiveJoinMm);
                    if(reduced.HasValue()) {kernel::ReleaseShape(reduced.Value());handles=std::move(trial);ids.erase(ids.begin()+at);}
                    else ++at;
                }
                for(const auto& id:ids) {
                    modeling::GuideTableRow source;source.role=modeling::ChainRole::SourceSurface;
                    source.sourceWireIds={id};source.sourceLabels={window_.session_->GetDocument().FindEntity(id)->displayName};
                    table.rows.push_back(source);
                }
                found=true;break;
            }
        }
}
