// トリム・延長・分割の幾何(geometry/CurveTrim、INVENTOR_PROCEDURE.md)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/CurveTrim.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::CutPoint;
using kachakacha::v2::geometry::CutPointsOn;
using kachakacha::v2::geometry::ExtendToNearestBoundary;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::NearestCut;
using kachakacha::v2::geometry::RemoveSpan;
using kachakacha::v2::geometry::SampleSpan;
using kachakacha::v2::geometry::SpanLengthMm;
using kachakacha::v2::geometry::SplitAtCut;
using kachakacha::v2::geometry::TrimSpan;
using kachakacha::v2::geometry::TrimSpanAround;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

[[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

//! x = -50..50, y = 0 の水平線。
[[nodiscard]] CurveSegment BaseLine()
{
    return Line({-50, 0, 0}, {50, 0, 0});
}

//! 垂直な境目線。y は十分に長い。
[[nodiscard]] CurveSegment VerticalAt(double x)
{
    return Line({x, -20, 0}, {x, 20, 0});
}

[[nodiscard]] CurveSegment Circle(double radius)
{
    const auto made = CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, radius);
    Require(made.HasValue(), "円が作れること");
    return made.Value();
}

} // namespace

// ---- CutPointsOn / TrimSpanAround / RemoveSpan (直線) ----

KACHA_V2_TEST(curve_trim, 二本の縦線に挟まれた水平線の区切りが正しい位置に立つ)
{
    const CurveSegment target = BaseLine();
    const std::vector<CurveSegment> boundaries{VerticalAt(-10.0), VerticalAt(10.0)};
    const auto cuts = CutPointsOn(target, boundaries, Tolerance());
    Require(cuts.size() == 2, "区切りは2つ");
    RequireNear(cuts[0].parameter, 0.4, 1.0e-9, "1つ目の区切りは t=0.4");
    RequireNear(cuts[1].parameter, 0.6, 1.0e-9, "2つ目の区切りは t=0.6");
}

KACHA_V2_TEST(curve_trim, 真ん中を押すと両側の区切りに挟まれる)
{
    const CurveSegment target = BaseLine();
    const std::vector<CurveSegment> boundaries{VerticalAt(-10.0), VerticalAt(10.0)};
    const auto cuts = CutPointsOn(target, boundaries, Tolerance());
    const auto span = TrimSpanAround(target, cuts, 0.5, Tolerance());
    Require(span.HasValue(), "区間が決まる: " + span.FirstSummaryJa());
    RequireNear(span.Value().low, 0.4, 1.0e-9, "下側は0.4");
    RequireNear(span.Value().high, 0.6, 1.0e-9, "上側は0.6");
    Require(!span.Value().whole, "線ごとではない");
    Require(!span.Value().closed, "円ではない");

    const auto removed = RemoveSpan(target, span.Value());
    Require(removed.HasValue(), "消せる: " + removed.FirstSummaryJa());
    Require(removed.Value().before.size() == 1, "始点側が1本残る");
    Require(removed.Value().after.size() == 1, "終点側が1本残る");
    RequireNear(removed.Value().before.front().StartPoint().x, -50.0, 1.0e-6, "始点側の始点");
    RequireNear(removed.Value().before.front().EndPoint().x, -10.0, 1.0e-6, "始点側の終点");
    RequireNear(removed.Value().after.front().StartPoint().x, 10.0, 1.0e-6, "終点側の始点");
    RequireNear(removed.Value().after.front().EndPoint().x, 50.0, 1.0e-6, "終点側の終点");

    RequireNear(SpanLengthMm(target, span.Value(), Tolerance()), 20.0, 1.0e-6,
        "消える長さは20mm");
}

KACHA_V2_TEST(curve_trim, 片側だけ区切りがあれば端まで消える)
{
    const CurveSegment target = BaseLine();
    const std::vector<CurveSegment> boundaries{VerticalAt(-10.0), VerticalAt(10.0)};
    const auto cuts = CutPointsOn(target, boundaries, Tolerance());
    const auto span = TrimSpanAround(target, cuts, 0.1, Tolerance());
    Require(span.HasValue(), "区間が決まる: " + span.FirstSummaryJa());
    RequireNear(span.Value().low, 0.0, 1.0e-9, "下側は端(0)");
    RequireNear(span.Value().high, 0.4, 1.0e-9, "上側は0.4");

    const auto removed = RemoveSpan(target, span.Value());
    Require(removed.HasValue(), "消せる: " + removed.FirstSummaryJa());
    Require(removed.Value().before.empty(), "始点側は残らない");
    Require(removed.Value().after.size() == 1, "終点側が1本残る");
    RequireNear(removed.Value().after.front().StartPoint().x, -10.0, 1.0e-6, "終点側の始点");
    RequireNear(removed.Value().after.front().EndPoint().x, 50.0, 1.0e-6, "終点側の終点");
}

