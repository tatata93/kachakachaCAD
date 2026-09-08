// 押し出しをカーネルで実際に行う層の試験(AT-EXT-001〜007)。
//
// core が出した予測と、OCCT が作った実物を突き合わせる。
// 合わなければ BuildExtrude が KER-E002 で断る。ここではその両方を確かめる。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/kernel/OcctExtrude.h"
#include "kachakacha/kernel/OcctGuideSurface.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::fabrication::AnalyticSurfaceInfo;
using kachakacha::v2::fabrication::AnalyticSurfaceKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::kernel::BuildExtrude;
using kachakacha::v2::kernel::ClearShapeCache;
using kachakacha::v2::kernel::ExtrudeBuildResult;
using kachakacha::v2::modeling::AnalyzeExtrudeRequest;
using kachakacha::v2::modeling::ExtrudeBooleanMode;
using kachakacha::v2::modeling::ExtrudeDirectionMode;
using kachakacha::v2::modeling::ExtrudeExtentMode;
using kachakacha::v2::modeling::ExtrudeProfile;
using kachakacha::v2::modeling::ExtrudeRequest;
using kachakacha::v2::modeling::ExtrudeTargetKind;
using kachakacha::v2::modeling::KernelShapeHandle;
using kachakacha::v2::modeling::StandardPlane;
using kachakacha::v2::modeling::StandardPlaneKind;
using kachakacha::v2::modeling::WorkPlaneFrame;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[maybe_unused]] [[nodiscard]] GeometryTolerance Tolerance()
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

[[maybe_unused]] [[nodiscard]] ExtrudeProfile Rectangle(double x0, double y0, double x1,
    double y1, double z = 0.0)
{
    ExtrudeProfile profile;
    profile.closed = true;
    profile.segments = {
        Line({x0, y0, z}, {x1, y0, z}),
        Line({x1, y0, z}, {x1, y1, z}),
        Line({x1, y1, z}, {x0, y1, z}),
        Line({x0, y1, z}, {x0, y0, z}),
    };
    return profile;
}

[[maybe_unused]] [[nodiscard]] ExtrudeProfile Circle(Vector3 center, double radius)
{
    ExtrudeProfile profile;
    profile.closed = true;
    const auto made =
        CurveSegment::MakeCircle(center, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, radius);
    Require(made.HasValue(), "円が作れること");
    profile.segments = {made.Value()};
    return profile;
}

[[maybe_unused]] [[nodiscard]] std::string FirstCode(
    const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

[[maybe_unused]] [[nodiscard]] ExtrudeRequest BasicRequest()
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, 40, 20)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 30.0;
    request.outputs.part = true;
    return request;
}

[[maybe_unused]] [[nodiscard]] kachakacha::v2::base::Result<ExtrudeBuildResult> Build(
    const ExtrudeRequest& request, KernelShapeHandle target = {})
{
    const GeometryTolerance tolerance = Tolerance();
    auto analysis = AnalyzeExtrudeRequest(request, tolerance);
    Require(analysis.HasValue(), "入力検査が通ること");
    return BuildExtrude(request, analysis.Value(), tolerance, target);
}

} // namespace

#ifndef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_extrude_absent, カーネルが無い版は押し出さずに断る)
{
    const auto built = Build(BasicRequest());
    Require(!built.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), "KER-E005", "カーネル不在の診断コード");
}

#else

// =====================================================================
//  AT-EXT-001 基本押し出し
// =====================================================================

KACHA_V2_TEST(kernel_extrude, 体積が予測どおりになる)
{
    ClearShapeCache();
    const auto built = Build(BasicRequest());
    Require(built.HasValue(), "作れること");
    RequireNear(built.Value().totalVolumeMm3, 24000.0, 1.0e-6, "体積");
}

KACHA_V2_TEST(kernel_extrude, 面が6枚になる)
{
    const auto built = Build(BasicRequest());
    Require(built.HasValue(), "作れること");
    RequireEqual(std::to_string(built.Value().totalFaceCount), "6", "面の数");
}

