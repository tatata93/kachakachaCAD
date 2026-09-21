#include "kachakacha/modeling/GordonGrid.h"

#include "kachakacha/geometry/CurveSampling.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace kachakacha::v2::modeling {

using base::MakeError;
using base::Result;

namespace {

//! 1 本の線: 点列と正規化弧長、向き、交点の位置(網の並びで)。
struct NetworkCurve {
    std::vector<Vector3> points;
    std::vector<double> parameters;
    bool reversed = false;
    std::vector<double> crossings;   //!< 向きをそろえた t。相手の並び順
};

[[nodiscard]] NetworkCurve Sample(const GuideChain& chain, double toleranceMm)
{
    NetworkCurve curve;
    curve.points = geometry::SampleChain(chain.segments, toleranceMm);
    curve.parameters = geometry::NormalizedArcLength(curve.points);
    return curve;
}

//! 向きをそろえた t での点。
[[nodiscard]] Vector3 PointAt(const NetworkCurve& curve, double t)
{
    const double s = curve.reversed ? 1.0 - t : t;
    return geometry::PointAtNormalizedArcLength(curve.points, curve.parameters,
        std::clamp(s, 0.0, 1.0));
}

//! 付け替えた t(網の共通の u)から、その線の t へ。交点の位置で区切った折れ線。
[[nodiscard]] double LocalParameter(const NetworkCurve& curve, const std::vector<double>& nodes,
    double common)
{
    for (std::size_t k = 1; k < nodes.size(); ++k) {
        if (common <= nodes[k] || k + 1 == nodes.size()) {
            const double span = nodes[k] - nodes[k - 1];
            const double ratio = span > 1.0e-12 ? (common - nodes[k - 1]) / span : 0.0;
            return curve.crossings[k - 1] + ratio * (curve.crossings[k] - curve.crossings[k - 1]);
        }
    }
    return common;
}

//! a 本の線に対して、b 本の相手と交わる t(弧長)を求め、相手の並びと向きを決める。
void FindCrossings(std::vector<NetworkCurve>& along, const std::vector<NetworkCurve>& across,
    std::vector<std::size_t>& acrossOrder)
{
    // 相手の並び: 1 本目の線に沿った交点の順。
    std::vector<std::pair<double, std::size_t>> keyed;
    for (std::size_t j = 0; j < across.size(); ++j) {
        const auto approach = geometry::ClosestApproachBetween(along.front().points,
            across[j].points);
        keyed.emplace_back(approach.firstParameter, j);
    }
    std::sort(keyed.begin(), keyed.end());
    acrossOrder.clear();
    for (const auto& item : keyed) {
        acrossOrder.push_back(item.second);
    }
    for (NetworkCurve& curve : along) {
        curve.crossings.clear();
        for (const std::size_t j : acrossOrder) {
            curve.crossings.push_back(
                geometry::ClosestApproachBetween(curve.points, across[j].points).firstParameter);
        }
        // 交点の順に t が増えるよう向きをそろえる。
        curve.reversed = curve.crossings.front() > curve.crossings.back();
        if (curve.reversed) {
            for (double& t : curve.crossings) {
                t = 1.0 - t;
            }
        }
        // 端の交点は線の端(検査が確かめてある)。0 と 1 にそろえる。
        curve.crossings.front() = 0.0;
        curve.crossings.back() = 1.0;
    }
}

//! 網の共通の u(または v): 線ごとの交点の t の平均。両端は 0 と 1。
[[nodiscard]] std::vector<double> CommonNodes(const std::vector<NetworkCurve>& curves)
{
    std::vector<double> nodes(curves.front().crossings.size(), 0.0);
    for (const NetworkCurve& curve : curves) {
        for (std::size_t k = 0; k < nodes.size(); ++k) {
            nodes[k] += curve.crossings[k] / static_cast<double>(curves.size());
        }
    }
    nodes.front() = 0.0;
    nodes.back() = 1.0;
    return nodes;
}

//! 等間隔の点に節の値を混ぜた並び。節が入った位置も返す。
[[nodiscard]] std::vector<double> WithNodes(std::size_t samples, const std::vector<double>& nodes,
    std::vector<std::size_t>& nodePositions)
{
    std::vector<double> values;
    for (std::size_t k = 0; k < samples; ++k) {
        values.push_back(static_cast<double>(k) / static_cast<double>(samples - 1));
    }
    values.insert(values.end(), nodes.begin(), nodes.end());
    std::sort(values.begin(), values.end());
    std::vector<double> unique;
    for (const double value : values) {
        if (unique.empty() || value - unique.back() > 1.0e-6) {
            unique.push_back(value);
        } else if (std::find(nodes.begin(), nodes.end(), value) != nodes.end()) {
            unique.back() = value;   // 近い点は節の値に寄せる
        }
    }
    nodePositions.clear();
    for (const double node : nodes) {
        std::size_t best = 0;
        for (std::size_t k = 1; k < unique.size(); ++k) {
            if (std::abs(unique[k] - node) < std::abs(unique[best] - node)) {
                best = k;
            }
        }
        unique[best] = node;
        nodePositions.push_back(best);
    }
    return unique;
}

} // namespace

