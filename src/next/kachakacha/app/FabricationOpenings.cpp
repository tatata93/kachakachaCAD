#include "kachakacha/app/FabricationOpenings.h"

#include "kachakacha/fabrication/BandFold.h"
#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>
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

//! 取り分の輪郭のうち、帯の境目(レール)の上を通る辺に、レールの点を挟む。
//!
//! 切り口を2点の直線で済ませると、曲がった面の上では弦になって形が崩れる
//! (fabrication-contract §8.2、V1 の壊れどころ)。境目に沿って面の上を辿らせる。
[[nodiscard]] std::vector<Vector3> FollowRailsOnCuts(const BandMesh& mesh,
    const std::vector<std::vector<Vector3>>& parameterState,
    const std::vector<double>& railParameters, const std::vector<Vector3>& outline)
{
    constexpr double kOnRail = 1.0e-6;
    std::vector<Vector3> refined;
    const std::size_t count = outline.size();
    for (std::size_t index = 0; index < count; ++index) {
        const Vector3& from = outline[index];
        const Vector3& to = outline[(index + 1) % count];
        refined.push_back(from);
        const auto a = fabrication::MapPointToBandState(mesh, parameterState, from);
        const auto b = fabrication::MapPointToBandState(mesh, parameterState, to);
        if (!a.HasValue() || !b.HasValue()) {
            continue;
        }
        for (std::size_t row = 0; row < railParameters.size() && row < mesh.world.size();
             ++row) {
            const double rail = railParameters[row];
            if (std::abs(a.Value().point.x - rail) > kOnRail
                || std::abs(b.Value().point.x - rail) > kOnRail) {
                continue;
            }
            // 両端がこのレールの上。間の列の点を、進む向きに挟む。
            const double sFrom = a.Value().point.y;
            const double sTo = b.Value().point.y;
            const int columns = mesh.columns;
            const int first = static_cast<int>(std::ceil(std::min(sFrom, sTo) * (columns - 1)));
            const int last = static_cast<int>(std::floor(std::max(sFrom, sTo) * (columns - 1)));
            std::vector<Vector3> between;
            for (int column = first; column <= last; ++column) {
                const double s = static_cast<double>(column) / (columns - 1);
                if (s > std::min(sFrom, sTo) + kOnRail && s < std::max(sFrom, sTo) - kOnRail) {
                    between.push_back(mesh.world[row][static_cast<std::size_t>(column)]);
                }
            }
            if (sFrom > sTo) {
                std::reverse(between.begin(), between.end());
            }
            refined.insert(refined.end(), between.begin(), between.end());
            break;
        }
    }
    return refined;
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
        const std::vector<Vector3> followed = FollowRailsOnCuts(mesh, parameterState,
            target.railParameters, piece.points);
        outline.reserve(followed.size());
        for (const Vector3& point : followed) {
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
