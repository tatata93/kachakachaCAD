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

#ifdef KACHACAD_V2_WITH_OCCT
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <GeomAbs_Shape.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_Curve.hxx>
#include <NCollection_Array1.hxx>
#include <gp_Pnt.hxx>
#endif

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

[[maybe_unused]] [[nodiscard]] CurveSegment StraightSegment(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

//! 反時計回りの正方形。閉じている。
//!
//! 名前を `Square` にしてはいけない。OCCT が同じ名前の関数を大域に置いており、
//! 無名名前空間の中身も大域の一員なので、`Square(40.0)` がどちらとも取れて
//! MSVC が止まる。雲は OCCT を組み立てないので、ここは PC でしか出ない。
[[maybe_unused]] [[nodiscard]] std::vector<CurveSegment> SquareLoop(double size, double z = 0.0)
{
    return {
        StraightSegment({0.0, 0.0, z}, {size, 0.0, z}),
        StraightSegment({size, 0.0, z}, {size, size, z}),
        StraightSegment({size, size, z}, {0.0, size, z}),
        StraightSegment({0.0, size, z}, {0.0, 0.0, z}),
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
    const auto square = SquareLoop(40.0);
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
    const auto wire = kachakacha::v2::kernel::ToWire(SquareLoop(40.0), 1.0e-6);
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
        StraightSegment({0.0, 0.0, 0.0}, {0.0, 40.0, 0.0}),
        StraightSegment({0.0, 40.0, 0.0}, {40.0, 40.0, 0.0}),
        StraightSegment({40.0, 40.0, 0.0}, {40.0, 0.0, 0.0}),
        StraightSegment({40.0, 0.0, 0.0}, {0.0, 0.0, 0.0}),
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
        StraightSegment(arc.Value().EndPoint(), {0.0, 0.0, 0.0}),
        StraightSegment({0.0, 0.0, 0.0}, arc.Value().StartPoint()),
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
        StraightSegment({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}),
        StraightSegment({20.0, 0.0, 0.0}, {30.0, 0.0, 0.0}),
    };
    const auto wire = kachakacha::v2::kernel::ToWire(broken, 1.0e-6);
    Require(!wire.HasValue(), "断ること");
    RequireEqual(wire.Diagnostics().front().code, std::string("KER-C003"),
        "繋がっていないときの診断コード");
}

KACHA_V2_TEST(kernel_curve_round_trip, Bsplineは核へ渡しても形が変わらず同じ制御点で戻る)
{
    // core の B-spline は端を重ねない一様 3 次。核へは区間ごとのベジェで厳密に渡し、
    // 戻すときは値に合わせて写す(2026-09-22 まで制御点をそのまま写していて形が変わった)。
    const std::vector<Vector3> control{{0, 0, 0}, {10, 6, 0}, {20, -4, 3}, {30, 7, 1},
        {40, -2, -2}, {50, 3, 0}};
    const CurveSegment original = CurveSegment::MakeCubicBSpline(control).Value();
    const auto edge = kachakacha::v2::kernel::ToEdge(original);
    Require(edge.HasValue(), "辺にできる");
    double first = 0.0;
    double last = 0.0;
    const occ::handle<Geom_Curve> curve = BRep_Tool::Curve(edge.Value(), first, last);
    for (int k = 0; k <= 20; ++k) {
        const double t = k / 20.0;
        const gp_Pnt p = curve->Value(first + (last - first) * t);
        const Vector3 expected = original.Evaluate(t);
        Require(std::abs(p.X() - expected.x) + std::abs(p.Y() - expected.y)
                    + std::abs(p.Z() - expected.z) < 1.0e-7,
            "核の曲線が core と同じ形(t=" + std::to_string(t) + ")");
    }
    const auto back = kachakacha::v2::kernel::FromEdge(edge.Value(), 1.0e-6);
    Require(back.HasValue(), "戻せる");
    Require(back.Value().ControlPoints().size() == control.size(), "制御点の数も同じ");
    for (std::size_t i = 0; i < control.size(); ++i) {
        Require((back.Value().ControlPoints()[i] - control[i]).Length() < 1.0e-6,
            "制御点が同じ(" + std::to_string(i) + ")");
    }
}

KACHA_V2_TEST(kernel_curve_round_trip, 節点が不揃いな核のBsplineも形を保って戻る)
{
    NCollection_Array1<gp_Pnt> points(1, 9);
    for (int i = 1; i <= 9; ++i) {
        const double x = (i - 1) * (i - 1) * 1.5;   // 間隔が不揃い
        points.SetValue(i, gp_Pnt(x, 8.0 * std::sin(x / 12.0), 0.3 * x));
    }
    GeomAPI_PointsToBSpline fit(points, 3, 3, GeomAbs_C2, 1.0e-6);
    Require(fit.IsDone(), "核で B-spline を作れる");
    BRepBuilderAPI_MakeEdge maker{occ::handle<Geom_Curve>(fit.Curve())};
    Require(maker.IsDone(), "辺にできる");
    const auto back = kachakacha::v2::kernel::FromEdge(maker.Edge(), 1.0e-6);
    Require(back.HasValue(), "戻せる: "
            + (back.HasValue() ? std::string() : back.Diagnostics().front().summaryJa));
    const occ::handle<Geom_Curve> curve = fit.Curve();
    const double first = curve->FirstParameter();
    const double last = curve->LastParameter();
    for (int k = 0; k <= 40; ++k) {
        const double t = k / 40.0;
        const gp_Pnt p = curve->Value(first + (last - first) * t);
        const Vector3 got = back.Value().Evaluate(t);
        Require(std::sqrt((p.X() - got.x) * (p.X() - got.x) + (p.Y() - got.y) * (p.Y() - got.y)
                    + (p.Z() - got.z) * (p.Z() - got.z)) <= 1.0e-4 + 1.0e-9,
            "形が許容以内(t=" + std::to_string(t) + ")");
    }
}

#endif // KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST_MAIN("kernel_curve_round_trip_tests")