KACHA_V2_TEST(curve_trim, 区切りが無ければ線ごと消える)
{
    const CurveSegment target = BaseLine();
    const std::vector<CurveSegment> boundaries; // 交わる線が無い
    const auto cuts = CutPointsOn(target, boundaries, Tolerance());
    Require(cuts.empty(), "区切りは無い");
    const auto span = TrimSpanAround(target, cuts, 0.5, Tolerance());
    Require(span.HasValue(), "区間は決まる: " + span.FirstSummaryJa());
    Require(span.Value().whole, "線ごと消える扱い");

    const auto removed = RemoveSpan(target, span.Value());
    Require(removed.HasValue(), "消せる: " + removed.FirstSummaryJa());
    Require(removed.Value().before.empty(), "始点側は無い");
    Require(removed.Value().after.empty(), "終点側も無い");

    const auto points = SampleSpan(target, span.Value(), 48);
    RequireEqual(std::to_string(points.size()), std::to_string(49), "点列は count+1 個");
}

// ---- 円のトリム ----

KACHA_V2_TEST(curve_trim, 円は横切る線二本で上下半分に分けられる)
{
    const CurveSegment target = Circle(10.0);
    const std::vector<CurveSegment> boundaries{Line({-20, 0, 0}, {20, 0, 0})};
    const auto cuts = CutPointsOn(target, boundaries, Tolerance());
    Require(cuts.size() == 2, "円は左右の2か所で交わる");

    // 下(270°, t=0.75)を押すと下半分が消えて上半分の円弧が残る。
    const auto spanBottom = TrimSpanAround(target, cuts, 0.75, Tolerance());
    Require(spanBottom.HasValue(), "区間が決まる: " + spanBottom.FirstSummaryJa());
    Require(spanBottom.Value().closed, "円として扱う");
    const auto removedBottom = RemoveSpan(target, spanBottom.Value());
    Require(removedBottom.HasValue(), "消せる: " + removedBottom.FirstSummaryJa());
    Require(removedBottom.Value().before.empty(), "円は before を使わない");
    Require(removedBottom.Value().after.size() == 1, "残りは円弧1本");
    const CurveSegment& survivorBottom = removedBottom.Value().after.front();
    Require(survivorBottom.Kind() == kachakacha::v2::geometry::CurveKind::CircularArc,
        "残りは円弧");
    RequireNear(std::abs(survivorBottom.SweepAngleRad()), 3.14159265358979323846, 1.0e-9,
        "半周ぶんの円弧");
    const Vector3 midBottom = survivorBottom.Evaluate(0.5);
    RequireNear(midBottom.x, 0.0, 1.0e-6, "上半分の真ん中x");
    RequireNear(midBottom.y, 10.0, 1.0e-6, "上半分の真ん中y");

    // 上(90°, t=0.25)を押すと上半分が消えて下半分の円弧が残る。
    const auto spanTop = TrimSpanAround(target, cuts, 0.25, Tolerance());
    Require(spanTop.HasValue(), "区間が決まる: " + spanTop.FirstSummaryJa());
    const auto removedTop = RemoveSpan(target, spanTop.Value());
    Require(removedTop.HasValue(), "消せる: " + removedTop.FirstSummaryJa());
    Require(removedTop.Value().after.size() == 1, "残りは円弧1本");
    const Vector3 midTop = removedTop.Value().after.front().Evaluate(0.5);
    RequireNear(midTop.x, 0.0, 1.0e-6, "下半分の真ん中x");
    RequireNear(midTop.y, -10.0, 1.0e-6, "下半分の真ん中y");
}

KACHA_V2_TEST(curve_trim, 円に接するだけの線一本では区間が決まらない)
{
    const CurveSegment target = Circle(10.0);
    // y=10 で円の頂点にだけ接する線。
    const std::vector<CurveSegment> boundaries{Line({-20, 10, 0}, {20, 10, 0})};
    const auto cuts = CutPointsOn(target, boundaries, Tolerance());
    Require(cuts.size() <= 1, "接するだけなら区切りは高々1つ");
    const auto span = TrimSpanAround(target, cuts, 0.75, Tolerance());
    Require(!span.HasValue(), "区間が決まらないので断る");
    RequireEqual(span.FirstCode(), std::string(kachakacha::v2::geometry::kTrimOneCutOnCircle),
        "GEO-E023で断る");
}

// ---- 延長 ----

