#include "V2LoopFacesTool.h"
#include "kachakacha/app/SurfaceNetwork.h"
#include "kachakacha/geometry/CurveSampling.h"

bool V2LoopFacesTool::TryNetworkPlan()
{
    using namespace kachakacha::v2;
    const auto tolerance = ToleranceNow();
    bool disconnected = false;
    std::vector<app::RoleWire> wires;
    for (const auto& selection : selections_) {
        wires.push_back({selection.sourceWireId, selection.segments});
        for (std::size_t i = 1; i < selection.segments.size(); ++i)
            disconnected = disconnected || geometry::Distance(selection.segments[i-1].EndPoint(),
                selection.segments[i].StartPoint()) > tolerance.interactiveJoinMm;
    }
    if (!disconnected) return false;
    networkTables_ = app::SurfaceNetworkTables(wires, tolerance);
    if (networkTables_.empty()) return false;
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
    plan_ = std::move(plan);
    methodOverride_.assign(plan_->faces.size(), std::nullopt);
    make_.assign(plan_->faces.size(), true);
    continuity_.assign(plan_->faces.size(), {});
    leaveGap_.clear();
    return true;
}
