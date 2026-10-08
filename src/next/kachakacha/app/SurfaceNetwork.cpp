#include "kachakacha/app/SurfaceNetwork.h"
#include "kachakacha/app/LoopFaces.h"
#include "kachakacha/app/ProfileNetwork.h"
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

std::vector<modeling::GuideTable> SurfaceNetworkTables(const std::vector<RoleWire>& wires,
    const geometry::GeometryTolerance& tolerance)
{
    std::vector<modeling::SnapCurve> curves;
    for (const auto& wire : wires)
        for (const auto& segment : wire.segments) curves.push_back({wire.id, {}, segment});
    // Keep the bounded cycle solver bounded before intersection work as well.
    if (curves.size() > 24) return {};
    const auto pieces = detail::SplitProfileNetwork(curves, tolerance);
    std::vector<modeling::GuideTableSelection> selections;
    for (const auto& piece : pieces)
        selections.push_back({piece.entityId, "境界", {piece.segment}});
    const auto planned = PlanLoopFaces(selections, tolerance);
    if (!planned.HasValue() || planned.Value().faces.empty() || !planned.Value().unused.empty()) return {};
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
    return tables;
}
}
