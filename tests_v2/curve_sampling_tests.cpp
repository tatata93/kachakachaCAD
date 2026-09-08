// 曲線を検査用の点列へ落とす道具。ここが甘いと、交差も同一平面も見誤る。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/ArcBuilders.h"
#include "kachakacha/geometry/CurveSampling.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::Centroid;
using kachakacha::v2::geometry::ContainsPoint;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::FitPlane;
using kachakacha::v2::geometry::HasSelfIntersection;
using kachakacha::v2::geometry::LoopsIntersect;
using kachakacha::v2::geometry::MakeFrame;
using kachakacha::v2::geometry::MaximumDeviationTo;
using kachakacha::v2::geometry::MinimumDistanceBetween;
using kachakacha::v2::geometry::NormalizedArcLength;
using kachakacha::v2::geometry::Point2;
using kachakacha::v2::geometry::PointAtNormalizedArcLength;
using kachakacha::v2::geometry::ProjectToFrame;
using kachakacha::v2::geometry::SampleChain;
using kachakacha::v2::geometry::SampleCurve;
using kachakacha::v2::geometry::SignedArea;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

//! 点列が本当に曲線の上に載っているか、細かく測り直す。
[[nodiscard]] double WorstChordError(const CurveSegment& segment,
    const std::vector<kachakacha::v2::geometry::CurvePoint>& points)
{
    double worst = 0.0;
    for (std::size_t index = 1; index < points.size(); ++index) {
        const double t0 = points[index - 1].parameter;
        const double t1 = points[index].parameter;
        for (int step = 1; step < 8; ++step) {
            const double local = static_cast<double>(step) / 8.0;
            const double t = t0 + (t1 - t0) * local;
            const Vector3 onCurve = segment.Evaluate(t);
            const Vector3 onChord = points[index - 1].position
                + (points[index].position - points[index - 1].position) * local;
            worst = std::max(worst, (onCurve - onChord).Length());
        }
    }
    return worst;
}

[[nodiscard]] std::vector<Point2> Rectangle(double width, double height)
{
    return {{0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}};
}

} // namespace

KACHA_V2_TEST(sampling, 直線は2点で足りる)
{
    const CurveSegment line = CurveSegment::MakeLine({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}).Value();
    const auto points = SampleCurve(line, 0.01);
    Require(points.size() >= 2, "端点が入ること");
    RequireNear(points.front().position.x, 0.0, 1e-12, "始点");
    RequireNear(points.back().position.x, 10.0, 1e-12, "終点");
    RequireNear(WorstChordError(line, points), 0.0, 1e-12, "直線に誤差は出ない");
}

