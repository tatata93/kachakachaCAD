// 線と線のつなぎ方(geometry-contract §4、V1同等)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/WireConnect.h"

#include <cmath>
#include <string>

using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::geometry::ConnectContinuity;
using kachakacha::v2::geometry::ConnectContinuityNameJa;
using kachakacha::v2::geometry::ConnectCurves;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::JoinCurves;
using kachakacha::v2::geometry::SplitCurveAtIntersections;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kTolerance = 0.01;

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

[[nodiscard]] CurveSegment Line(Vector3 start, Vector3 end)
{
    const auto made = CurveSegment::MakeLine(start, end);
    Require(made.HasValue(), "線が作れる");
    return made.Value();
}

[[nodiscard]] double GapBetween(const Vector3& a, const Vector3& b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)
        + (a.z - b.z) * (a.z - b.z));
}

} // namespace

KACHA_V2_TEST(wire_connect, 端点一致は両方を同じだけ動かす)
{
    // 片方だけ動かすと、その線だけが設計とずれる。半分ずつ寄せる。
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = Line({10.4, 0, 0}, {20, 0, 0});
    const auto joined = ConnectCurves(first, second, ConnectContinuity::Position,
        kTolerance);
    Require(joined.HasValue(), "つながる");
    RequireNear(joined.Value().joint.x, 10.2, 1.0e-9, "中点で合う");
    RequireNear(joined.Value().first.EndPoint().x, 10.2, 1.0e-9, "1本目が伸びる");
    RequireNear(joined.Value().second.StartPoint().x, 10.2, 1.0e-9, "2本目が縮む");
    RequireNear(joined.Value().movedMm, 0.2, 1.0e-9, "動かした距離");
}

KACHA_V2_TEST(wire_connect, もう合っていれば動かさない)
{
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = Line({10, 0, 0}, {20, 0, 0});
    const auto joined = ConnectCurves(first, second, ConnectContinuity::Position,
        kTolerance);
    Require(joined.HasValue(), "つながる");
    RequireNear(joined.Value().movedMm, 0.0, 1.0e-12, "動かさない");
}

KACHA_V2_TEST(wire_connect, 近いほうの端どうしをつなぐ)
{
    // 2本目が逆向きに置いてあっても、近い端どうしで判断する。
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = Line({20, 0, 0}, {10.4, 0, 0});
    const auto joined = ConnectCurves(first, second, ConnectContinuity::Position,
        kTolerance);
    Require(joined.HasValue(), "つながる");
    RequireNear(joined.Value().joint.x, 10.2, 1.0e-9, "近い端で合う");
}

KACHA_V2_TEST(wire_connect, 一直線なら接線接続になる)
{
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = Line({10, 0, 0}, {20, 0, 0});
    Require(ConnectCurves(first, second, ConnectContinuity::Tangent, kTolerance).HasValue(),
        "つながる");
}

KACHA_V2_TEST(wire_connect, 折れていれば接線接続は断る)
{
    // 直線を曲線に化けさせてまでそろえない。できないことをできたことにしない。
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = Line({10, 0, 0}, {10, 10, 0});
    const auto joined = ConnectCurves(first, second, ConnectContinuity::Tangent,
        kTolerance);
    Require(!joined.HasValue(), "断る");
    RequireEqual(FirstCode(joined.Diagnostics()), std::string("GEO-E013"), "コード");
    // 何度ずれているかを言う。言わないと、どれだけ回せばよいか分からない。
    Require(joined.Diagnostics().front().detailsJa.find("90") != std::string::npos,
        "ずれを言う");
}

KACHA_V2_TEST(wire_connect, 直線どうしは曲率接続できる)
{
    // どちらも曲がり0なので、そろっている。
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = Line({10, 0, 0}, {20, 0, 0});
    Require(
        ConnectCurves(first, second, ConnectContinuity::Curvature, kTolerance).HasValue(),
        "つながる");
}

KACHA_V2_TEST(wire_connect, 直線と円弧は曲率接続を断る)
{
    // 直線は曲がり0、円弧は0でない。そろわない。
    const CurveSegment line = Line({0, 0, 0}, {20, 0, 0});
    const auto arc = CurveSegment::MakeCircularArc({20, 10, 0}, {0, 0, 1}, {0, -1, 0}, 10.0,
        0.0, 1.5707963267948966);
    Require(arc.HasValue(), "円弧が作れる");
    const auto joined = ConnectCurves(line, arc.Value(), ConnectContinuity::Curvature,
        kTolerance);
    Require(!joined.HasValue(), "断る");
    RequireEqual(FirstCode(joined.Diagnostics()), std::string("GEO-E014"), "コード");
}

KACHA_V2_TEST(wire_connect, 円弧の端は動かさない)
{
    // 円弧の端を引っぱると途中の形まで変わる。だから断る。
    const auto arc = CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0,
        0.0, 1.5707963267948966);
    Require(arc.HasValue(), "円弧が作れる");
    const CurveSegment line = Line({5, 15, 0}, {5, 25, 0});
    const auto joined = ConnectCurves(arc.Value(), line, ConnectContinuity::Position,
        kTolerance);
    Require(!joined.HasValue(), "断る");
    RequireEqual(FirstCode(joined.Diagnostics()), std::string("GEO-E011"), "コード");
}

