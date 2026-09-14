// ワイヤーと core の曲線の往復(Codex P1-EXTRUDE-R1 の MISSING TESTS 6)。
//
// ここで見るのは **並びと向き** である。
// `FromWire` が位相の並びで返すと、戻した線を輪郭として使えない。
// 面の押し引きは面の縁を輪郭として押し出すので、ここが崩れると
// KER-C003「線がつながっていません」で毎回断られる。
//
// 実際、2026-09-14 にその不具合が出た。`TopExp_Explorer` は
// 位相の並びで辺を返すので、閉じた輪でも順に繋がっているとは限らない。
// `BRepTools_WireExplorer` に替えて直した。ここはその回帰試験である。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/kernel/OcctCurveConversion.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[maybe_unused]] [[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

//! 反時計回りの正方形。閉じている。
[[maybe_unused]] [[nodiscard]] std::vector<CurveSegment> Square(double size, double z = 0.0)
{
    return {
        Line({0.0, 0.0, z}, {size, 0.0, z}),
        Line({size, 0.0, z}, {size, size, z}),
        Line({size, size, z}, {0.0, size, z}),
        Line({0.0, size, z}, {0.0, 0.0, z}),
    };
}

//! 曲線の並びが端から端へ繋がっているか。**ここが今回の要である。**
[[maybe_unused]] [[nodiscard]] bool Connected(const std::vector<CurveSegment>& segments,
    double toleranceMm)
{
    for (std::size_t index = 0; index + 1 < segments.size(); ++index) {
        const Vector3 gap = segments[index + 1].StartPoint() - segments[index].EndPoint();
        if (gap.Length() > toleranceMm) {
            return false;
        }
    }
    return true;
}

} // namespace

#ifndef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_curve_round_trip_absent, カーネルが無い版では試さない)
{
    // 形を作る層が無い版では、この試験は何も確かめられない。
    // 「通った」ことにせず、確かめていないと言う。
    Require(true, "カーネルが無いので、この試験は PC の往復でだけ意味を持つ");
}

#else

KACHA_V2_TEST(kernel_curve_round_trip, 閉じた輪が繋がった順で戻る)
{
    const auto square = Square(40.0);
    const auto wire = kachakacha::v2::kernel::ToWire(square, 1.0e-6);
    Require(wire.HasValue(), "ワイヤーが作れること");
    const auto back = kachakacha::v2::kernel::FromWire(wire.Value(), 1.0e-6);
    Require(back.HasValue(), "戻せること");
    RequireEqual(std::to_string(back.Value().size()), "4", "辺の数");
    Require(Connected(back.Value(), 1.0e-6),
        "戻した並びが端から端へ繋がっている(位相の並びではない)");
    // 閉じていること。最後の終点が最初の始点へ戻る。
    const Vector3 gap =
        back.Value().back().EndPoint() - back.Value().front().StartPoint();
    Require(gap.Length() < 1.0e-6, "輪が閉じている");
}

KACHA_V2_TEST(kernel_curve_round_trip, 戻した輪はもう一度ワイヤーにできる)
{
    // ここが本題。面の押し引きは、戻した線をそのまま輪郭として押し出す。
    // 繋がった順で戻らないと、ここで KER-C003 になる。
    const auto wire = kachakacha::v2::kernel::ToWire(Square(40.0), 1.0e-6);
    Require(wire.HasValue(), "ワイヤーが作れること");
    const auto back = kachakacha::v2::kernel::FromWire(wire.Value(), 1.0e-6);
    Require(back.HasValue(), "戻せること");
    const auto again = kachakacha::v2::kernel::ToWire(back.Value(), 1.0e-6);
    Require(again.HasValue(),
        "戻した線をもう一度ワイヤーにできる(繋がっていないと断られる)");
    const auto third = kachakacha::v2::kernel::FromWire(again.Value(), 1.0e-6);
    Require(third.HasValue(), "3周目も戻せる");
    RequireEqual(std::to_string(third.Value().size()), "4", "何周しても辺の数は変わらない");
}

KACHA_V2_TEST(kernel_curve_round_trip, 逆向きに並べた輪も繋がった順で戻る)
{
    // 時計回り。向きが変わっても、繋がっていることは変わらない。
    std::vector<CurveSegment> reversed = {
        Line({0.0, 0.0, 0.0}, {0.0, 40.0, 0.0}),
        Line({0.0, 40.0, 0.0}, {40.0, 40.0, 0.0}),
        Line({40.0, 40.0, 0.0}, {40.0, 0.0, 0.0}),
        Line({40.0, 0.0, 0.0}, {0.0, 0.0, 0.0}),
    };
    const auto wire = kachakacha::v2::kernel::ToWire(reversed, 1.0e-6);
    Require(wire.HasValue(), "ワイヤーが作れること");
    const auto back = kachakacha::v2::kernel::FromWire(wire.Value(), 1.0e-6);
    Require(back.HasValue(), "戻せること");
    Require(Connected(back.Value(), 1.0e-6), "逆向きでも繋がった順で戻る");
    Require(kachakacha::v2::kernel::ToWire(back.Value(), 1.0e-6).HasValue(),
        "もう一度ワイヤーにできる");
}

KACHA_V2_TEST(kernel_curve_round_trip, 円弧は円弧のまま戻る)
{
    // 折れ線へ落とすと、型紙にしたときに角が多角形になる。種類を落とさない。
    const auto arc = CurveSegment::MakeCircularArc({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0},
        {1.0, 0.0, 0.0}, 25.0, 0.0, 1.5707963267948966);
    Require(arc.HasValue(), "円弧が作れること");
    const std::vector<CurveSegment> chain = {
        arc.Value(),
        Line(arc.Value().EndPoint(), {0.0, 0.0, 0.0}),
        Line({0.0, 0.0, 0.0}, arc.Value().StartPoint()),
    };
    const auto wire = kachakacha::v2::kernel::ToWire(chain, 1.0e-6);
    Require(wire.HasValue(), "ワイヤーが作れること");
    const auto back = kachakacha::v2::kernel::FromWire(wire.Value(), 1.0e-6);
    Require(back.HasValue(), "戻せること");
    RequireEqual(std::to_string(back.Value().size()), "3", "辺の数");
    Require(Connected(back.Value(), 1.0e-6), "繋がった順で戻る");
    bool foundArc = false;
    double radius = 0.0;
    for (const auto& segment : back.Value()) {
        if (segment.Kind() == kachakacha::v2::geometry::CurveKind::CircularArc) {
            foundArc = true;
            radius = segment.Radius();
        }
    }
    Require(foundArc, "円弧が円弧のまま戻る(折れ線になっていない)");
    RequireNear(radius, 25.0, 1.0e-6, "半径も変わらない");
}

KACHA_V2_TEST(kernel_curve_round_trip, 繋がっていない並びは勝手に繋げずに断る)
{
    // 「できないことを、できたことにしない」。
    std::vector<CurveSegment> broken = {
        Line({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}),
        Line({20.0, 0.0, 0.0}, {30.0, 0.0, 0.0}),
    };
    const auto wire = kachakacha::v2::kernel::ToWire(broken, 1.0e-6);
    Require(!wire.HasValue(), "断ること");
    RequireEqual(wire.Diagnostics().front().code, std::string("KER-C003"),
        "繋がっていないときの診断コード");
}

#endif // KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST_MAIN("kernel_curve_round_trip_tests")
