// ポリラインの角の加工(app/PolylineCorners.h)。V1 の「角の加工」。
#include "kachakacha/geometry/PolylineCorners.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/ArcBuilders.h"

#include <cmath>
#include <string>

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

KACHA_V2_TEST_MAIN("polyline_corners")
