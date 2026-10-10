#include "kachakacha/app/SurfaceNetwork.h"
#include "kachakacha/app/LoopFaces.h"
#include "kachakacha/app/ProfileNetwork.h"
#include "kachakacha/app/ProfileRegion.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/geometry/WireEdit.h"
#include <algorithm>

namespace kachakacha::v2::app {
namespace {
modeling::GuideTable NetworkFaceTable(const std::vector<modeling::GuideTableSelection>& selections,
    const LoopFace& face)
{
    modeling::GuideTable table;
    const bool flat = face.method == LoopFaceMethod::Planar;
    const bool four = face.method == LoopFaceMethod::FourEdge && face.sideCount == 4
        && face.sideOf.size() == face.selections.size();
    table.method = flat ? modeling::GuideSurfaceMethod::PlanarBoundary
        : four ? modeling::GuideSurfaceMethod::FourEdgePatch : modeling::GuideSurfaceMethod::BoundaryFill;
    std::size_t start = 0;
    if (four) {
        for (std::size_t i = 0; i < face.selections.size(); ++i)
            if (face.sideOf[i] == 0 && face.sideOf[(i + face.sideOf.size() - 1) % face.sideOf.size()] != 0)
                start = i;
    }
    for (std::size_t n = 0; n < face.selections.size(); ++n) {
        const auto i = (start + n) % face.selections.size();
        const auto prev = (i + face.selections.size() - 1) % face.selections.size();
        if (n == 0 || (!flat && (!four || face.sideOf[i] != face.sideOf[prev]))) {
            modeling::GuideTableRow row;
            row.role = flat ? modeling::ChainRole::OuterBoundary : modeling::ChainRole::BoundarySide;
            table.rows.push_back(row);
        }
        const auto& chosen = selections[face.selections[i]];
        auto& row = table.rows.back();
        if (std::find(row.sourceWireIds.begin(), row.sourceWireIds.end(), chosen.sourceWireId)
            == row.sourceWireIds.end()) {
            row.sourceWireIds.push_back(chosen.sourceWireId);
            row.sourceLabels.push_back(chosen.label);
        }
        auto segments = chosen.segments;
        if (!face.forward[i]) {
            std::reverse(segments.begin(), segments.end());
            for (auto& segment : segments) {
                const auto reversed = geometry::ReverseCurve(segment);
                if (!reversed.HasValue()) return {};
                segment = reversed.Value();
            }
        }
        row.segments.insert(row.segments.end(), segments.begin(), segments.end());
    }
    return table;
}

bool SameSegment(const geometry::CurveSegment& a,const geometry::CurveSegment& b,double eps)
{
    bool same=true,reverse=true;
    for(double t:{0.0,0.25,0.5,0.75,1.0}) {
        same=same&&geometry::Distance(a.Evaluate(t),b.Evaluate(t))<=eps;
        reverse=reverse&&geometry::Distance(a.Evaluate(t),b.Evaluate(1.0-t))<=eps;
    }
    return same||reverse;
}

SurfaceNetworkPlan PlanarHoles(const std::vector<modeling::SnapCurve>& curves,
    const geometry::GeometryTolerance& tolerance)
{
    modeling::SnapScene scene;scene.curves=curves;
    const auto regions=DetectProfileRegions(scene,tolerance);
    if(std::none_of(regions.begin(),regions.end(),[](const auto& region){return !region.holes.empty();}))return {};
    std::vector<geometry::Vector3> samples;
    for(const auto& curve:curves)for(double t:{0.0,0.25,0.5,0.75,1.0})samples.push_back(curve.segment.Evaluate(t));
    const auto plane=geometry::FitPlane(samples);
    if(!plane.valid||plane.maximumDeviationMm>tolerance.modelLinearMm)return {};
    SurfaceNetworkPlan result;
    for(const auto& region:regions){
        modeling::GuideTable table;table.method=modeling::GuideSurfaceMethod::PlanarBoundary;
        const auto append=[&](const ProfileBoundary& boundary,modeling::ChainRole role){
            modeling::GuideTableRow row;row.role=role;row.segments=boundary.segments;
            row.sourceWireIds=boundary.entityIds;row.sourceLabels.assign(row.sourceWireIds.size(),"境界");
            table.rows.push_back(std::move(row));
        };
        append(region.outer,modeling::ChainRole::OuterBoundary);
        for(const auto& hole:region.holes)append(hole,modeling::ChainRole::HoleBoundary);
        const auto request=modeling::ToGuideSurfaceRequest(table,tolerance);
        if(!request.HasValue()||!modeling::AnalyzeGuideSurfaceRequest(request.Value(),tolerance).HasValue())return {};
        result.tables.push_back(std::move(table));
    }
    for(const auto& curve:curves){
        const bool covered=std::any_of(result.tables.begin(),result.tables.end(),[&](const auto& table){
            return std::any_of(table.rows.begin(),table.rows.end(),[&](const auto& row){
                if(std::find(row.sourceWireIds.begin(),row.sourceWireIds.end(),curve.entityId)==row.sourceWireIds.end())return false;
                for(double t:{0.0,0.25,0.5,0.75,1.0})
                    if(std::none_of(row.segments.begin(),row.segments.end(),[&](const auto& segment){
                        return segment.ClosestPoint(curve.segment.Evaluate(t)).distance<=tolerance.modelLinearMm;
                    }))return false;
                return true;
            });
        });
        if(!covered)result.unusedSegments.push_back({curve.entityId,"境界",{curve.segment}});
    }
    return result;
}

bool IndependentCycle(std::vector<bool> cycle, std::vector<std::vector<bool>>& basis)
{
    for (std::size_t i = 0; i < cycle.size(); ++i) {
        if (!cycle[i]) continue;
        if (basis[i].empty()) { basis[i] = std::move(cycle); return true; }
        for (std::size_t j = i; j < cycle.size(); ++j) cycle[j] = cycle[j] != basis[i][j];
    }
    return false;
}
}

SurfaceNetworkPlan PlanSurfaceNetwork(const std::vector<RoleWire>& wires,
    const geometry::GeometryTolerance& tolerance)
{
    std::vector<modeling::SnapCurve> curves;
    for (const auto& wire : wires)
        for (const auto& segment : wire.segments) curves.push_back({wire.id, {}, segment});
    // Keep the bounded cycle solver bounded before intersection work as well.
    if (curves.size() > 24) return {};
    std::vector<modeling::GuideTableSelection> duplicates;
    std::vector<modeling::SnapCurve> unique;
    for(const auto& curve:curves){
        const bool duplicate=std::any_of(unique.begin(),unique.end(),[&](const auto& prior){
            return SameSegment(prior.segment,curve.segment,tolerance.modelLinearMm);
        });
        if(duplicate)duplicates.push_back({curve.entityId,"重複",{curve.segment}});
        else unique.push_back(curve);
    }
    const auto pieces = detail::SplitProfileNetwork(unique, tolerance);
    auto holes=PlanarHoles(pieces,tolerance);
    if(!holes.tables.empty()){
        holes.unusedSegments.insert(holes.unusedSegments.end(),duplicates.begin(),duplicates.end());
        return holes;
    }
    std::vector<modeling::GuideTableSelection> selections;
    for (const auto& piece : pieces) {
        const bool duplicate=std::any_of(selections.begin(),selections.end(),[&](const auto& prior){
            return SameSegment(prior.segments.front(),piece.segment,tolerance.modelLinearMm);
        });
        if(duplicate)duplicates.push_back({piece.entityId,"重複",{piece.segment}});
        else selections.push_back({piece.entityId,"境界",{piece.segment}});
    }
    const auto planned = PlanLoopFaces(selections, tolerance);
    if (!planned.HasValue() || planned.Value().faces.empty()) return {};
    // PlanLoopFaces can refine further at T joins; use its piece indices exactly.
    std::vector<modeling::GuideTableSelection> refined;
    for (const auto& piece : planned.Value().pieces)
        refined.push_back({selections[piece.source].sourceWireId, "境界", piece.segments});
    std::vector<modeling::GuideTable> tables;
    std::vector<std::vector<bool>> basis(refined.size());
    for (const auto& face : planned.Value().faces) {
        if (face.method == LoopFaceMethod::Loft) return {};
        std::vector<bool> cycle(refined.size());
        for (auto index : face.selections) cycle[index] = !cycle[index];
        if (!IndependentCycle(cycle, basis)) continue;
        const auto table = NetworkFaceTable(refined, face);
        const auto request = modeling::ToGuideSurfaceRequest(table, tolerance);
        if (!request.HasValue()) return {};
        const auto checked = modeling::AnalyzeGuideSurfaceRequest(request.Value(), tolerance);
        if (!checked.HasValue()) return {};
        tables.push_back(table);
    }
    SurfaceNetworkPlan result;
    result.tables = std::move(tables);
    result.unusedSegments = std::move(duplicates);
    std::vector<bool> used(refined.size());
    for (const auto& face : planned.Value().faces)
        for (const auto index : face.selections) used[index] = true;
    for (std::size_t i = 0; i < refined.size(); ++i)
        if (!planned.Value().pieces[i].split && !used[i]) result.unusedSegments.push_back(refined[i]);
    return result;
}

std::vector<modeling::GuideTable> SurfaceNetworkTables(const std::vector<RoleWire>& wires,
    const geometry::GeometryTolerance& tolerance)
{
    auto plan = PlanSurfaceNetwork(wires, tolerance);
    // The role classifier has no unused-input UI; retain its all-input contract.
    return plan.unusedSegments.empty() ? std::move(plan.tables) : std::vector<modeling::GuideTable>{};
}
}