KACHA_V2_TEST(kernel_extrude, 部品が1つできる)
{
    const auto built = Build(BasicRequest());
    Require(built.HasValue(), "作れること");
    RequireEqual(std::to_string(built.Value().parts.size()), "1", "部品の数");
    Require(built.Value().parts.front().handle.Valid(), "番号が有効");
}

KACHA_V2_TEST(kernel_extrude, 面に意味的なキーが付く)
{
    const auto built = Build(BasicRequest());
    Require(built.HasValue(), "作れること");
    const auto& keys = built.Value().parts.front().faceKeys;
    RequireEqual(std::to_string(keys.size()), "6", "キーの数");
    std::size_t caps = 0;
    std::size_t sides = 0;
    for (const std::string& key : keys) {
        if (key.rfind("extrude/cap/", 0) == 0) {
            ++caps;
        } else if (key.rfind("extrude/side/", 0) == 0) {
            ++sides;
        }
    }
    RequireEqual(std::to_string(caps), "2", "蓋の数");
    RequireEqual(std::to_string(sides), "4", "側面の数");
}

KACHA_V2_TEST(kernel_extrude, 中央から両側でも体積が同じ)
{
    ExtrudeRequest request = BasicRequest();
    request.extent = ExtrudeExtentMode::SymmetricDistance;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    RequireNear(built.Value().totalVolumeMm3, 24000.0, 1.0e-6, "体積");
}

KACHA_V2_TEST(kernel_extrude, 正負を別々に指定しても体積が合う)
{
    ExtrudeRequest request = BasicRequest();
    request.extent = ExtrudeExtentMode::TwoDistances;
    request.distanceMm = 20.0;
    request.secondDistanceMm = 10.0;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    RequireNear(built.Value().totalVolumeMm3, 800.0 * 30.0, 1.0e-6, "体積");
}

KACHA_V2_TEST(kernel_extrude, 斜めに押しても体積が合う)
{
    ExtrudeRequest request = BasicRequest();
    request.directionMode = ExtrudeDirectionMode::CustomXYZ;
    request.customDirection = {0.0, 1.0, 1.0};
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    RequireNear(built.Value().totalVolumeMm3, 800.0 * 30.0 / std::sqrt(2.0), 1.0e-6,
        "体積");
}

// =====================================================================
//  AT-EXT-004 穴付き輪郭。円が多角形へ化けていないことを体積で見る
// =====================================================================

KACHA_V2_TEST(kernel_extrude, 円柱の体積が厳密に合う)
{
    ExtrudeRequest request;
    request.profiles = {Circle({0, 0, 0}, 10.0)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 25.0;
    request.outputs.part = true;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    RequireNear(built.Value().totalVolumeMm3, kPi * 100.0 * 25.0, 1.0e-6, "体積");
    RequireEqual(std::to_string(built.Value().totalFaceCount), "3", "面の数");
}

KACHA_V2_TEST(kernel_extrude, 貫通穴の分だけ体積が減る)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, 40, 20), Circle({20, 10, 0}, 5.0)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 30.0;
    request.outputs.part = true;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    RequireNear(built.Value().totalVolumeMm3, (800.0 - kPi * 25.0) * 30.0, 1.0e-6,
        "体積");
    RequireEqual(std::to_string(built.Value().totalFaceCount), "7", "面の数");
}

KACHA_V2_TEST(kernel_extrude, 角丸の断面でも体積が合う)
{
    const double radius = 5.0;
    const double width = 40.0;
    const double height = 25.0;
    const auto arc = [&](Vector3 center, double startAngle) {
        const auto made = CurveSegment::MakeCircularArc(center, {0, 0, 1}, {1, 0, 0},
            radius, startAngle, kPi / 2.0);
        Require(made.HasValue(), "角の円弧が作れること");
        return made.Value();
    };
    ExtrudeProfile profile;
    profile.closed = true;
    profile.segments = {
        Line({radius, 0, 0}, {width - radius, 0, 0}),
        arc({width - radius, radius, 0}, -kPi / 2.0),
        Line({width, radius, 0}, {width, height - radius, 0}),
        arc({width - radius, height - radius, 0}, 0.0),
        Line({width - radius, height, 0}, {radius, height, 0}),
        arc({radius, height - radius, 0}, kPi / 2.0),
        Line({0, height - radius, 0}, {0, radius, 0}),
        arc({radius, radius, 0}, kPi),
    };
    ExtrudeRequest request;
    request.profiles = {profile};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 12.0;
    request.outputs.part = true;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    const double area = width * height - (4.0 - kPi) * radius * radius;
    RequireNear(built.Value().totalVolumeMm3, area * 12.0, 1.0e-6, "体積");
}