KACHA_V2_TEST(wire_connect, つなぎ方の名前がそろっている)
{
    for (const auto value : {ConnectContinuity::Position, ConnectContinuity::Tangent,
             ConnectContinuity::Curvature}) {
        Require(!ConnectContinuityNameJa(value).empty(), "名前がある");
    }
}

KACHA_V2_TEST(wire_connect, 交点で2つに分かれる)
{
    const CurveSegment horizontal = Line({-10, 0, 0}, {10, 0, 0});
    const CurveSegment vertical = Line({0, -10, 0}, {0, 10, 0});
    const auto pieces = SplitCurveAtIntersections(horizontal, {vertical}, kTolerance);
    Require(pieces.HasValue(), "切れる");
    RequireEqual(std::to_string(pieces.Value().size()), std::string("2"), "2本");
    RequireNear(pieces.Value()[0].EndPoint().x, 0.0, 1.0e-9, "交点で切れる");
    RequireNear(pieces.Value()[1].StartPoint().x, 0.0, 1.0e-9, "交点から始まる");
}

KACHA_V2_TEST(wire_connect, 交点が2つなら3つに分かれる)
{
    const CurveSegment horizontal = Line({-10, 0, 0}, {10, 0, 0});
    const CurveSegment left = Line({-5, -10, 0}, {-5, 10, 0});
    const CurveSegment right = Line({5, -10, 0}, {5, 10, 0});
    const auto pieces = SplitCurveAtIntersections(horizontal, {left, right}, kTolerance);
    Require(pieces.HasValue(), "切れる");
    RequireEqual(std::to_string(pieces.Value().size()), std::string("3"), "3本");
    // 切っても全体の長さは変わらない。
    double total = 0.0;
    for (const auto& piece : pieces.Value()) {
        total += piece.TotalLength(kTolerance);
    }
    RequireNear(total, 20.0, 1.0e-6, "長さの合計は変わらない");
}

KACHA_V2_TEST(wire_connect, 交わっていなければ切らない)
{
    const CurveSegment horizontal = Line({-10, 0, 0}, {10, 0, 0});
    const CurveSegment away = Line({0, 50, 0}, {0, 60, 0});
    const auto pieces = SplitCurveAtIntersections(horizontal, {away}, kTolerance);
    Require(!pieces.HasValue(), "断る");
    RequireEqual(FirstCode(pieces.Diagnostics()), std::string("GEO-E015"), "コード");
}

KACHA_V2_TEST(wire_connect, 端で交わっていても切らない)
{
    // 切っても同じ形が2本できるだけである。
    const CurveSegment horizontal = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment atEnd = Line({10, -5, 0}, {10, 5, 0});
    const auto pieces = SplitCurveAtIntersections(horizontal, {atEnd}, kTolerance);
    Require(!pieces.HasValue(), "断る");
}

KACHA_V2_TEST(wire_connect, つながっている線を1本の並びにできる)
{
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = Line({10, 0, 0}, {10, 10, 0});
    const auto joined = JoinCurves({first, second}, kTolerance);
    Require(joined.HasValue(), "つながる");
    RequireEqual(std::to_string(joined.Value().size()), std::string("2"), "2本のまま");
    RequireNear(GapBetween(joined.Value()[0].EndPoint(), joined.Value()[1].StartPoint()), 0.0,
        1.0e-9, "端がつながる");
}

KACHA_V2_TEST(wire_connect, 逆向きの線は向きをそろえる)
{
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment second = Line({10, 10, 0}, {10, 0, 0});
    const auto joined = JoinCurves({first, second}, kTolerance);
    Require(joined.HasValue(), "つながる");
    RequireNear(GapBetween(joined.Value()[0].EndPoint(), joined.Value()[1].StartPoint()), 0.0,
        1.0e-9, "端がつながる");
    // 形は変えない。長さはそのまま。
    RequireNear(joined.Value()[1].TotalLength(kTolerance), 10.0, 1.0e-6, "長さは同じ");
}

KACHA_V2_TEST(wire_connect, 離れている線は結合できない)
{
    const CurveSegment first = Line({0, 0, 0}, {10, 0, 0});
    const CurveSegment away = Line({50, 0, 0}, {60, 0, 0});
    Require(!JoinCurves({first, away}, kTolerance).HasValue(), "断る");
}

KACHA_V2_TEST(wire_connect, 1本だけでは結合できない)
{
    const auto joined = JoinCurves({Line({0, 0, 0}, {10, 0, 0})}, kTolerance);
    Require(!joined.HasValue(), "断る");
    RequireEqual(FirstCode(joined.Diagnostics()), std::string("GEO-E016"), "コード");
}

KACHA_V2_TEST_MAIN("wire_connect_tests")
