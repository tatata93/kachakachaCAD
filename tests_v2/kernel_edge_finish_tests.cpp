// 立体の辺を丸める・落とす層の試験(kernel/OcctEdgeFinish、matrix P-12)。
//
// 辺は真ん中の点で指す。丸め・面取りで減る体積は式と合う(丸め: (1 - π/4) r² L、
// 面取り: d² / 2 × L)。指した辺が無ければ・大きすぎれば断る。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctEdgeFinish.h"
#include "kachakacha/kernel/OcctExtrude.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::kernel::BuildExtrude;
using kachakacha::v2::kernel::EdgeFinishKind;
using kachakacha::v2::kernel::FinishSolidEdges;
using kachakacha::v2::kernel::NearestSolidEdge;
using kachakacha::v2::modeling::AnalyzeExtrudeRequest;
using kachakacha::v2::modeling::ExtrudeDirectionMode;
using kachakacha::v2::modeling::ExtrudeExtentMode;
using kachakacha::v2::modeling::ExtrudeProfile;
using kachakacha::v2::modeling::ExtrudeRequest;
using kachakacha::v2::modeling::KernelShapeHandle;
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

[[maybe_unused]] [[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

//! 箱(xy の矩形を z = 0 から高さぶん押し出す)。作れなければ空の番号。
[[maybe_unused]] [[nodiscard]] KernelShapeHandle Box(double x1, double y1, double height)
{
    ExtrudeProfile profile;
    profile.closed = true;
    profile.segments = {Line({0, 0, 0}, {x1, 0, 0}), Line({x1, 0, 0}, {x1, y1, 0}),
        Line({x1, y1, 0}, {0, y1, 0}), Line({0, y1, 0}, {0, 0, 0})};
    ExtrudeRequest request;
    request.profiles = {profile};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = height;
    request.outputs.part = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    if (!analysis.HasValue()) {
        return {};
    }
    const auto built = BuildExtrude(request, analysis.Value(), Tolerance(), {});
    if (!built.HasValue() || built.Value().parts.empty()) {
        return {};
    }
    return built.Value().parts.front().handle;
}

[[maybe_unused]] [[nodiscard]] std::string FirstCode(
    const std::vector<kachakacha::v2::base::Diagnostic>& found)
{
    return found.empty() ? std::string("(なし)") : found.front().code;
}

} // namespace

#ifndef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_edge_finish_absent, カーネルが無い版は丸めずに断る)
{
    const auto made = FinishSolidEdges({}, EdgeFinishKind::Fillet, 1.0, {Vector3{}}, Tolerance());
    Require(!made.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(made.Diagnostics()), "KER-R004", "カーネル不在の診断コード");
}

#else

KACHA_V2_TEST(kernel_edge_finish, 箱の上の縁を丸めると体積が式どおり減る)
{
    // 40 × 20 × 30。上の面の、x に沿った縁(y = 0, z = 30、長さ 40)を半径 3 で丸める。
    const auto box = Box(40, 20, 30);
    Require(box.Valid(), "箱が作れること");
    const auto edge = NearestSolidEdge(box, Vector3{20.0, 0.5, 29.5});
    Require(edge.HasValue(), "押した点に近い辺が拾える: " + edge.FirstSummaryJa());
    RequireNear(edge.Value().midpoint.x, 20.0, 1.0e-6, "辺の真ん中 x");
    RequireNear(edge.Value().midpoint.y, 0.0, 1.0e-6, "辺の真ん中 y");
    RequireNear(edge.Value().midpoint.z, 30.0, 1.0e-6, "辺の真ん中 z");
    const auto rounded = FinishSolidEdges(box, EdgeFinishKind::Fillet, 3.0,
        {edge.Value().midpoint}, Tolerance());
    Require(rounded.HasValue(), "丸められる: " + rounded.FirstSummaryJa());
    const double removed = (1.0 - 3.14159265358979323846 / 4.0) * 9.0 * 40.0;
    RequireNear(rounded.Value().previousVolumeMm3, 24000.0, 1.0e-3, "前の体積");
    RequireNear(rounded.Value().volumeMm3, 24000.0 - removed, 0.05, "減った体積は (1 - π/4) r² L");
}

KACHA_V2_TEST(kernel_edge_finish, 縁を2本面取りすると1本ぶんの三角柱ずつ減る)
{
    const auto box = Box(40, 20, 30);
    Require(box.Valid(), "箱が作れること");
    // 上の面の x に沿った 2 本(y = 0 と y = 20)。C 2。
    const auto chamfered = FinishSolidEdges(box, EdgeFinishKind::Chamfer, 2.0,
        {Vector3{20.0, 0.0, 30.0}, Vector3{20.0, 20.0, 30.0}}, Tolerance());
    Require(chamfered.HasValue(), "面取りできる: " + chamfered.FirstSummaryJa());
    RequireNear(chamfered.Value().volumeMm3, 24000.0 - 2.0 * (2.0 * 2.0 / 2.0) * 40.0, 1.0e-3,
        "減った体積は 2 × d²/2 × L");
}

KACHA_V2_TEST(kernel_edge_finish, 無い辺と大きすぎる丸めは断る)
{
    const auto box = Box(40, 20, 30);
    Require(box.Valid(), "箱が作れること");
    const auto missing = FinishSolidEdges(box, EdgeFinishKind::Fillet, 1.0,
        {Vector3{20.0, 10.0, 15.0}}, Tolerance());
    Require(!missing.HasValue() && FirstCode(missing.Diagnostics()) == "KER-R002",
        "箱の中の点には辺が無いと言う: " + missing.FirstSummaryJa());
    const auto huge = FinishSolidEdges(box, EdgeFinishKind::Fillet, 50.0,
        {Vector3{20.0, 0.0, 30.0}}, Tolerance());
    Require(!huge.HasValue(), "箱より大きい半径は作れたことにしない: " + huge.FirstSummaryJa());
    const auto none = FinishSolidEdges(box, EdgeFinishKind::Chamfer, 1.0, {}, Tolerance());
    Require(!none.HasValue() && FirstCode(none.Diagnostics()) == "KER-R002", "辺が無ければ断る");
}

#endif

KACHA_V2_TEST_MAIN("kernel_edge_finish_tests")