// =====================================================================
//  AT-EXT-005 複数外周
// =====================================================================

KACHA_V2_TEST(kernel_extrude, 離れた2つの外周から2部品ができる)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, 10, 10), Rectangle(50, 0, 60, 10)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 4.0;
    request.outputs.part = true;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    RequireEqual(std::to_string(built.Value().parts.size()), "2", "部品の数");
    RequireNear(built.Value().totalVolumeMm3, 2.0 * 100.0 * 4.0, 1.0e-6, "体積");
    for (const auto& part : built.Value().parts) {
        RequireNear(part.volumeMm3, 400.0, 1.0e-6, "1つあたりの体積");
    }
}

// =====================================================================
//  AT-EXT-006 終端方式
// =====================================================================

KACHA_V2_TEST(kernel_extrude, 平面まで押せる)
{
    ExtrudeRequest request = BasicRequest();
    request.extent = ExtrudeExtentMode::ToTarget;
    request.targetKind = ExtrudeTargetKind::Plane;
    WorkPlaneFrame target = StandardPlane(StandardPlaneKind::XY);
    target.origin = {0, 0, 45.0};
    request.targetPlane = target;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    RequireNear(built.Value().totalVolumeMm3, 800.0 * 45.0, 1.0e-4, "体積");
}

KACHA_V2_TEST(kernel_extrude, 傾いた平面まで押すと楔になる)
{
    ExtrudeRequest request = BasicRequest();
    request.extent = ExtrudeExtentMode::ToTarget;
    request.targetKind = ExtrudeTargetKind::Plane;
    WorkPlaneFrame target;
    const double scale = 1.0 / std::sqrt(1.25);
    target.origin = {0, 0, 50.0};
    target.normal = Vector3{-0.5, 0.0, 1.0} * scale;
    target.uAxis = {0, 1, 0};
    target.vAxis = kachakacha::v2::geometry::Cross(target.normal, target.uAxis);
    request.targetPlane = target;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    // 高さは x=0 で 50、x=40 で 70。平均 60。
    RequireNear(built.Value().totalVolumeMm3, 800.0 * 60.0, 1.0e-3, "体積");
}

KACHA_V2_TEST(kernel_extrude, 円筒まで押すと厳密に切れる)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(-5, -5, 5, 5)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::ToTarget;
    request.targetKind = ExtrudeTargetKind::AnalyticSurface;
    AnalyticSurfaceInfo cylinder;
    cylinder.kind = AnalyticSurfaceKind::Cylinder;
    cylinder.origin = {0, 0, 0};
    cylinder.axis = {1, 0, 0};
    cylinder.reference = {0, 1, 0};
    cylinder.radiusMm = 50.0;
    request.targetSurface = cylinder;
    request.outputs.part = true;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    // 高さ h(y) = sqrt(2500 - y^2)。x 方向 10mm、y は -5..5 で積分する。
    // ∫ sqrt(2500-y^2) dy = [y/2*sqrt(2500-y^2) + 1250*asin(y/50)]
    const auto integral = [](double y) {
        return 0.5 * y * std::sqrt(2500.0 - y * y) + 1250.0 * std::asin(y / 50.0);
    };
    const double expected = 10.0 * (integral(5.0) - integral(-5.0));
    RequireNear(built.Value().totalVolumeMm3, expected, 1.0e-3, "体積");
    // 曲面のまま切れていれば、上面は平面ではない。
    Require(built.Value().totalFaceCount == 6, "面は6枚");
}

