// 部品どうしの足す・引く・交差をカーネルで実際に行う層の試験(kernel/OcctBoolean)。
//
// 約束は 2 つ。何も変わらなかったら成功したことにしない。何も残らなかったら断る。
// 断るときの理由は演算ごとに違う(「触れていない」と一律に言わない)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctBoolean.h"
#include "kachakacha/kernel/OcctExtrude.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <TopoDS_Shape.hxx>
#include <BRepTools.hxx>
#include <sstream>
#endif

#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::kernel::BooleanOperation;
using kachakacha::v2::kernel::BuildBoolean;
using kachakacha::v2::kernel::BuildExtrude;
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

[[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

//! 箱(xy の矩形を z から高さぶん押し出す)。作れなければ空の番号。
[[maybe_unused]] [[nodiscard]] KernelShapeHandle Box(double x0, double y0, double x1, double y1, double z,
    double height)
{
    ExtrudeProfile profile;
    profile.closed = true;
    profile.segments = {
        Line({x0, y0, z}, {x1, y0, z}),
        Line({x1, y0, z}, {x1, y1, z}),
        Line({x1, y1, z}, {x0, y1, z}),
        Line({x0, y1, z}, {x0, y0, z}),
    };
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

[[nodiscard]] std::string FirstCode(const std::vector<kachakacha::v2::base::Diagnostic>& found)
{
    return found.empty() ? std::string("(なし)") : found.front().code;
}

} // namespace

#ifndef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_boolean_absent, カーネルが無い版は足し引きせずに断る)
{
    const auto built = BuildBoolean(BooleanOperation::Intersection, {}, {}, 0.01);
    Require(!built.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), "KER-B005", "カーネル不在の診断コード");
}

#else

KACHA_V2_TEST(kernel_boolean, batch_union_matches_ordered_union_and_preserves_inputs)
{
    using kachakacha::v2::kernel::BuildBooleans;
    const auto a = Box(0, 0, 10, 10, 0, 10);
    const auto b = Box(5, 0, 15, 10, 0, 10);
    const auto c = Box(10, 0, 20, 10, 0, 10);
    const auto serialized = [](KernelShapeHandle handle) {
        TopoDS_Shape shape;
        Require(kachakacha::v2::kernel::LookupShape(handle, shape), "input exists");
        std::ostringstream stream;
        BRepTools::Write(shape, stream);
        return stream.str();
    };
    const auto aa = serialized(a), bb = serialized(b), cc = serialized(c);
    const auto batch = BuildBooleans(BooleanOperation::Union, a, {b, c}, 1e-6);
    Require(batch.HasValue(), batch.FirstSummaryJa());
    const auto reversed = BuildBooleans(BooleanOperation::Union, c, {b, a}, 1e-6);
    Require(reversed.HasValue(), reversed.FirstSummaryJa());
    RequireNear(batch.Value().volumeMm3, 2000, 1e-6, "exact union volume");
    RequireNear(reversed.Value().volumeMm3, batch.Value().volumeMm3, 1e-6, "input order invariant");
    RequireNear(batch.Value().previousVolumeMm3, 1000, 1e-6, "original base volume");
    Require(serialized(a) == aa && serialized(b) == bb && serialized(c) == cc,
        "batch union preserves all source BREP geometry and topology");
    const auto ab = BuildBoolean(BooleanOperation::Union, a, b, 1e-6);
    Require(ab.HasValue(), ab.FirstSummaryJa());
    const auto sequential = BuildBoolean(BooleanOperation::Union, ab.Value().handle, c, 1e-6);
    Require(sequential.HasValue(), sequential.FirstSummaryJa());
    RequireNear(batch.Value().volumeMm3, sequential.Value().volumeMm3, 1e-6, "same result as ordered union");
}

KACHA_V2_TEST(kernel_boolean, binary_operations_preserve_source_geometry)
{
    for(const auto operation:{BooleanOperation::Union,BooleanOperation::Difference,BooleanOperation::Intersection}) {
        const auto a=Box(0,0,10,10,0,10),b=Box(5,0,15,10,0,10);
        const auto serialized=[](KernelShapeHandle handle) {
            TopoDS_Shape shape;Require(kachakacha::v2::kernel::LookupShape(handle,shape),"input exists");
            std::ostringstream stream;BRepTools::Write(shape,stream);return stream.str();
        };
        const auto beforeA=serialized(a),beforeB=serialized(b);
        const auto result=BuildBoolean(operation,a,b,1e-6);
        Require(result.HasValue(),result.FirstSummaryJa());
        Require(serialized(a)==beforeA&&serialized(b)==beforeB,"preview operation leaves original BRep unchanged");
        RequireNear(result.Value().volumeMm3,operation==BooleanOperation::Union?1500:500,1e-6,"independent analytic volume");
        kachakacha::v2::kernel::ReleaseShape(result.Value().handle);
        kachakacha::v2::kernel::ReleaseShape(a);kachakacha::v2::kernel::ReleaseShape(b);
    }
}

KACHA_V2_TEST(kernel_boolean, batch_union_redundant_tools_and_missing_inputs)
{
    using kachakacha::v2::kernel::BuildBooleans;
    const auto a = Box(0, 0, 10, 10, 0, 10);
    const auto inside = Box(2, 2, 4, 4, 2, 2);
    const auto b = Box(5, 0, 15, 10, 0, 10);
    const auto useful = BuildBooleans(BooleanOperation::Union, a, {inside, b}, 1e-6);
    Require(useful.HasValue(), useful.FirstSummaryJa());
    RequireNear(useful.Value().volumeMm3, 1500, 1e-6, "contained tool does not reject useful batch");
    const auto unchanged = BuildBooleans(BooleanOperation::Union, a, {inside, inside}, 1e-6);
    Require(!unchanged.HasValue() && FirstCode(unchanged.Diagnostics()) == "KER-B003", "whole no-change remains rejected");
    const auto missing = BuildBooleans(BooleanOperation::Union, a, {b, {}}, 1e-6);
    Require(!missing.HasValue() && FirstCode(missing.Diagnostics()) == "KER-B002", "missing input rejects entire batch");
}

KACHA_V2_TEST(kernel_boolean, inward_input_is_rejected_instead_of_hidden_by_absolute_volume)
{
    const auto a = Box(0, 0, 10, 10, 0, 10);
    const auto b = Box(5, 0, 15, 10, 0, 10);
    TopoDS_Shape reversed;
    Require(kachakacha::v2::kernel::LookupShape(a, reversed), "input available");
    reversed.Reverse();
    const auto bad = kachakacha::v2::kernel::StoreShape(reversed);
    const auto result = BuildBoolean(BooleanOperation::Union, bad, b, 1e-6);
    Require(!result.HasValue() && FirstCode(result.Diagnostics()) == "KER-B001",
        "an inverted solid must never produce a successful union");
    kachakacha::v2::kernel::ReleaseShape(a);
    kachakacha::v2::kernel::ReleaseShape(b);
    kachakacha::v2::kernel::ReleaseShape(bad);
}

KACHA_V2_TEST(kernel_boolean, 重なる2つの箱を足す引く交差すると体積が合う)
{
    // A: 40×20×30 = 24000、B: x を 20 ずらした同じ箱。重なりは 20×20×30 = 12000。
    const auto a = Box(0, 0, 40, 20, 0, 30);
    const auto b = Box(20, 0, 60, 20, 0, 30);
    Require(a.Valid() && b.Valid(), "箱が作れること");
    const auto sum = BuildBoolean(BooleanOperation::Union, a, b, 0.01);
    Require(sum.HasValue(), "足せる: " + sum.FirstSummaryJa());
    RequireNear(sum.Value().volumeMm3, 36000.0, 1.0e-3, "足すと 36000");
    RequireNear(sum.Value().previousVolumeMm3, 24000.0, 1.0e-3, "前の体積は土台の 24000");
    const auto rest = BuildBoolean(BooleanOperation::Difference, a, b, 0.01);
    Require(rest.HasValue(), "引ける: " + rest.FirstSummaryJa());
    RequireNear(rest.Value().volumeMm3, 12000.0, 1.0e-3, "引くと 12000");
    const auto common = BuildBoolean(BooleanOperation::Intersection, a, b, 0.01);
    Require(common.HasValue(), "交差できる: " + common.FirstSummaryJa());
    RequireNear(common.Value().volumeMm3, 12000.0, 1.0e-3, "交差は重なりの 12000");
    Require(common.Value().handle.Valid(), "出来た形の番号がある");
}

KACHA_V2_TEST(kernel_boolean, 何も変わらないか何も残らないときは演算ごとの理由で断る)
{
    const auto big = Box(0, 0, 40, 40, 0, 40);
    const auto inside = Box(10, 10, 20, 20, 10, 10);
    const auto apart = Box(100, 0, 120, 20, 0, 20);
    const auto around = Box(-10, -10, 50, 50, -10, 60);
    Require(big.Valid() && inside.Valid() && apart.Valid() && around.Valid(), "箱が作れること");
    // 足す: 相手が土台の中なら何も変わらない。
    const auto union0 = BuildBoolean(BooleanOperation::Union, big, inside, 0.01);
    Require(!union0.HasValue() && FirstCode(union0.Diagnostics()) == "KER-B003"
            && union0.FirstSummaryJa().find("収まって") != std::string::npos,
        "足す: 土台の中に収まっていると言う: " + union0.FirstSummaryJa());
    // 引く: 重ならなければ何も変わらない。
    const auto cut0 = BuildBoolean(BooleanOperation::Difference, big, apart, 0.01);
    Require(!cut0.HasValue() && FirstCode(cut0.Diagnostics()) == "KER-B003"
            && cut0.FirstSummaryJa().find("重なっていない") != std::string::npos,
        "引く: 重なっていないと言う: " + cut0.FirstSummaryJa());
    // 交差: 重ならなければ何も残らない。相手が土台を全部含めば土台のまま。
    const auto common0 = BuildBoolean(BooleanOperation::Intersection, big, apart, 0.01);
    Require(!common0.HasValue() && FirstCode(common0.Diagnostics()) == "KER-B004"
            && common0.FirstSummaryJa().find("共通部分がありません") != std::string::npos,
        "交差: 共通部分が無いと言う: " + common0.FirstSummaryJa());
    const auto common1 = BuildBoolean(BooleanOperation::Intersection, big, around, 0.01);
    Require(!common1.HasValue() && FirstCode(common1.Diagnostics()) == "KER-B003",
        "交差: 土台のままなら何も変わらないと言う: " + common1.FirstSummaryJa());
    // 引き切れば何も残らない。
    const auto cut1 = BuildBoolean(BooleanOperation::Difference, big, around, 0.01);
    Require(!cut1.HasValue() && FirstCode(cut1.Diagnostics()) == "KER-B004",
        "引き切れば断る: " + cut1.FirstSummaryJa());
}

#endif

KACHA_V2_TEST_MAIN("kernel_boolean_tests")