KACHA_V2_TEST(sampling, 円弧は許容差の中に収まる)
{
    for (const double radius : {0.5, 5.0, 50.0, 500.0}) {
        for (const double sweep : {0.2, 1.0, 3.0, 6.0}) {
            const CurveSegment arc = CurveSegment::MakeCircularArc({0.0, 0.0, 0.0},
                {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, radius, 0.0, sweep)
                                         .Value();
            const double tolerance = 0.01;
            const auto points = SampleCurve(arc, tolerance);
            const double worst = WorstChordError(arc, points);
            Require(worst <= tolerance * 1.2,
                "半径 " + std::to_string(radius) + " 掃引 " + std::to_string(sweep)
                    + " の誤差 " + std::to_string(worst));
        }
    }
}

KACHA_V2_TEST(sampling, 許容差を細かくすると点が増える)
{
    const CurveSegment arc = CurveSegment::MakeCircularArc({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0},
        {1.0, 0.0, 0.0}, 20.0, 0.0, 3.0)
                                 .Value();
    const std::size_t coarse = SampleCurve(arc, 0.5).size();
    const std::size_t fine = SampleCurve(arc, 0.005).size();
    Require(fine > coarse, "細かい許容差のほうが点が多いこと");
}

KACHA_V2_TEST(sampling, 円は一周ぶん点が並ぶ)
{
    const CurveSegment circle = CurveSegment::MakeCircle({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0},
        {1.0, 0.0, 0.0}, 10.0)
                                    .Value();
    const auto points = SampleCurve(circle, 0.05);
    Require(points.size() > 20, "点が十分あること");
    // 全部が半径10の上にあること。
    for (const auto& point : points) {
        const double radius = std::sqrt(point.position.x * point.position.x
            + point.position.y * point.position.y);
        RequireNear(radius, 10.0, 1e-9, "半径");
    }
    Require(WorstChordError(circle, points) <= 0.06, "誤差が許容差の中");
}

KACHA_V2_TEST(sampling, B_splineも許容差に収まる)
{
    const CurveSegment spline = CurveSegment::MakeCubicBSpline(
        {{0.0, 0.0, 0.0}, {2.0, 6.0, 1.0}, {8.0, -4.0, -2.0}, {12.0, 5.0, 3.0},
            {18.0, 0.0, 0.0}})
                                    .Value();
    const auto points = SampleCurve(spline, 0.02);
    Require(WorstChordError(spline, points) <= 0.03, "誤差");
}

KACHA_V2_TEST(sampling, 鎖にすると継ぎ目が重複しない)
{
    const std::vector<CurveSegment> chain{
        CurveSegment::MakeLine({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}).Value(),
        CurveSegment::MakeLine({10.0, 0.0, 0.0}, {10.0, 10.0, 0.0}).Value(),
    };
    const auto points = SampleChain(chain, 0.01);
    for (std::size_t index = 1; index < points.size(); ++index) {
        Require((points[index] - points[index - 1]).Length() > 1e-9,
            "同じ点が2つ並ばないこと");
    }
    RequireNear(points.back().y, 10.0, 1e-12, "終点");
}

KACHA_V2_TEST(sampling, 弧長の正規化が単調に0から1へ進む)
{
    const std::vector<CurveSegment> chain{
        CurveSegment::MakeLine({0.0, 0.0, 0.0}, {3.0, 0.0, 0.0}).Value(),
        CurveSegment::MakeLine({3.0, 0.0, 0.0}, {3.0, 4.0, 0.0}).Value(),
    };
    const auto points = SampleChain(chain, 0.01);
    const auto parameters = NormalizedArcLength(points);
    RequireNear(parameters.front(), 0.0, 1e-12, "始まり");
    RequireNear(parameters.back(), 1.0, 1e-12, "終わり");
    for (std::size_t index = 1; index < parameters.size(); ++index) {
        Require(parameters[index] >= parameters[index - 1], "単調であること");
    }
    // 全長7の折れ線。3/7 の位置は角。
    const Vector3 corner = PointAtNormalizedArcLength(points, parameters, 3.0 / 7.0);
    RequireNear(corner.x, 3.0, 1e-9, "角のx");
    RequireNear(corner.y, 0.0, 1e-9, "角のy");
    const Vector3 middle = PointAtNormalizedArcLength(points, parameters, 5.0 / 7.0);
    RequireNear(middle.y, 2.0, 1e-9, "後半の中点");
}

KACHA_V2_TEST(sampling, 平面あてはめが正しい法線を返す)
{
    // z = 0 の面。
    const std::vector<Vector3> flat{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
    const auto fit = FitPlane(flat);
    Require(fit.valid, "決まること");
    RequireNear(std::abs(fit.normal.z), 1.0, 1e-9, "法線がz");
    RequireNear(fit.maximumDeviationMm, 0.0, 1e-12, "ずれ0");

    // 45度に傾いた面。
    const std::vector<Vector3> tilted{{0, 0, 0}, {10, 0, 10}, {10, 10, 10}, {0, 10, 0}};
    const auto tiltedFit = FitPlane(tilted);
    Require(tiltedFit.valid, "決まること");
    RequireNear(tiltedFit.maximumDeviationMm, 0.0, 1e-9, "ずれ0");
    RequireNear(std::abs(Dot(tiltedFit.normal, Vector3{1, 0, -1})) / std::sqrt(2.0), 1.0,
        1e-9, "法線の向き");
}

KACHA_V2_TEST(sampling, 平面に載らない点の量を測れる)
{
    std::vector<Vector3> points{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
    points.push_back({5.0, 5.0, 0.3});
    const auto fit = FitPlane(points);
    Require(fit.valid, "決まること");
    Require(fit.maximumDeviationMm > 0.1, "ずれを見逃さないこと");
}

KACHA_V2_TEST(sampling, 一直線の点では平面を決めない)
{
    const std::vector<Vector3> line{{0, 0, 0}, {1, 1, 1}, {2, 2, 2}, {5, 5, 5}};
    Require(!FitPlane(line).valid, "平面を決めないこと");
    Require(!FitPlane({{0, 0, 0}, {1, 0, 0}}).valid, "点が2つでは決めないこと");
    Require(!FitPlane({}).valid, "点が無ければ決めないこと");
    Require(!FitPlane({{1, 1, 1}, {1, 1, 1}, {1, 1, 1}}).valid, "同じ点ばかりでは決めないこと");
}

KACHA_V2_TEST(sampling, 局所座標へ落として面積が出せる)
{
    const std::vector<Vector3> square{{0, 0, 5}, {10, 0, 5}, {10, 10, 5}, {0, 10, 5}};
    const auto fit = FitPlane(square);
    const auto frame = MakeFrame(fit);
    const auto flat = ProjectToFrame(square, frame);
    RequireNear(std::abs(SignedArea(flat)), 100.0, 1e-9, "面積");

    // 傾いた面でも同じ面積になること(投影で潰れていないこと)。
    const std::vector<Vector3> tilted{{0, 0, 0}, {10, 0, 10}, {10, 10, 10}, {0, 10, 0}};
    const auto tiltedFlat = ProjectToFrame(tilted, MakeFrame(FitPlane(tilted)));
    RequireNear(std::abs(SignedArea(tiltedFlat)), 10.0 * std::sqrt(200.0), 1e-9,
        "傾いた四角の面積");
}

KACHA_V2_TEST(sampling, 内外判定が効く)
{
    const auto square = Rectangle(10.0, 10.0);
    Require(ContainsPoint(square, {5.0, 5.0}), "中");
    Require(!ContainsPoint(square, {15.0, 5.0}), "右外");
    Require(!ContainsPoint(square, {-1.0, 5.0}), "左外");
    Require(!ContainsPoint(square, {5.0, 20.0}), "上外");
    Require(!ContainsPoint(square, {5.0, -0.5}), "下外");
    // へこんだ形でも効くこと。
    const std::vector<Point2> concave{{0, 0}, {10, 0}, {10, 10}, {5, 5}, {0, 10}};
    Require(ContainsPoint(concave, {2.0, 2.0}), "へこみの外側の中");
    Require(!ContainsPoint(concave, {5.0, 8.0}), "へこんだ部分は外");
}

KACHA_V2_TEST(sampling, 自己交差を見つける)
{
    Require(!HasSelfIntersection(Rectangle(10.0, 10.0), 1e-6), "四角は交わらない");
    // 8の字。
    const std::vector<Point2> figureEight{{0, 0}, {10, 10}, {10, 0}, {0, 10}};
    Require(HasSelfIntersection(figureEight, 1e-6), "8の字は交わる");
    // 三角形。
    Require(!HasSelfIntersection({{0, 0}, {10, 0}, {5, 8}}, 1e-6), "三角は交わらない");
}

KACHA_V2_TEST(sampling, 輪郭どうしの交差を見つける)
{
    const auto outer = Rectangle(10.0, 10.0);
    const std::vector<Point2> inside{{2, 2}, {4, 2}, {4, 4}, {2, 4}};
    const std::vector<Point2> overlapping{{5, 5}, {15, 5}, {15, 15}, {5, 15}};
    const std::vector<Point2> apart{{20, 20}, {30, 20}, {30, 30}, {20, 30}};
    Require(!LoopsIntersect(outer, inside, 1e-6), "内側の穴は辺が交わらない");
    Require(LoopsIntersect(outer, overlapping, 1e-6), "重なる四角は交わる");
    Require(!LoopsIntersect(outer, apart, 1e-6), "離れた四角は交わらない");
}

KACHA_V2_TEST(sampling, 点列どうしの距離を測れる)
{
    const std::vector<Vector3> first{{0, 0, 0}, {10, 0, 0}};
    const std::vector<Vector3> second{{0, 5, 0}, {10, 5, 0}};
    RequireNear(MinimumDistanceBetween(first, second), 5.0, 1e-9, "最短距離");
    // 交わる場合は0。
    const std::vector<Vector3> crossing{{5, -5, 0}, {5, 5, 0}};
    Require(MinimumDistanceBetween(first, crossing) < 1e-9, "交わるなら0");
}

KACHA_V2_TEST(sampling, 面から曲線までの最大のずれを測れる)
{
    const std::vector<Vector3> reference{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}};
    const std::vector<Vector3> probe{{0, 0, 0}, {5, 0, 0.5}, {10, 5, 0}};
    const double deviation = MaximumDeviationTo(probe, reference);
    RequireNear(deviation, 0.5, 1e-9, "いちばん離れている点のずれ");
}

KACHA_V2_TEST(sampling, 重心が正しい)
{
    RequireNear(Centroid({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}).x, 5.0, 1e-12, "x");
    RequireNear(Centroid({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}).y, 5.0, 1e-12, "y");
    RequireNear(Centroid({}).x, 0.0, 1e-12, "空でも落ちない");
}

KACHA_V2_TEST_MAIN("curve_sampling_tests")