KACHA_V2_TEST(kernel_extrude, 球まで押せる)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(-3, -3, 3, 3)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::ToTarget;
    request.targetKind = ExtrudeTargetKind::AnalyticSurface;
    AnalyticSurfaceInfo sphere;
    sphere.kind = AnalyticSurfaceKind::Sphere;
    sphere.origin = {0, 0, 0};
    sphere.radiusMm = 40.0;
    request.targetSurface = sphere;
    request.outputs.part = true;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    Require(built.Value().totalVolumeMm3 > 36.0 * 39.0, "体積は下限より大きい");
    Require(built.Value().totalVolumeMm3 < 36.0 * 40.0, "体積は上限より小さい");
}

KACHA_V2_TEST(kernel_extrude, トーラスまでは厳密に切れないので断る)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(-3, -3, 3, 3)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::ToTarget;
    request.targetKind = ExtrudeTargetKind::AnalyticSurface;
    AnalyticSurfaceInfo torus;
    torus.kind = AnalyticSurfaceKind::Torus;
    torus.origin = {0, 0, 100};
    torus.axis = {0, 0, 1};
    torus.reference = {1, 0, 0};
    torus.radiusMm = 50.0;
    torus.secondaryRadiusMm = 10.0;
    request.targetSurface = torus;
    request.outputs.part = true;
    // core は到達判定でトーラスを扱えないので、ここで断る。
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(!analysis.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(analysis.Diagnostics()), "EXT-003", "届かないと言う");
}

// =====================================================================
//  AT-EXT-007 足す / 引く
// =====================================================================

namespace {

[[nodiscard]] KernelShapeHandle MakeBlock(double x0, double y0, double x1, double y1,
    double height)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(x0, y0, x1, y1)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = height;
    request.outputs.part = true;
    const auto built = Build(request);
    Require(built.HasValue(), "元の部品が作れること");
    return built.Value().parts.front().handle;
}

} // namespace