Vector3 NaturalSplineAt(const std::vector<double>& x, const std::vector<Vector3>& values,
    double at)
{
    const std::size_t n = x.size();
    if (n == 0) {
        return {};
    }
    if (n == 1) {
        return values.front();
    }
    // 区間を探す。外側は端の区間を延ばす。
    std::size_t k = 1;
    while (k + 1 < n && at > x[k]) {
        ++k;
    }
    const double h = x[k] - x[k - 1];
    if (n == 2 || !(h > 0.0)) {
        const double ratio = h > 0.0 ? (at - x[k - 1]) / h : 0.0;
        return values[k - 1] + (values[k] - values[k - 1]) * ratio;
    }
    // 2 階微分 M を三重対角で解く(自然: 両端 0)。
    std::vector<Vector3> m(n, Vector3{});
    std::vector<double> c(n, 0.0);
    std::vector<Vector3> d(n, Vector3{});
    for (std::size_t i = 1; i + 1 < n; ++i) {
        const double h0 = x[i] - x[i - 1];
        const double h1 = x[i + 1] - x[i];
        const double diag = 2.0 * (h0 + h1);
        const Vector3 rhs = ((values[i + 1] - values[i]) * (1.0 / h1)
                                - (values[i] - values[i - 1]) * (1.0 / h0))
            * 6.0;
        const double denom = diag - h0 * c[i - 1];
        c[i] = h1 / denom;
        d[i] = (rhs - d[i - 1] * h0) * (1.0 / denom);
    }
    for (std::size_t i = n - 2; i >= 1; --i) {
        m[i] = d[i] - m[i + 1] * c[i];
        if (i == 1) {
            break;
        }
    }
    const double a = (x[k] - at) / h;
    const double b = (at - x[k - 1]) / h;
    return values[k - 1] * a + values[k] * b
        + (m[k - 1] * (a * a * a - a) + m[k] * (b * b * b - b)) * (h * h / 6.0);
}

Result<GordonGrid> BuildGordonGrid(const GuideSurfaceRequest& request,
    const GeometryTolerance& tolerance, std::size_t samples)
{
    const double sampling = std::max(tolerance.modelLinearMm * 10.0, 1.0e-4);
    std::vector<NetworkCurve> us;
    std::vector<NetworkCurve> vs;
    for (const GuideChain& chain : request.chains) {
        if (chain.role == ChainRole::GuideU) {
            us.push_back(Sample(chain, sampling));
        } else if (chain.role == ChainRole::GuideV) {
            vs.push_back(Sample(chain, sampling));
        }
    }
    if (us.size() < 2 || vs.size() < 2 || samples < 3) {
        return Result<GordonGrid>::Failure(MakeError("GEO-G009",
            "曲線網には U 線と V 線が 2 本ずつ以上要ります。", {}));
    }
    std::vector<std::size_t> vOrder;
    std::vector<std::size_t> uOrder;
    FindCrossings(us, vs, vOrder);
    FindCrossings(vs, us, uOrder);
    // us は uOrder の順、vs は vOrder の順に並べ替える(網の行と列)。
    std::vector<NetworkCurve> rowsU;
    for (const std::size_t i : uOrder) {
        rowsU.push_back(us[i]);
    }
    std::vector<NetworkCurve> columnsV;
    for (const std::size_t j : vOrder) {
        columnsV.push_back(vs[j]);
    }
    const std::vector<double> uNodes = CommonNodes(rowsU);     // V 線の位置(u)
    const std::vector<double> vNodes = CommonNodes(columnsV);  // U 線の位置(v)
    const auto onU = [&](std::size_t i, double u) {
        return PointAt(rowsU[i], LocalParameter(rowsU[i], uNodes, u));
    };
    const auto onV = [&](std::size_t j, double v) {
        return PointAt(columnsV[j], LocalParameter(columnsV[j], vNodes, v));
    };
    // 交点: U 線の上と V 線の上の点の中点(交わりのずれは許容差の内側)。
    std::vector<std::vector<Vector3>> cross(rowsU.size(), std::vector<Vector3>(columnsV.size()));
    for (std::size_t i = 0; i < rowsU.size(); ++i) {
        for (std::size_t j = 0; j < columnsV.size(); ++j) {
            cross[i][j] = (onU(i, uNodes[j]) + onV(j, vNodes[i])) * 0.5;
        }
    }
    GordonGrid grid;
    grid.uParameters = WithNodes(samples, uNodes, grid.vCurveColumns);
    grid.vParameters = WithNodes(samples, vNodes, grid.uCurveRows);
    grid.columns = grid.uParameters.size();
    grid.rows = grid.vParameters.size();
    grid.points.resize(grid.rows * grid.columns);
    for (std::size_t column = 0; column < grid.columns; ++column) {
        const double u = grid.uParameters[column];
        std::vector<Vector3> uCurves;   // C_i(u)
        std::vector<Vector3> tensor;    // T の u の向きの補間(交点を u でつないだ値)
        for (std::size_t i = 0; i < rowsU.size(); ++i) {
            uCurves.push_back(onU(i, u));
            tensor.push_back(NaturalSplineAt(uNodes, cross[i], u));
        }
        for (std::size_t row = 0; row < grid.rows; ++row) {
            const double v = grid.vParameters[row];
            std::vector<Vector3> vCurves;   // D_j(v)
            for (std::size_t j = 0; j < columnsV.size(); ++j) {
                vCurves.push_back(onV(j, v));
            }
            const Vector3 lu = NaturalSplineAt(vNodes, uCurves, v);
            const Vector3 lv = NaturalSplineAt(uNodes, vCurves, u);
            const Vector3 t = NaturalSplineAt(vNodes, tensor, v);
            grid.points[row * grid.columns + column] = lu + lv - t;
        }
    }
    return Result<GordonGrid>::Success(std::move(grid));
}

} // namespace kachakacha::v2::modeling
