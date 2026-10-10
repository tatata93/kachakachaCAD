#include "V2LoopFacesTool.h"
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