KACHA_V2_TEST(curve_trim, 端をいちばん近い境目まで延ばす)
{
    const CurveSegment target = Line({0, 0, 0}, {10, 0, 0});
    const std::vector<CurveSegment> boundaries{VerticalAt(30.0), VerticalAt(60.0)};

    const auto extended = ExtendToNearestBoundary(target, 1, boundaries, Tolerance());
    Require(extended.HasValue(), "終点側は延ばせる: " + extended.FirstSummaryJa());
    RequireNear(extended.Value().addedMm, 20.0, 1.0e-6, "伸びた長さは20mm");
    RequireNear(extended.Value().extended.EndPoint().x, 30.0, 1.0e-6, "近い方の境目で止まる");

    const auto failed = ExtendToNearestBoundary(target, 0, boundaries, Tolerance());
    Require(!failed.HasValue(), "始点側には境目が無いので断る");
    RequireEqual(failed.FirstCode(), std::string("GEO-E001"), "GEO-E001で断る");
}

KACHA_V2_TEST(curve_trim, 円は延ばせない)
{
    const CurveSegment target = Circle(10.0);
    const std::vector<CurveSegment> boundaries{VerticalAt(30.0)};
    const auto extended = ExtendToNearestBoundary(target, 1, boundaries, Tolerance());
    Require(!extended.HasValue(), "円は延ばせないので断る");
    RequireEqual(extended.FirstCode(), std::string(kachakacha::v2::geometry::kExtendClosed),
        "GEO-E025で断る");
}

// ---- 分割 ----

KACHA_V2_TEST(curve_trim, いちばん近い区切りで分ける)
{
    const CurveSegment target = BaseLine();
    const std::vector<CurveSegment> boundaries{VerticalAt(-10.0), VerticalAt(10.0)};
    const auto cuts = CutPointsOn(target, boundaries, Tolerance());
    Require(cuts.size() == 2, "区切りは2つ");

    const auto nearLow = NearestCut(target, cuts, 0.45);
    Require(nearLow.has_value(), "近い区切りが見つかる");
    RequireNear(nearLow->parameter, 0.4, 1.0e-9, "0.45に近いのは0.4");

    const auto nearHigh = NearestCut(target, cuts, 0.58);
    Require(nearHigh.has_value(), "近い区切りが見つかる");
    RequireNear(nearHigh->parameter, 0.6, 1.0e-9, "0.58に近いのは0.6");

    const auto pieces = SplitAtCut(target, *nearLow);
    Require(pieces.HasValue(), "分けられる: " + pieces.FirstSummaryJa());
    RequireNear(pieces.Value().first.EndPoint().x, -10.0, 1.0e-6, "1本目は区切りで終わる");
    Require(pieces.Value().second.has_value(), "2本目がある");
    RequireNear(pieces.Value().second->StartPoint().x, -10.0, 1.0e-6, "2本目は区切りから始まる");
}

KACHA_V2_TEST(curve_trim, 円を分けると継ぎ目入りの一周円弧になる)
{
    const CurveSegment target = Circle(10.0);
    const CutPoint seam{0.3, 0, target.Evaluate(0.3)};
    const auto pieces = SplitAtCut(target, seam);
    Require(pieces.HasValue(), "分けられる: " + pieces.FirstSummaryJa());
    Require(!pieces.Value().second.has_value(), "円は2本にならない");
    Require(pieces.Value().first.Kind() == kachakacha::v2::geometry::CurveKind::CircularArc,
        "継ぎ目入りの円弧になる");
    RequireNear(std::abs(pieces.Value().first.SweepAngleRad()), 6.283185307179586, 1.0e-9,
        "掃引角はほぼ2π");
}

// ---- Bスプラインは切れない ----

KACHA_V2_TEST(curve_trim, Bスプラインを横切る線があってもトリムでは切れない)
{
    // 左右対称: t=0.5 でちょうど原点を通る。
    const auto made = CurveSegment::MakeCubicBSpline(
        {{-30, -15, 0}, {-10, 15, 0}, {10, -15, 0}, {30, 15, 0}});
    Require(made.HasValue(), "Bスプラインが作れる");
    const CurveSegment target = made.Value();
    RequireNear(target.Evaluate(0.5).x, 0.0, 1.0e-9, "中央はx=0を通る(前提)");
    RequireNear(target.Evaluate(0.5).y, 0.0, 1.0e-9, "中央はy=0を通る(前提)");

    const std::vector<CurveSegment> boundaries{VerticalAt(0.0)};
    const auto cuts = CutPointsOn(target, boundaries, Tolerance());
    Require(!cuts.empty(), "交わる場所はある");

    const auto span = TrimSpanAround(target, cuts, 0.5, Tolerance());
    Require(span.HasValue(), "区間自体は決まる: " + span.FirstSummaryJa());
    Require(!span.Value().whole, "区切りがあるので線ごとではない");

    const auto removed = RemoveSpan(target, span.Value());
    Require(!removed.HasValue(), "Bスプラインは途中で切れないので断る");
    RequireEqual(removed.FirstCode(), std::string("GEO-C014"), "GEO-C014で断る");
}

KACHA_V2_TEST_MAIN("curve_trim_tests")
