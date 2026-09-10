// ポリラインの角の加工(app/PolylineCorners.h)。V1 の「角の加工」。
#include "kachakacha/geometry/PolylineCorners.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/ArcBuilders.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CornerStyle;
using kachakacha::v2::geometry::ProcessPolylineCorners;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れる");
    return made.Value();
}

//! L 字(2辺)、コ字(3辺)、閉じた正方形(4辺)。
[[nodiscard]] std::vector<CurveSegment> Square()
{
    return {Line({0, 0, 0}, {20, 0, 0}), Line({20, 0, 0}, {20, 20, 0}),
        Line({20, 20, 0}, {0, 20, 0}), Line({0, 20, 0}, {0, 0, 0})};
}

} // namespace

KACHA_V2_TEST(polyline_corners, L字の角が1つ落ちて3本になる)
{
    const auto made = ProcessPolylineCorners(
        {Line({0, 0, 0}, {20, 0, 0}), Line({20, 0, 0}, {20, 20, 0})}, CornerStyle::Chamfer, 5.0,
        0.01);
    Require(made.HasValue(), "落とせる");
    RequireEqual(std::to_string(made.Value().size()), std::string("3"), "辺 + 面取り + 辺");
    RequireNear(made.Value()[0].EndPoint().x, 15.0, 1e-9, "1本目は 5 短くなる");
    RequireNear(made.Value()[2].StartPoint().y, 5.0, 1e-9, "2本目も 5 短くなる");
    Require(made.Value()[1].Kind() == CurveKind::Line, "面取りは直線");
}

KACHA_V2_TEST(polyline_corners, コ字は2つの角が丸まり短くなった辺が次へ渡る)
{
    const auto made = ProcessPolylineCorners({Line({0, 0, 0}, {20, 0, 0}),
                                                 Line({20, 0, 0}, {20, 20, 0}), Line({20, 20, 0}, {0, 20, 0})},
        CornerStyle::Fillet, 4.0, 0.01);
    Require(made.HasValue(), "丸められる");
    RequireEqual(std::to_string(made.Value().size()), std::string("5"), "3辺 + 2つの丸め");
    Require(made.Value()[1].Kind() == CurveKind::CircularArc, "丸めは円弧");
    // 真ん中の辺は両端で 4 ずつ短くなる。
    RequireNear(made.Value()[2].StartPoint().y, 4.0, 1e-9, "始点側");
    RequireNear(made.Value()[2].EndPoint().y, 16.0, 1e-9, "終点側");
}

KACHA_V2_TEST(polyline_corners, 閉じた正方形は4つ全部の角が落ちる)
{
    const auto made = ProcessPolylineCorners(Square(), CornerStyle::Chamfer, 3.0, 0.01);
    Require(made.HasValue(), "落とせる");
    RequireEqual(std::to_string(made.Value().size()), std::string("8"), "4辺 + 4面取り");
    // 最初の辺は最後の角でも短くなっている(始点が x=3 へ)。
    RequireNear(made.Value().front().StartPoint().x, 3.0, 1e-9, "先頭の辺の始点");
    RequireNear(made.Value().front().EndPoint().x, 17.0, 1e-9, "先頭の辺の終点");
}

