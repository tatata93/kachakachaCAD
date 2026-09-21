// 曲線網(Gordon)の格子(modeling/GordonGrid.h)。
//
// U 線と V 線から S = L_U + L_V - T で組み立てた点は、U 線も V 線も全部通る。
// 線の向きや渡す順がばらばらでも、網の並びと向きを決め直して同じ形になる。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/GordonGrid.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/SurfaceCardinality.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::BuildGordonGrid;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideChain;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::modeling::GuideSurfaceRequest;
using kachakacha::v2::modeling::NaturalSplineAt;
using kachakacha::v2::test::Require;

namespace {

[[nodiscard]] double Paraboloid(double x, double y)
{
    return x * x / 100.0 + y * y / 80.0;
}

//! 放物面の上の折れ線。from → to を segments 本で。
[[nodiscard]] GuideChain OnSurface(ChainRole role, int index, Vector3 from, Vector3 to,
    int segments = 40)
{
    GuideChain chain;
    chain.role = role;
    chain.index = index;
    Vector3 previous{from.x, from.y, Paraboloid(from.x, from.y)};
    for (int k = 1; k <= segments; ++k) {
        const double t = static_cast<double>(k) / segments;
        const double x = from.x + (to.x - from.x) * t;
        const double y = from.y + (to.y - from.y) * t;
        const Vector3 next{x, y, Paraboloid(x, y)};
        chain.segments.push_back(CurveSegment::MakeLine(previous, next).Value());
        previous = next;
    }
    return chain;
}

[[nodiscard]] GuideSurfaceRequest Network(bool shuffled)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::CurveNetworkExact;
    std::vector<double> ys{-20.0, 0.0, 20.0};
    std::vector<double> xs{-30.0, -10.0, 10.0, 30.0};
    if (shuffled) {
        std::reverse(ys.begin(), ys.end());
        std::swap(xs[0], xs[2]);
    }
    int index = 1;
    for (const double y : ys) {
        // shuffled のときは 1 本おきに向きを逆にする。
        const bool flip = shuffled && index % 2 == 0;
        request.chains.push_back(flip
                ? OnSurface(ChainRole::GuideU, index, {30.0, y, 0}, {-30.0, y, 0})
                : OnSurface(ChainRole::GuideU, index, {-30.0, y, 0}, {30.0, y, 0}));
        ++index;
    }
    index = 1;
    for (const double x : xs) {
        const bool flip = shuffled && index % 2 == 1;
        request.chains.push_back(flip
                ? OnSurface(ChainRole::GuideV, index, {x, 20.0, 0}, {x, -20.0, 0})
                : OnSurface(ChainRole::GuideV, index, {x, -20.0, 0}, {x, 20.0, 0}));
        ++index;
    }
    return request;
}

