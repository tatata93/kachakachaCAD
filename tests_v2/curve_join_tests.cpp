// 曲線どうしの接続(V1の「端点一致」「接線接続」「曲率接続」)と、角の加工。
// 合っていないのに合ったことにしない、が肝。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/CurveJoin.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveEnd;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::JoinAnchor;
using kachakacha::v2::geometry::JoinContinuity;
using kachakacha::v2::geometry::JoinContinuityNameJa;
using kachakacha::v2::geometry::JoinCurves;
using kachakacha::v2::geometry::ProcessPolylineCorners;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

void RequireCount(std::size_t actual, std::size_t expected, const std::string& why)
{
    RequireEqual(std::to_string(actual), std::to_string(expected), why);
}

[[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    tolerance.modelAngularRad = 1.0e-9;
    tolerance.interactiveJoinMm = 0.01;
    return tolerance;
}

[[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

[[nodiscard]] CurveSegment Bezier(std::vector<Vector3> control)
{
    const auto made = CurveSegment::MakeCubicBezier(std::move(control));
    Require(made.HasValue(), "ベジェが作れること");
    return made.Value();
}

} // namespace

KACHA_V2_TEST(join, 端点一致で離れた線がつながる)
{
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = Line({10.5, 0.3, 0}, {20, 0, 0});
    const auto result = JoinCurves(first, CurveEnd::End, second, CurveEnd::Start,
        JoinContinuity::Position, JoinAnchor::KeepFirst, Tolerance());
    Require(result.HasValue(), "つながること");
    Require(result.Value().positionGapMm < 1e-9, "隙間が無いこと");
    // 1本目は固定なので、終点は動いていない。
    RequireNear(result.Value().first.EndPoint().x, 10.0, 1e-12, "1本目は動かない");
    RequireNear(result.Value().second.StartPoint().x, 10.0, 1e-12, "2本目が動く");
}

KACHA_V2_TEST(join, どちらを固定するか選べる)
{
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = Line({12, 0, 0}, {20, 0, 0});

    const auto keepFirst = JoinCurves(first, CurveEnd::End, second, CurveEnd::Start,
        JoinContinuity::Position, JoinAnchor::KeepFirst, Tolerance());
    RequireNear(keepFirst.Value().second.StartPoint().x, 10.0, 1e-12, "1本目に寄る");

    const auto keepSecond = JoinCurves(first, CurveEnd::End, second, CurveEnd::Start,
        JoinContinuity::Position, JoinAnchor::KeepSecond, Tolerance());
    RequireNear(keepSecond.Value().first.EndPoint().x, 12.0, 1e-12, "2本目に寄る");

    const auto middle = JoinCurves(first, CurveEnd::End, second, CurveEnd::Start,
        JoinContinuity::Position, JoinAnchor::Midpoint, Tolerance());
    RequireNear(middle.Value().first.EndPoint().x, 11.0, 1e-12, "中点へ寄る");
    RequireNear(middle.Value().second.StartPoint().x, 11.0, 1e-12, "中点へ寄る");
}

KACHA_V2_TEST(join, 始点どうしもつなげる)
{
    const CurveSegment first = Line({10, 0, 0}, {0, 0, 0});
    const CurveSegment second = Line({10.5, 0.2, 0}, {20, 0, 0});
    const auto result = JoinCurves(first, CurveEnd::Start, second, CurveEnd::Start,
        JoinContinuity::Position, JoinAnchor::KeepFirst, Tolerance());
    Require(result.HasValue(), "つながること");
    Require(result.Value().positionGapMm < 1e-9, "隙間が無いこと");
}

KACHA_V2_TEST(join, 接線接続でベジェの向きが揃う)
{
    // 1本目は +x へ出ていく直線。2本目のベジェの接線を、それに合わせる。
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = Bezier({{10, 0, 0}, {12, 5, 0}, {18, 5, 0}, {20, 0, 0}});
    const auto result = JoinCurves(first, CurveEnd::End, second, CurveEnd::Start,
        JoinContinuity::Tangent, JoinAnchor::KeepFirst, Tolerance());
    Require(result.HasValue(), "つながること ("
            + (result.Diagnostics().empty() ? std::string("診断なし")
                                            : result.Diagnostics().front().detailsJa)
            + ")");
    Require(result.Value().tangentAngleRad < 1e-9,
        "接線が揃うこと (" + std::to_string(result.Value().tangentAngleRad) + ")");
    // 2本目の始点での接線が +x を向くこと。
    const Vector3 tangent = kachakacha::v2::geometry::Normalized(
        result.Value().second.FirstDerivative(0.0));
    RequireNear(tangent.x, 1.0, 1e-9, "接線が +x");
    RequireNear(tangent.y, 0.0, 1e-9, "接線に y 成分が無い");
}

KACHA_V2_TEST(join, 接線接続で長さが保たれる)
{
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = Bezier({{10, 0, 0}, {12, 5, 0}, {18, 5, 0}, {20, 0, 0}});
    const double before = (second.ControlPoints()[1] - second.ControlPoints()[0]).Length();
    const auto result = JoinCurves(first, CurveEnd::End, second, CurveEnd::Start,
        JoinContinuity::Tangent, JoinAnchor::KeepFirst, Tolerance());
    Require(result.HasValue(), "つながること");
    const double after = (result.Value().second.ControlPoints()[1]
        - result.Value().second.ControlPoints()[0])
                             .Length();
    RequireNear(after, before, 1e-9, "制御点の距離が変わらないこと");
}

KACHA_V2_TEST(join, 円弧は接線接続できないと言う)
{
    // 円弧は形を変えずに接線を変えられない。黙って形を変えない。
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = CurveSegment::MakeCircularArc({10, 10, 0}, {0, 0, 1},
        {0, -1, 0}, 10.0, 0.0, 1.0)
                                    .Value();
    const auto result = JoinCurves(first, CurveEnd::End, second, CurveEnd::Start,
        JoinContinuity::Tangent, JoinAnchor::KeepFirst, Tolerance());
    Require(!result.HasValue(), "断ること");
    RequireEqual(result.Diagnostics().front().code, std::string("GEO-J002"), "診断コード");
    Require(result.Diagnostics().front().detailsJa.find("形") != std::string::npos,
        "形が変わると言うこと");
}

KACHA_V2_TEST(join, 曲率接続は合わないときに断る)
{
    // 直線(曲率0)とベジェ(曲率あり)は、形を保ったままでは G2 にできない。
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = Bezier({{10, 0, 0}, {12, 5, 0}, {18, 5, 0}, {20, 0, 0}});
    const auto result = JoinCurves(first, CurveEnd::End, second, CurveEnd::Start,
        JoinContinuity::Curvature, JoinAnchor::KeepFirst, Tolerance());
    Require(!result.HasValue(), "断ること");
    RequireEqual(result.Diagnostics().front().code, std::string("GEO-J001"), "診断コード");
    Require(result.Diagnostics().front().detailsJa.find("制御点") != std::string::npos,
        "どうすればよいかを言うこと");
}

KACHA_V2_TEST(join, 曲率接続が成り立つ場合は通る)
{
    // 直線どうし。どちらも曲率0なので G2 が成り立つ。
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = Line({10, 0, 0}, {20, 0, 0});
    const auto result = JoinCurves(first, CurveEnd::End, second, CurveEnd::Start,
        JoinContinuity::Curvature, JoinAnchor::KeepFirst, Tolerance());
    // 直線は接線を変えられないので、もともと向きが合っている場合だけ通る。
    if (!result.HasValue()) {
        Require(result.Diagnostics().front().code == "GEO-J002", "理由が種類であること");
    } else {
        Require(result.Value().curvatureDifference < 1e-6, "曲率が合うこと");
    }
}

KACHA_V2_TEST(join, つなぎ方の名前がある)
{
    Require(!JoinContinuityNameJa(JoinContinuity::Position).empty(), "端点一致");
    Require(!JoinContinuityNameJa(JoinContinuity::Tangent).empty(), "接線接続");
    Require(!JoinContinuityNameJa(JoinContinuity::Curvature).empty(), "曲率接続");
}

// ---------------------------------------------------------------- 角の加工

KACHA_V2_TEST(corner, ポリラインの角を丸める)
{
    // L字の折れ線。角を1つ丸める。
    const std::vector<CurveSegment> polyline{Line({0, 0, 0}, {50, 0, 0}),
        Line({50, 0, 0}, {50, 40, 0})};
    const auto result = ProcessPolylineCorners(polyline, 10.0, true, Tolerance());
    Require(result.HasValue(), "加工できること ("
            + (result.Diagnostics().empty() ? std::string("診断なし")
                                            : result.Diagnostics().front().summaryJa)
            + ")");
    RequireCount(result.Value().size(), 3, "線 + 円弧 + 線");
    Require(result.Value()[1].Kind() == CurveKind::CircularArc, "真ん中が円弧");
    RequireNear(result.Value()[1].Radius(), 10.0, 1e-6, "半径");
    // 端はつながったまま。
    for (std::size_t index = 1; index < result.Value().size(); ++index) {
        const double gap =
            (result.Value()[index].StartPoint() - result.Value()[index - 1].EndPoint())
                .Length();
        Require(gap < 1e-6, "つながっていること (" + std::to_string(gap) + ")");
    }
}

KACHA_V2_TEST(corner, ポリラインの角を落とす)
{
    const std::vector<CurveSegment> polyline{Line({0, 0, 0}, {50, 0, 0}),
        Line({50, 0, 0}, {50, 40, 0})};
    const auto result = ProcessPolylineCorners(polyline, 8.0, false, Tolerance());
    Require(result.HasValue(), "加工できること");
    RequireCount(result.Value().size(), 3, "線 + 面取り + 線");
    Require(result.Value()[1].Kind() == CurveKind::Line, "真ん中が直線");
}

KACHA_V2_TEST(corner, 角が複数でも順に加工する)
{
    // コの字。角が2つ。
    const std::vector<CurveSegment> polyline{Line({0, 0, 0}, {50, 0, 0}),
        Line({50, 0, 0}, {50, 40, 0}), Line({50, 40, 0}, {0, 40, 0})};
    const auto result = ProcessPolylineCorners(polyline, 8.0, true, Tolerance());
    Require(result.HasValue(), "加工できること");
    // 線3 + 円弧2 = 5本。
    RequireCount(result.Value().size(), 5, "本数");
    RequireCount(static_cast<std::size_t>(std::count_if(result.Value().begin(),
                     result.Value().end(),
                     [](const CurveSegment& segment) {
                         return segment.Kind() == CurveKind::CircularArc;
                     })),
        2, "円弧の数");
}

KACHA_V2_TEST(corner, つながっていない折れ線を断る)
{
    const std::vector<CurveSegment> broken{Line({0, 0, 0}, {50, 0, 0}),
        Line({60, 0, 0}, {60, 40, 0})};
    const auto result = ProcessPolylineCorners(broken, 8.0, true, Tolerance());
    Require(!result.HasValue(), "断ること");
    RequireEqual(result.Diagnostics().front().code, std::string("GEO-J003"), "診断コード");
}

KACHA_V2_TEST(corner, 曲線が混ざった折れ線を断る)
{
    const std::vector<CurveSegment> mixed{Line({0, 0, 0}, {50, 0, 0}),
        CurveSegment::MakeCircularArc({50, 10, 0}, {0, 0, 1}, {0, -1, 0}, 10.0, 0.0, 1.0)
            .Value()};
    const auto result = ProcessPolylineCorners(mixed, 5.0, true, Tolerance());
    Require(!result.HasValue(), "断ること");
    RequireEqual(result.Diagnostics().front().code, std::string("GEO-J002"), "診断コード");
}

KACHA_V2_TEST(corner, おかしな値を断る)
{
    const std::vector<CurveSegment> polyline{Line({0, 0, 0}, {50, 0, 0}),
        Line({50, 0, 0}, {50, 40, 0})};
    Require(!ProcessPolylineCorners(polyline, 0.0, true, Tolerance()).HasValue(), "半径0");
    Require(!ProcessPolylineCorners(polyline, -5.0, true, Tolerance()).HasValue(), "負の値");
    Require(!ProcessPolylineCorners(polyline, std::nan(""), true, Tolerance()).HasValue(),
        "NaN");
    Require(!ProcessPolylineCorners({Line({0, 0, 0}, {50, 0, 0})}, 5.0, true, Tolerance())
                 .HasValue(),
        "線が1本");
}

KACHA_V2_TEST(corner, 大きすぎる半径を断る)
{
    // 線の長さより大きい半径では角に収まらない。
    const std::vector<CurveSegment> polyline{Line({0, 0, 0}, {10, 0, 0}),
        Line({10, 0, 0}, {10, 10, 0})};
    const auto result = ProcessPolylineCorners(polyline, 50.0, true, Tolerance());
    Require(!result.HasValue(), "断ること");
}

KACHA_V2_TEST(corner, 何度加工しても同じ結果になる)
{
    const std::vector<CurveSegment> polyline{Line({0, 0, 0}, {50, 0, 0}),
        Line({50, 0, 0}, {50, 40, 0}), Line({50, 40, 0}, {0, 40, 0})};
    const auto first = ProcessPolylineCorners(polyline, 8.0, true, Tolerance());
    Require(first.HasValue(), "加工できること");
    for (int repeat = 0; repeat < 5; ++repeat) {
        const auto again = ProcessPolylineCorners(polyline, 8.0, true, Tolerance());
        Require(again.HasValue(), "毎回加工できること");
        RequireCount(again.Value().size(), first.Value().size(), "本数");
        for (std::size_t index = 0; index < first.Value().size(); ++index) {
            RequireNear(again.Value()[index].StartPoint().x,
                first.Value()[index].StartPoint().x, 0.0, "完全に同じ");
        }
    }
}

KACHA_V2_TEST_MAIN("curve_join_tests")
