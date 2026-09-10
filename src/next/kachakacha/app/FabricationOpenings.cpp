#include "kachakacha/app/FabricationOpenings.h"

#include "kachakacha/fabrication/BandFold.h"
#include "kachakacha/geometry/CurveSampling.h"

#include <cstddef>
#include <vector>

namespace kachakacha::v2::app {

using fabrication::BandMesh;
using geometry::Point2;
using geometry::Vector3;

namespace {

//! メッシュと同じ位相で「分割軸のパラメータ」を持つ状態。
//! 点をここへ写すと、x にその点の分割軸パラメータが出る。
[[nodiscard]] std::vector<std::vector<Vector3>> ParameterState(const BandMesh& mesh,
    const std::vector<double>& railParameters)
{
    std::vector<std::vector<Vector3>> state(static_cast<std::size_t>(mesh.rows));
    for (int row = 0; row < mesh.rows; ++row) {
        const double t = static_cast<std::size_t>(row) < railParameters.size()
            ? railParameters[static_cast<std::size_t>(row)]
            : static_cast<double>(row) / std::max(1, mesh.rows - 1);
        auto& rail = state[static_cast<std::size_t>(row)];
        rail.reserve(static_cast<std::size_t>(mesh.columns));
        for (int column = 0; column < mesh.columns; ++column) {
            const double s = static_cast<double>(column) / std::max(1, mesh.columns - 1);
            rail.push_back(Vector3{t, s, 0.0});
        }
    }
    return state;
}

//! 展開座標を 3D の状態として読む(z = 0)。写した点の x, y が型紙の座標。
[[nodiscard]] std::vector<std::vector<Vector3>> DevelopedState(const BandMesh& mesh)
{
    std::vector<std::vector<Vector3>> state;
    state.reserve(mesh.developed.size());
    for (const auto& rail : mesh.developed) {
        std::vector<Vector3> lifted;
        lifted.reserve(rail.size());
        for (const Point2& point : rail) {
            lifted.push_back(Vector3{point.u, point.v, 0.0});
        }
        state.push_back(std::move(lifted));
    }
    return state;
}

} // namespace

bool ClipOpeningIntoBandPanels(const BandOpeningTarget& target,
    const std::vector<geometry::CurveSegment>& opening, double toleranceMm,
    std::vector<fabrication::PatternPanel>& panels)
{
    if (target.mesh == nullptr || target.mesh->rows < 2 || opening.empty()) {
        return false;
    }
    const BandMesh& mesh = *target.mesh;
    std::vector<Vector3> points = geometry::SampleChain(opening, toleranceMm);
    geometry::RemoveClosingDuplicate(points, toleranceMm);
    if (points.size() < 3) {
        return false;
    }
    // 輪郭の点ごとに、分割軸のパラメータを測る。1点でも載っていなければ、この面の窓ではない。
    const auto parameterState = ParameterState(mesh, target.railParameters);
    std::vector<double> parameters;
    parameters.reserve(points.size());
    for (const Vector3& point : points) {
        const auto mapped = fabrication::MapPointToBandState(mesh, parameterState, point);
        if (!mapped.HasValue() || mapped.Value().distanceMm > target.snapToleranceMm) {
            return false;
        }
        parameters.push_back(mapped.Value().point.x);
    }
    // 帯の範囲ごとに多角形として切り出す(またぐ窓は、またぐ全ての帯へ取り分を開ける)。
    const auto pieces = fabrication::ClipLoopIntoBands(points, parameters,
        target.railParameters);
    if (pieces.empty()) {
        return false;
    }
    const auto developed = DevelopedState(mesh);
    for (const auto& piece : pieces) {
        const std::size_t panelIndex = target.firstPanelIndex
            + static_cast<std::size_t>(piece.band);
        if (piece.band < 0 || panelIndex >= panels.size() || piece.points.size() < 3) {
            continue;
        }
        std::vector<Point2> outline;
        outline.reserve(piece.points.size());
        for (const Vector3& point : piece.points) {
            const auto mapped = fabrication::MapPointToBandState(mesh, developed, point);
            if (!mapped.HasValue()) {
                outline.clear();
                break;
            }
            outline.push_back(Point2{mapped.Value().point.x, mapped.Value().point.y});
        }
        if (outline.size() >= 3) {
            panels[panelIndex].openings.push_back(std::move(outline));
        }
    }
    return true;
}

} // namespace kachakacha::v2::app
