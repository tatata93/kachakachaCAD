// 部品を動かす・回す・鏡に写す(P-18、kernel/OcctTransform)をカーネルで実際に行う層の試験。
//
// 剛体の変換なので体積は変わらない。形の外接箱が、掛けた変換どおりに動くことを見る。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctExtrude.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/kernel/OcctTransform.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::kernel::BuildExtrude;
using kachakacha::v2::kernel::ShapeTransform;
using kachakacha::v2::kernel::ShapeTransformKind;
using kachakacha::v2::kernel::TransformShape;
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

//! 箱(xy の矩形を z=0 から高さぶん押し出す)。
[[maybe_unused]] [[nodiscard]] KernelShapeHandle Box(double x0, double y0, double x1, double y1,
    double height)
{
    ExtrudeProfile profile;
    profile.closed = true;
    profile.segments = {Line({x0, y0, 0}, {x1, y0, 0}), Line({x1, y0, 0}, {x1, y1, 0}),
        Line({x1, y1, 0}, {x0, y1, 0}), Line({x0, y1, 0}, {x0, y0, 0})};
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

[[maybe_unused]] void RequireBounds(KernelShapeHandle handle, Vector3 minimum, Vector3 maximum,
    const std::string& what)
{
    const auto mesh = kachakacha::v2::kernel::BuildShapeMesh(handle);
    Require(mesh.HasValue(), what + ": 形を三角形にできる");
    const auto& m = mesh.Value();
    RequireNear(m.minimum.x, minimum.x, 1.0e-4, what + ": 最小 x");
    RequireNear(m.minimum.y, minimum.y, 1.0e-4, what + ": 最小 y");
    RequireNear(m.minimum.z, minimum.z, 1.0e-4, what + ": 最小 z");
    RequireNear(m.maximum.x, maximum.x, 1.0e-4, what + ": 最大 x");
    RequireNear(m.maximum.y, maximum.y, 1.0e-4, what + ": 最大 y");
    RequireNear(m.maximum.z, maximum.z, 1.0e-4, what + ": 最大 z");
}

} // namespace

KACHA_V2_TEST(kernel_place, 読めない動かし方は形を見る前に断る)
{
    ShapeTransform zero;
    zero.kind = ShapeTransformKind::Translate;
    const auto moved = TransformShape({}, zero);
    Require(!moved.HasValue(), "作れたことにしない");
    ShapeTransform noAngle;
    noAngle.kind = ShapeTransformKind::Rotate;
    noAngle.vector = {0, 0, 1};
    const auto turned = TransformShape({}, noAngle);
    Require(!turned.HasValue(), "角度 0° は作れたことにしない");
}

#ifndef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_place_absent, カーネルが無い版は動かさずに断る)
{
    ShapeTransform move;
    move.vector = {10, 0, 0};
    const auto built = TransformShape({}, move);
    Require(!built.HasValue() && !built.Diagnostics().empty()
            && built.Diagnostics().front().code == "KER-P004",
        "カーネル不在の診断コード");
}

#else

KACHA_V2_TEST(kernel_place, 動かすと体積はそのままで外接箱が動いた分だけずれる)
{
    const auto box = Box(0, 0, 40, 20, 30);
    Require(box.Valid(), "箱が作れること");
    ShapeTransform move;
    move.kind = ShapeTransformKind::Translate;
    move.vector = {10, -5, 7};
    const auto moved = TransformShape(box, move);
    Require(moved.HasValue(), "動かせる: " + moved.FirstSummaryJa());
    RequireNear(moved.Value().volumeMm3, 24000.0, 1.0e-3, "体積はそのまま");
    Require(moved.Value().handle.value != box.value, "別の形になる(元は残る)");
    RequireBounds(moved.Value().handle, {10, -5, 7}, {50, 15, 37}, "移動");
    RequireBounds(box, {0, 0, 0}, {40, 20, 30}, "元の箱は動かない");
}

KACHA_V2_TEST(kernel_place, 回すと鏡に写すと外接箱が変換どおりになり体積は正のまま)
{
    const auto box = Box(0, 0, 40, 20, 30);
    Require(box.Valid(), "箱が作れること");
    // z 軸まわりに 90°(原点を通る): x 0..40, y 0..20 → x -20..0, y 0..40。
    ShapeTransform turn;
    turn.kind = ShapeTransformKind::Rotate;
    turn.vector = {0, 0, 1};
    turn.angleRad = kPi / 2.0;
    const auto turned = TransformShape(box, turn);
    Require(turned.HasValue(), "回せる: " + turned.FirstSummaryJa());
    RequireNear(turned.Value().volumeMm3, 24000.0, 1.0e-3, "回しても体積はそのまま");
    RequireBounds(turned.Value().handle, {-20, 0, 0}, {0, 40, 30}, "回転");
    // x = -5 の面に写す: x 0..40 → x -50..-10。体積は正(裏返らない)。
    ShapeTransform mirror;
    mirror.kind = ShapeTransformKind::Mirror;
    mirror.vector = {1, 0, 0};
    mirror.point = {-5, 0, 0};
    const auto mirrored = TransformShape(box, mirror);
    Require(mirrored.HasValue(), "鏡に写せる: " + mirrored.FirstSummaryJa());
    RequireNear(mirrored.Value().volumeMm3, 24000.0, 1.0e-3, "鏡に写しても体積はそのまま");
    RequireBounds(mirrored.Value().handle, {-50, 0, 0}, {-10, 20, 30}, "鏡映");
}

#endif

KACHA_V2_TEST_MAIN("kernel_place_tests")