KACHA_V2_TEST(polyline_corners, 直線でない角と離れた辺は触らず角が無ければ断る)
{
    const auto arc = kachakacha::v2::geometry::ArcThroughThreePoints({20, 0, 0}, {27, 7, 0},
        {20, 14, 0});
    Require(arc.HasValue(), "円弧");
    const auto mixed = ProcessPolylineCorners(
        {Line({0, 0, 0}, {20, 0, 0}), arc.Value(), Line({20, 14, 0}, {0, 14, 0})},
        CornerStyle::Chamfer, 3.0, 0.01);
    Require(!mixed.HasValue(), "直線どうしの角が無ければ断る");
    RequireEqual(mixed.Diagnostics().front().code, std::string("GEO-E021"), "理由の番号");
    const auto apart = ProcessPolylineCorners(
        {Line({0, 0, 0}, {20, 0, 0}), Line({30, 0, 0}, {30, 20, 0})}, CornerStyle::Chamfer, 3.0,
        0.01);
    Require(!apart.HasValue(), "離れた辺は角ではない");
    const auto one = ProcessPolylineCorners({Line({0, 0, 0}, {20, 0, 0})}, CornerStyle::Chamfer,
        3.0, 0.01);
    Require(!one.HasValue(), "1本では断る");
    // 大きすぎる量は geometry が断る(黙って縮めない)。
    const auto tooBig = ProcessPolylineCorners(
        {Line({0, 0, 0}, {20, 0, 0}), Line({20, 0, 0}, {20, 20, 0})}, CornerStyle::Chamfer, 50.0,
        0.01);
    Require(!tooBig.HasValue(), "辺より大きい量は断る");
}

KACHA_V2_TEST(polyline_corners, 頂点番号で1つの角だけ加工でき端は角でない)
{
    // コの字: (0,0)-(20,0)-(20,20)-(0,20)。頂点 1 = (20,0)、頂点 2 = (20,20)。
    const std::vector<CurveSegment> u = {Line({0, 0, 0}, {20, 0, 0}),
        Line({20, 0, 0}, {20, 20, 0}), Line({20, 20, 0}, {0, 20, 0})};
    const auto one = ProcessPolylineCorners(u, CornerStyle::Chamfer, 3.0, 0.01, 1);
    Require(one.HasValue(), "頂点 1 だけ落とせる");
    RequireEqual(std::to_string(one.Value().size()), std::string("4"), "辺 3 + 面取り 1");
    RequireNear(one.Value()[0].EndPoint().x, 17.0, 1e-9, "1本目が縮む");
    RequireNear(one.Value()[2].StartPoint().y, 3.0, 1e-9, "2本目が縮む");
    RequireNear(one.Value()[2].EndPoint().y, 20.0, 1e-9, "頂点 2 の角はそのまま");
    RequireNear(one.Value()[3].StartPoint().x, 20.0, 1e-9, "3本目はそのまま");
    // 開いた並びの両端は角ではない。無い番号も断る。
    const auto start = ProcessPolylineCorners(u, CornerStyle::Chamfer, 3.0, 0.01, 0);
    Require(!start.HasValue(), "頂点 0 は角でない");
    RequireEqual(start.Diagnostics().front().code, std::string("GEO-E021"), "理由");
    Require(!ProcessPolylineCorners(u, CornerStyle::Chamfer, 3.0, 0.01, 3).HasValue(),
        "最後の頂点は角でない");
    Require(!ProcessPolylineCorners(u, CornerStyle::Chamfer, 3.0, 0.01, 9).HasValue(),
        "無い番号は断る");
    // 閉じた正方形では頂点 0 が最後の辺と最初の辺の角。
    const std::vector<CurveSegment> square = {Line({0, 0, 0}, {20, 0, 0}),
        Line({20, 0, 0}, {20, 20, 0}), Line({20, 20, 0}, {0, 20, 0}), Line({0, 20, 0}, {0, 0, 0})};
    const auto zero = ProcessPolylineCorners(square, CornerStyle::Fillet, 4.0, 0.01, 0);
    Require(zero.HasValue(), "閉じた並びの頂点 0 を丸められる");
    RequireEqual(std::to_string(zero.Value().size()), std::string("5"), "辺 4 + 丸め 1");
    RequireNear(zero.Value().front().StartPoint().x, 4.0, 1e-9, "最初の辺の始点が角から 4");
    RequireNear(zero.Value()[3].EndPoint().y, 4.0, 1e-9, "最後の辺の終点が角から 4");
    Require(zero.Value()[4].Kind() == kachakacha::v2::geometry::CurveKind::CircularArc,
        "丸めは円弧");
}

KACHA_V2_TEST_MAIN("polyline_corners")