[[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

} // namespace

KACHA_V2_TEST(gordon_grid, 自然スプラインは節を通り2点なら直線)
{
    const std::vector<double> x{0.0, 0.3, 1.0};
    const std::vector<Vector3> y{{0, 0, 0}, {1, 2, 0}, {3, 1, 0}};
    for (std::size_t k = 0; k < x.size(); ++k) {
        const Vector3 at = NaturalSplineAt(x, y, x[k]);
        Require((at - y[k]).Length() < 1.0e-12, "節を通る");
    }
    const Vector3 half = NaturalSplineAt({0.0, 1.0}, {{0, 0, 0}, {2, 4, 6}}, 0.5);
    Require((half - Vector3{1, 2, 3}).Length() < 1.0e-12, "2 点なら直線");
}

KACHA_V2_TEST(gordon_grid, 格子はU線もV線も通り放物面に近い)
{
    const auto grid = BuildGordonGrid(Network(false), Tolerance(), 21);
    Require(grid.HasValue(), "組み立てられる");
    const auto& g = grid.Value();
    Require(g.uCurveRows.size() == 3 && g.vCurveColumns.size() == 4, "U 線 3 本・V 線 4 本の位置");
    double onCurves = 0.0;
    for (const std::size_t row : g.uCurveRows) {
        for (std::size_t column = 0; column < g.columns; ++column) {
            const Vector3 p = g.At(row, column);
            onCurves = std::max(onCurves, std::abs(p.z - Paraboloid(p.x, p.y)));
        }
    }
    for (const std::size_t column : g.vCurveColumns) {
        for (std::size_t row = 0; row < g.rows; ++row) {
            const Vector3 p = g.At(row, column);
            onCurves = std::max(onCurves, std::abs(p.z - Paraboloid(p.x, p.y)));
        }
    }
    Require(onCurves < 0.02, "U 線・V 線の上の点は線の上(放物面の上)にある: "
            + std::to_string(onCurves));
    double inside = 0.0;
    for (const Vector3& p : g.points) {
        inside = std::max(inside, std::abs(p.z - Paraboloid(p.x, p.y)));
    }
    Require(inside < 0.5, "網の内側も放物面に近い: " + std::to_string(inside));
}

KACHA_V2_TEST(gordon_grid, 線の向きや順がばらばらでも同じ形になる)
{
    const auto a = BuildGordonGrid(Network(false), Tolerance(), 21);
    const auto b = BuildGordonGrid(Network(true), Tolerance(), 21);
    Require(a.HasValue() && b.HasValue(), "どちらも組み立てられる");
    // 同じ網なので、点の集まりの広がりと高さの最大が一致する。
    const auto extent = [](const std::vector<Vector3>& points) {
        double zMax = -1e9;
        double xMin = 1e9;
        double xMax = -1e9;
        for (const Vector3& p : points) {
            zMax = std::max(zMax, p.z);
            xMin = std::min(xMin, p.x);
            xMax = std::max(xMax, p.x);
        }
        return Vector3{xMin, xMax, zMax};
    };
    const Vector3 ea = extent(a.Value().points);
    const Vector3 eb = extent(b.Value().points);
    Require((ea - eb).Length() < 1.0e-6, "同じ広がり");
    double worst = 0.0;
    for (const Vector3& p : b.Value().points) {
        worst = std::max(worst, std::abs(p.z - Paraboloid(p.x, p.y)));
    }
    Require(worst < 0.5, "ばらばらに渡しても放物面に近い: " + std::to_string(worst));
}

KACHA_V2_TEST(gordon_grid, U5本V4本の網を受け入れて格子を作る)
{
    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::CurveNetworkExact;
    int index = 1;
    for (const double y : {-20.0, -10.0, 0.0, 10.0, 20.0}) {
        request.chains.push_back(OnSurface(ChainRole::GuideU, index++, {-30.0, y, 0}, {30.0, y, 0}));
    }
    index = 1;
    for (const double x : {-30.0, -10.0, 10.0, 30.0}) {
        request.chains.push_back(OnSurface(ChainRole::GuideV, index++, {x, -20.0, 0}, {x, 20.0, 0}));
    }
    const auto analysis = kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(analysis.HasValue(), "U5 × V4 を受け入れる");
    Require(analysis.Value().crossings.size() == 20, "交わりは 20");
    const auto grid = BuildGordonGrid(request, Tolerance(), 25);
    Require(grid.HasValue() && grid.Value().uCurveRows.size() == 5
            && grid.Value().vCurveColumns.size() == 4,
        "格子に U 線 5 本・V 線 4 本の位置がある");
}

KACHA_V2_TEST(gordon_grid, 外側の線が端で交わらない網はGordonで断り近似では受ける)
{
    GuideSurfaceRequest request = Network(false);
    // 外側の V 線(x = 30)を短くして、U 線の端(x = 30)ではなく途中の x = 25 に置く。
    request.chains[3 + 3] = OnSurface(ChainRole::GuideV, 4, {25.0, -20.0, 0}, {25.0, 20.0, 0});
    const auto exact = kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(!exact.HasValue(), "Gordon は断る");
    Require(exact.Diagnostics().front().summaryJa.find("端で交わっている") != std::string::npos,
        "なぜ作れないかを言う: " + exact.Diagnostics().front().summaryJa);
    request.method = GuideSurfaceMethod::GordonNetwork;
    Require(kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest(request, Tolerance()).HasValue(),
        "近似(Filling)なら受ける");
}

KACHA_V2_TEST(gordon_grid, 成り立つ方の曲線網だけを薦め成り立たない方は理由を言う)
{
    using kachakacha::v2::modeling::NetworkAlternativeNoteJa;
    // 両方成り立つ網: 近似を選んでいると Gordon でも作れると言う(逆も)。
    GuideSurfaceRequest both = Network(false);
    both.method = GuideSurfaceMethod::GordonNetwork;
    const std::string fromApproximate = NetworkAlternativeNoteJa(both, Tolerance());
    Require(fromApproximate.find("曲線網(Gordon)でも作れます") != std::string::npos,
        "近似から Gordon を薦める: " + fromApproximate);
    both.method = GuideSurfaceMethod::CurveNetworkExact;
    const std::string fromExact = NetworkAlternativeNoteJa(both, Tolerance());
    Require(fromExact.find("近似 / Filling)でも作れます") != std::string::npos,
        "Gordon から近似もあると言う: " + fromExact);

    // Gordon だけ成り立たない網: Gordon を選んでいたら、理由と近似を言う。
    GuideSurfaceRequest onlyApproximate = Network(false);
    onlyApproximate.chains[3 + 3] =
        OnSurface(ChainRole::GuideV, 4, {25.0, -20.0, 0}, {25.0, 20.0, 0});
    const std::string refused = NetworkAlternativeNoteJa(onlyApproximate, Tolerance());
    Require(refused.find("曲線網(Gordon)では作れません") != std::string::npos
            && refused.find("端で交わっている") != std::string::npos
            && refused.find("近似 / Filling)なら作れます") != std::string::npos,
        "Gordon では作れない理由と、作れる方: " + refused);
    // 近似を選んでいるなら、成り立たない Gordon を薦めない。
    onlyApproximate.method = GuideSurfaceMethod::GordonNetwork;
    Require(NetworkAlternativeNoteJa(onlyApproximate, Tolerance()).empty(),
        "成り立たない Gordon は薦めない");

    // 曲線網でない作り方には何も言わない。
    GuideSurfaceRequest loft = Network(false);
    loft.method = GuideSurfaceMethod::LoftSections;
    Require(NetworkAlternativeNoteJa(loft, Tolerance()).empty(), "曲線網以外は空");
}

KACHA_V2_TEST_MAIN("gordon_grid_tests")