KACHA_V2_TEST(kernel_extrude, 重なる形を足すと体積が和より小さくなる)
{
    ClearShapeCache();
    const KernelShapeHandle block = MakeBlock(0, 0, 40, 20, 30.0);
    ExtrudeRequest request;
    request.profiles = {Rectangle(20, 0, 60, 20)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 30.0;
    request.outputs.part = true;
    request.booleanMode = ExtrudeBooleanMode::AddToPart;
    request.hasSelectedPart = true;
    const auto built = Build(request, block);
    Require(built.HasValue(), "足せること");
    RequireEqual(std::to_string(built.Value().parts.size()), "1", "1つにまとまる");
    // 0..60 x 0..20 x 30。
    RequireNear(built.Value().totalVolumeMm3, 60.0 * 20.0 * 30.0, 1.0e-4, "体積");
}

KACHA_V2_TEST(kernel_extrude, 離れた形を足すのは断る)
{
    ClearShapeCache();
    const KernelShapeHandle block = MakeBlock(0, 0, 10, 10, 5.0);
    ExtrudeRequest request;
    request.profiles = {Rectangle(50, 0, 60, 10)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 5.0;
    request.outputs.part = true;
    request.booleanMode = ExtrudeBooleanMode::AddToPart;
    request.hasSelectedPart = true;
    const auto built = Build(request, block);
    Require(!built.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), "EXT-005", "非連結の診断コード");
}

KACHA_V2_TEST(kernel_extrude, 穴を引ける)
{
    ClearShapeCache();
    const KernelShapeHandle block = MakeBlock(0, 0, 40, 20, 30.0);
    ExtrudeRequest request;
    request.profiles = {Circle({20, 10, -5}, 5.0)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::ThroughAll;
    request.outputs.part = true;
    request.booleanMode = ExtrudeBooleanMode::SubtractFromPart;
    request.hasSelectedPart = true;
    const auto built = Build(request, block);
    Require(built.HasValue(), "引けること");
    RequireNear(built.Value().totalVolumeMm3, 24000.0 - kPi * 25.0 * 30.0, 1.0e-4,
        "体積");
}

KACHA_V2_TEST(kernel_extrude, 引いて2つに分かれると個数を知らせる)
{
    ClearShapeCache();
    const KernelShapeHandle block = MakeBlock(0, 0, 40, 20, 10.0);
    ExtrudeRequest request;
    request.profiles = {Rectangle(15, -5, 25, 25)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::ThroughAll;
    request.outputs.part = true;
    request.booleanMode = ExtrudeBooleanMode::SubtractFromPart;
    request.hasSelectedPart = true;
    const auto built = Build(request, block);
    Require(built.HasValue(), "引けること");
    RequireEqual(std::to_string(built.Value().parts.size()), "2", "2つに分かれる");
    bool told = false;
    for (const auto& note : built.Diagnostics()) {
        if (note.code == "EXT-102") {
            told = true;
        }
    }
    Require(told, "個数を知らせる");
}

KACHA_V2_TEST(kernel_extrude, 引く相手が無ければ断る)
{
    ExtrudeRequest request = BasicRequest();
    request.booleanMode = ExtrudeBooleanMode::SubtractFromPart;
    request.hasSelectedPart = true;
    const auto built = Build(request, KernelShapeHandle{987654321});
    Require(!built.HasValue(), "作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), "KER-E004", "相手が無いときの診断コード");
}

// =====================================================================
//  ワイヤー出力
// =====================================================================

KACHA_V2_TEST(kernel_extrude, 押し出し先の輪郭ワイヤーが取れる)
{
    ExtrudeRequest request = BasicRequest();
    request.outputs.endProfileWire = true;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    RequireEqual(std::to_string(built.Value().endProfileWires.size()), "1", "1本");
    RequireEqual(std::to_string(built.Value().endProfileWires.front().size()), "4",
        "4本の線");
    for (const CurveSegment& segment : built.Value().endProfileWires.front()) {
        RequireNear(segment.StartPoint().z, 30.0, 1.0e-9, "押し出し先の高さ");
    }
}

KACHA_V2_TEST(kernel_extrude, 円の輪郭ワイヤーは円のまま出る)
{
    ExtrudeRequest request;
    request.profiles = {Circle({0, 0, 0}, 10.0)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 25.0;
    request.outputs.endProfileWire = true;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    RequireEqual(std::to_string(built.Value().endProfileWires.front().size()), "1",
        "1本の線");
    Require(built.Value().endProfileWires.front().front().Kind()
            == kachakacha::v2::geometry::CurveKind::Circle,
        "円のまま");
}

KACHA_V2_TEST(kernel_extrude, 側面の境界ワイヤーが取れる)
{
    ExtrudeRequest request = BasicRequest();
    request.outputs.sideBoundaryWires = true;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    RequireEqual(std::to_string(built.Value().sideBoundaryWires.size()), "4",
        "側面は4枚");
}

KACHA_V2_TEST(kernel_extrude, ワイヤーと部品の点が許容差内で一致する)
{
    // AT-EXT-003 の核。同じ押し出しから出したワイヤーと部品の境界が、
    // 本当に同じ場所にあるかを数で見る。「だいたい同じに見える」では受け入れない。
    // ここがずれると、ワイヤーを型紙に、部品を立体に使ったときに寸法が食い違う。
    ExtrudeRequest request = BasicRequest();
    request.outputs.endProfileWire = true;
    request.outputs.sideBoundaryWires = true;
    request.outputs.part = true;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    Require(!built.Value().parts.empty(), "部品が出ること");
    const GeometryTolerance tolerance;
    const KernelShapeHandle part = built.Value().parts.front().handle;

    std::size_t checked = 0;
    const auto checkWire = [&](const std::vector<CurveSegment>& wire,
                               const char* what) {
        for (const CurveSegment& segment : wire) {
            // 両端と中間を見る。曲線は点列へ落として調べる(検査用の点列である)。
            for (const auto& sample :
                kachakacha::v2::geometry::SampleCurve(segment, tolerance.modelLinearMm)) {
                const auto distance =
                    kachakacha::v2::kernel::DistanceToShapeSurface(part, sample.position);
                Require(distance.HasValue(), std::string(what) + " の距離が測れること");
                Require(distance.Value() <= tolerance.modelLinearMm,
                    std::string(what) + " の点が部品の表面に載っていること: "
                        + std::to_string(distance.Value()) + " mm");
                ++checked;
            }
        }
    };
    for (const auto& wire : built.Value().endProfileWires) {
        checkWire(wire, "押し出し先の輪郭");
    }
    for (const auto& wire : built.Value().sideBoundaryWires) {
        checkWire(wire, "側面の境界");
    }
    Require(checked >= 16, "十分な数の点を見たこと: " + std::to_string(checked));
}

KACHA_V2_TEST(kernel_extrude, 曲がった輪郭でもワイヤーと部品が一致する)
{
    // 直線だけだと、たまたま合っているだけかもしれない。円でも見る。
    ExtrudeRequest request;
    request.profiles = {Circle({0, 0, 0}, 10.0)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 25.0;
    request.outputs.endProfileWire = true;
    request.outputs.part = true;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    Require(!built.Value().parts.empty(), "部品が出ること");
    const GeometryTolerance tolerance;
    const KernelShapeHandle part = built.Value().parts.front().handle;
    std::size_t checked = 0;
    for (const auto& wire : built.Value().endProfileWires) {
        for (const CurveSegment& segment : wire) {
            for (const auto& sample :
                kachakacha::v2::geometry::SampleCurve(segment, tolerance.modelLinearMm)) {
                const auto distance =
                    kachakacha::v2::kernel::DistanceToShapeSurface(part, sample.position);
                Require(distance.HasValue(), "距離が測れること");
                Require(distance.Value() <= tolerance.modelLinearMm,
                    "円の点が部品の表面に載っていること: "
                        + std::to_string(distance.Value()) + " mm");
                ++checked;
            }
        }
    }
    Require(checked >= 8, "十分な数の点を見たこと: " + std::to_string(checked));
}

KACHA_V2_TEST(kernel_extrude, 表にない形の距離は測らずに断る)
{
    const auto refused = kachakacha::v2::kernel::DistanceToShapeSurface(
        KernelShapeHandle{}, Vector3{0, 0, 0});
    Require(!refused.HasValue(), "断ること");
}

// =====================================================================
//  予測との突き合わせ
// =====================================================================

KACHA_V2_TEST(kernel_extrude, 同じ入力からは毎回同じ体積になる)
{
    ClearShapeCache();
    const ExtrudeRequest request = BasicRequest();
    double reference = 0.0;
    for (int attempt = 0; attempt < 5; ++attempt) {
        const auto built = Build(request);
        Require(built.HasValue(), "作れること");
        if (attempt == 0) {
            reference = built.Value().totalVolumeMm3;
        } else {
            RequireNear(built.Value().totalVolumeMm3, reference, 1.0e-12, "毎回同じ");
        }
    }
    ClearShapeCache();
}

KACHA_V2_TEST(kernel_extrude, とても小さい形でも作れる)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, 0.5, 0.2)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 0.1;
    request.outputs.part = true;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    RequireNear(built.Value().totalVolumeMm3, 0.5 * 0.2 * 0.1, 1.0e-12, "体積");
}

KACHA_V2_TEST(kernel_extrude, とても大きい形でも作れる)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, 5000, 3000)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 2000.0;
    request.outputs.part = true;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    RequireNear(built.Value().totalVolumeMm3 / 1.0e9, 5000.0 * 3000.0 * 2000.0 / 1.0e9,
        1.0e-6, "体積");
    ClearShapeCache();
}

KACHA_V2_TEST(kernel_extrude, 原点から遠い場所でも作れる)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(100000, 100000, 100040, 100020)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 30.0;
    request.outputs.part = true;
    const auto built = Build(request);
    Require(built.HasValue(), "作れること");
    RequireNear(built.Value().totalVolumeMm3, 24000.0, 1.0e-3, "体積");
    ClearShapeCache();
}

#endif // KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST_MAIN("kernel_extrude_tests")
