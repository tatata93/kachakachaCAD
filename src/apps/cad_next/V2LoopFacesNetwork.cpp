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
    auto network = app::PlanSurfaceNetwork(wires, tolerance);
    if (network.tables.empty()) return false;
    const bool holes=std::any_of(network.tables.begin(),network.tables.end(),[](const auto& table){
        return std::any_of(table.rows.begin(),table.rows.end(),[](const auto& row){return row.role==modeling::ChainRole::HoleBoundary;});
    });
    if (!compound&&!holes) return false;
    networkTables_ = std::move(network.tables);
    // An existing native support preserves the actual body surface under a
    // projected window. Refilling only the outer boundary can change that shape.
    for(auto& table:networkTables_) {
        if(table.method!=modeling::GuideSurfaceMethod::BoundaryFill)continue;
        std::vector<std::vector<geometry::CurveSegment>> loops(1);
        for(const auto& row:table.rows)if(row.role==modeling::ChainRole::BoundarySide)
            loops.front().insert(loops.front().end(),row.segments.begin(),row.segments.end());
        for(const auto& row:table.rows)if(row.role==modeling::ChainRole::HoleBoundary)loops.push_back(row.segments);
        if(loops.size()<2)continue;
        const auto& snapshot=window_.session_->GetDocument().Snapshot();
        bool found=false;
        for(const auto* shapes:{&window_.guideShapes_,&window_.partShapes_}) {
            for(const auto& [key,handle]:*shapes) {
                const auto id=base::EntityId::Parse(key);if(!id)continue;
                const auto* entity=window_.session_->GetDocument().FindEntity(*id);
                if(!entity||(entity->kind!=domain::EntityKind::Part&&entity->kind!=domain::EntityKind::GuideSurface)
                    ||!app::EntityEffectivelyVisible(snapshot,*entity))continue;
                const auto cut=kernel::TrimSurfaceRegion({handle},loops,tolerance,tolerance.interactiveJoinMm);
                if(!cut.HasValue())continue;
                kernel::ReleaseShape(cut.Value());
                modeling::GuideTableRow source;source.role=modeling::ChainRole::SourceSurface;
                source.sourceWireIds={*id};source.sourceLabels={entity->displayName};table.rows.push_back(source);found=true;break;
            }
            if(found)break;
        }
    }
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
