// 押し出しの入力検査と予測(AT-EXT-001〜008、geometry-contract §8)。
//
// ここは OCCT を使わない。「押し出してよいか」と「どんな形になるはずか」を
// core が先に決める。OCCT が作ったものはこの予測と突き合わせる。
// V1 は OCCT が返した solid を無検査で採用したので、穴が多角形へ化けても気づかなかった。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/ExtrudeInput.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using kachakacha::v2::fabrication::AnalyticSurfaceInfo;
using kachakacha::v2::fabrication::AnalyticSurfaceKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Dot;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::modeling::AnalyzeExtrudeRequest;
using kachakacha::v2::modeling::CheckExtrudeResult;
using kachakacha::v2::modeling::ExtrudeBooleanMode;
using kachakacha::v2::modeling::ExtrudeDirectionMode;
using kachakacha::v2::modeling::ExtrudeExtentMode;
using kachakacha::v2::modeling::ExtrudeProfile;
using kachakacha::v2::modeling::ExtrudeRequest;
using kachakacha::v2::modeling::ExtrudeTargetKind;
using kachakacha::v2::modeling::StandardPlane;
using kachakacha::v2::modeling::StandardPlaneKind;
using kachakacha::v2::modeling::WorkPlaneFrame;
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

[[nodiscard]] ExtrudeProfile Rectangle(double x0, double y0, double x1, double y1,
    double z = 0.0)
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

[[nodiscard]] ExtrudeProfile Circle(Vector3 center, double radius)
{
    ExtrudeProfile profile;
    profile.closed = true;
    const auto made =
        CurveSegment::MakeCircle(center, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}, radius);
    Require(made.HasValue(), "円が作れること");
    profile.segments = {made.Value()};
    return profile;
}

[[nodiscard]] std::string FirstCode(
    const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

//! 40x20 を 30mm 押し出す、いちばん基本の入力(AT-EXT-001)。
[[nodiscard]] ExtrudeRequest BasicRequest()
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, 40, 20)};
    request.directionMode = ExtrudeDirectionMode::ProfileNormal;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 30.0;
    request.outputs.part = true;
    request.booleanMode = ExtrudeBooleanMode::NewPart;
    return request;
}

void RequireRefused(const ExtrudeRequest& request, const std::string& code,
    const std::string& why)
{
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(!analysis.HasValue(), why + ": 断ること");
    RequireEqual(FirstCode(analysis.Diagnostics()), code, why + ": 診断コード");
}

} // namespace

// =====================================================================
//  AT-EXT-001 基本押し出し
// =====================================================================

KACHA_V2_TEST(extrude_basic, 体積の予測が合う)
{
    const auto analysis = AnalyzeExtrudeRequest(BasicRequest(), Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireNear(analysis.Value().predictedVolumeMm3, 24000.0, 1.0e-6, "体積");
}

KACHA_V2_TEST(extrude_basic, 断面積の予測が合う)
{
    const auto analysis = AnalyzeExtrudeRequest(BasicRequest(), Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireNear(analysis.Value().predictedProfileAreaMm2, 800.0, 1.0e-9, "断面積");
}

KACHA_V2_TEST(extrude_basic, 面の数の予測が6になる)
{
    const auto analysis = AnalyzeExtrudeRequest(BasicRequest(), Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireEqual(std::to_string(analysis.Value().predictedFaceCount), "6", "面の数");
}

KACHA_V2_TEST(extrude_basic, 部品は1つできる)
{
    const auto analysis = AnalyzeExtrudeRequest(BasicRequest(), Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireEqual(std::to_string(analysis.Value().expectedPartCount), "1", "部品の数");
}

KACHA_V2_TEST(extrude_basic, 向きは輪郭の法線になる)
{
    const auto analysis = AnalyzeExtrudeRequest(BasicRequest(), Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireNear(std::abs(Dot(analysis.Value().direction, Vector3{0, 0, 1})), 1.0, 1.0e-12,
        "向き");
}

KACHA_V2_TEST(extrude_basic, 反転すると向きが逆になる)
{
    ExtrudeRequest request = BasicRequest();
    const auto forward = AnalyzeExtrudeRequest(request, Tolerance());
    request.reversed = true;
    const auto backward = AnalyzeExtrudeRequest(request, Tolerance());
    Require(forward.HasValue() && backward.HasValue(), "両方とも通ること");
    RequireNear(Dot(forward.Value().direction, backward.Value().direction), -1.0, 1.0e-12,
        "向きが逆");
    RequireNear(backward.Value().predictedVolumeMm3, 24000.0, 1.0e-6, "体積は同じ");
}

KACHA_V2_TEST(extrude_basic, 出来上がりを予測と突き合わせられる)
{
    const auto analysis = AnalyzeExtrudeRequest(BasicRequest(), Tolerance());
    Require(analysis.HasValue(), "通ること");
    const auto good = CheckExtrudeResult(analysis.Value(), 24000.0, 6, 1);
    Require(good.Ok(), "予測どおりなら合格");
    const auto badVolume = CheckExtrudeResult(analysis.Value(), 24500.0, 6, 1);
    Require(!badVolume.Ok(), "体積が違えば不合格");
    const auto badFaces = CheckExtrudeResult(analysis.Value(), 24000.0, 8, 1);
    Require(!badFaces.Ok(), "面の数が違えば不合格");
    const auto badParts = CheckExtrudeResult(analysis.Value(), 24000.0, 6, 2);
    Require(!badParts.Ok(), "部品の数が違えば不合格");
}

KACHA_V2_TEST(extrude_basic, 円の断面でも体積が合う)
{
    ExtrudeRequest request;
    request.profiles = {Circle({0, 0, 0}, 10.0)};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 25.0;
    request.outputs.part = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    // 円は円のまま扱う。多角形へ落として面積を出さない。
    RequireNear(analysis.Value().predictedVolumeMm3, kPi * 100.0 * 25.0, 1.0e-9, "体積");
    Require(analysis.Value().areaIsExact, "厳密に出せている");
}

// =====================================================================
//  AT-EXT-002 開いた輪郭
// =====================================================================

KACHA_V2_TEST(extrude_open, 開いた輪郭から部品は作れない)
{
    ExtrudeRequest request;
    ExtrudeProfile open;
    open.closed = false;
    open.segments = {
        Line({0, 0, 0}, {10, 0, 0}),
        Line({10, 0, 0}, {10, 10, 0}),
        Line({10, 10, 0}, {20, 10, 0}),
    };
    request.profiles = {open};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 5.0;
    request.outputs.part = true;
    RequireRefused(request, "EXT-002", "開いた輪郭から部品");
}

KACHA_V2_TEST(extrude_open, 開いた輪郭でもワイヤーなら作れる)
{
    ExtrudeRequest request;
    ExtrudeProfile open;
    open.closed = false;
    open.segments = {
        Line({0, 0, 0}, {10, 0, 0}),
        Line({10, 0, 0}, {10, 10, 0}),
        Line({10, 10, 0}, {20, 10, 0}),
    };
    request.profiles = {open};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 5.0;
    request.outputs.endProfileWire = true;
    request.outputs.sideBoundaryWires = true;
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireEqual(std::to_string(analysis.Value().expectedPartCount), "0", "部品は作らない");
}

// =====================================================================
//  AT-EXT-003 ワイヤーと部品を同時に
// =====================================================================

KACHA_V2_TEST(extrude_both, ワイヤーと部品を同時に選べる)
{
    ExtrudeRequest request = BasicRequest();
    request.outputs.endProfileWire = true;
    request.outputs.sideBoundaryWires = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireEqual(std::to_string(analysis.Value().expectedPartCount), "1", "部品は1つ");
    RequireNear(analysis.Value().endOffsetMm, 30.0, 1.0e-12, "先の位置");
}

KACHA_V2_TEST(extrude_both, 何も選ばなければ断る)
{
    ExtrudeRequest request = BasicRequest();
    request.outputs = {};
    RequireRefused(request, "EXT-010", "出力を選んでいない");
}

// =====================================================================
//  AT-EXT-004 穴付き輪郭
// =====================================================================

KACHA_V2_TEST(extrude_hole, 穴の分だけ体積が減る)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, 40, 20), Circle({20, 10, 0}, 5.0)};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 30.0;
    request.outputs.part = true;
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireNear(analysis.Value().predictedVolumeMm3,
        (800.0 - kPi * 25.0) * 30.0, 1.0e-9, "体積");
    Require(analysis.Value().areaIsExact, "厳密に出せている");
}

KACHA_V2_TEST(extrude_hole, 内側の輪郭が穴と分かる)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, 40, 20), Circle({20, 10, 0}, 5.0)};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 30.0;
    request.outputs.part = true;
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    std::size_t holes = 0;
    std::size_t outers = 0;
    for (const auto& loop : analysis.Value().loops) {
        if (loop.isHole) {
            ++holes;
        } else {
            ++outers;
            RequireEqual(std::to_string(loop.holes.size()), "1", "穴が結びついている");
        }
    }
    RequireEqual(std::to_string(holes), "1", "穴の数");
    RequireEqual(std::to_string(outers), "1", "外周の数");
    RequireEqual(std::to_string(analysis.Value().expectedPartCount), "1", "部品は1つ");
}

KACHA_V2_TEST(extrude_hole, 穴だけでは部品にならない)
{
    // 外周を1つ、その中に外周をもう1つ、さらにその中に穴 ── ではなく、
    // 外周が無い(すべて穴)入力は作れない。ここでは輪郭1つを穴扱いにはできないので、
    // 「外周が無い」状態を、輪郭を空にして確かめる。
    ExtrudeRequest request;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 10.0;
    request.outputs.part = true;
    RequireRefused(request, "EXT-010", "輪郭なし");
}

KACHA_V2_TEST(extrude_hole, 入れ子が3重でも外周と穴が交互になる)
{
    ExtrudeRequest request;
    request.profiles = {
        Rectangle(0, 0, 100, 100),
        Rectangle(20, 20, 80, 80),
        Rectangle(35, 35, 65, 65),
    };
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 10.0;
    request.outputs.part = true;
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    const auto& loops = analysis.Value().loops;
    Require(!loops[0].isHole, "一番外は外周");
    Require(loops[1].isHole, "2番目は穴");
    Require(!loops[2].isHole, "3番目はまた外周");
    RequireEqual(std::to_string(analysis.Value().expectedPartCount), "2", "部品は2つ");
}

// =====================================================================
//  AT-EXT-005 複数外周
// =====================================================================

KACHA_V2_TEST(extrude_multi, 離れた2つの外周から2部品ができる)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, 10, 10), Rectangle(50, 0, 60, 10)};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 4.0;
    request.outputs.part = true;
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireEqual(std::to_string(analysis.Value().expectedPartCount), "2", "部品の数");
    RequireNear(analysis.Value().predictedVolumeMm3, 2.0 * 100.0 * 4.0, 1.0e-9, "体積");
    RequireEqual(std::to_string(analysis.Value().predictedFaceCount), "12", "面の数");
}

KACHA_V2_TEST(extrude_multi, 離れた外周へ足すのは注意を出す)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, 10, 10), Rectangle(50, 0, 60, 10)};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 4.0;
    request.outputs.part = true;
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.booleanMode = ExtrudeBooleanMode::AddToPart;
    request.hasSelectedPart = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    bool warned = false;
    for (const auto& note : analysis.Value().notes) {
        if (note.code == "EXT-005") {
            warned = true;
        }
    }
    Require(warned, "離ればなれになるかもしれないと言う");
}

// =====================================================================
//  AT-EXT-006 終端方式
// =====================================================================

KACHA_V2_TEST(extrude_extent, 片側距離)
{
    const auto analysis = AnalyzeExtrudeRequest(BasicRequest(), Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireNear(analysis.Value().startOffsetMm, 0.0, 1.0e-12, "始まり");
    RequireNear(analysis.Value().endOffsetMm, 30.0, 1.0e-12, "終わり");
}

KACHA_V2_TEST(extrude_extent, 中央から両側)
{
    ExtrudeRequest request = BasicRequest();
    request.extent = ExtrudeExtentMode::SymmetricDistance;
    request.distanceMm = 30.0;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireNear(analysis.Value().startOffsetMm, -15.0, 1.0e-12, "始まり");
    RequireNear(analysis.Value().endOffsetMm, 15.0, 1.0e-12, "終わり");
    RequireNear(analysis.Value().predictedVolumeMm3, 24000.0, 1.0e-6, "体積は同じ");
}

KACHA_V2_TEST(extrude_extent, 正負を別々に指定)
{
    ExtrudeRequest request = BasicRequest();
    request.extent = ExtrudeExtentMode::TwoDistances;
    request.distanceMm = 20.0;
    request.secondDistanceMm = 10.0;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireNear(analysis.Value().startOffsetMm, -10.0, 1.0e-12, "始まり");
    RequireNear(analysis.Value().endOffsetMm, 20.0, 1.0e-12, "終わり");
    RequireNear(analysis.Value().predictedVolumeMm3, 800.0 * 30.0, 1.0e-6, "体積");
}

KACHA_V2_TEST(extrude_extent, 平面まで押す)
{
    ExtrudeRequest request = BasicRequest();
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::ToTarget;
    request.targetKind = ExtrudeTargetKind::Plane;
    WorkPlaneFrame target = StandardPlane(StandardPlaneKind::XY);
    target.origin = {0, 0, 45.0};
    request.targetPlane = target;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireNear(analysis.Value().minimumReachMm, 45.0, 1.0e-9, "最短");
    RequireNear(analysis.Value().maximumReachMm, 45.0, 1.0e-9, "最長");
    RequireNear(analysis.Value().predictedVolumeMm3, 800.0 * 45.0, 1.0e-6, "体積");
}

KACHA_V2_TEST(extrude_extent, 傾いた平面まで押すと長さに幅が出る)
{
    ExtrudeRequest request = BasicRequest();
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::ToTarget;
    request.targetKind = ExtrudeTargetKind::Plane;
    // z = 50 + x/2 の平面。
    WorkPlaneFrame target;
    const double scale = 1.0 / std::sqrt(1.25);
    target.origin = {0, 0, 50.0};
    target.normal = Vector3{-0.5, 0.0, 1.0} * scale;
    target.uAxis = {0, 1, 0};
    target.vAxis = kachakacha::v2::geometry::Cross(target.normal, target.uAxis);
    request.targetPlane = target;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireNear(analysis.Value().minimumReachMm, 50.0, 1.0e-6, "最短(x=0)");
    RequireNear(analysis.Value().maximumReachMm, 70.0, 1.0e-6, "最長(x=40)");
    bool warned = false;
    for (const auto& note : analysis.Value().notes) {
        if (note.code == "EXT-101") {
            warned = true;
        }
    }
    Require(warned, "長さが場所で変わると言う");
}

KACHA_V2_TEST(extrude_extent, 円筒まで押せる)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(-5, -5, 5, 5)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::ToTarget;
    request.targetKind = ExtrudeTargetKind::AnalyticSurface;
    AnalyticSurfaceInfo cylinder;
    cylinder.kind = AnalyticSurfaceKind::Cylinder;
    cylinder.origin = {0, 0, 0};
    cylinder.axis = {1, 0, 0};   // X軸まわりの円筒
    cylinder.reference = {0, 1, 0};
    cylinder.radiusMm = 50.0;
    request.targetSurface = cylinder;
    request.outputs.part = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    // y=0 の点は z=50 まで、y=±5 の点は z=sqrt(2500-25)=49.75 まで。
    RequireNear(analysis.Value().maximumReachMm, 50.0, 1.0e-6, "最長");
    RequireNear(analysis.Value().minimumReachMm, std::sqrt(2500.0 - 25.0), 1.0e-6, "最短");
}

KACHA_V2_TEST(extrude_extent, 球まで押せる)
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
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    // 輪郭は外周なので、いちばん軸に近い標本は辺の中点 (0, ±3)。
    RequireNear(analysis.Value().maximumReachMm, std::sqrt(1600.0 - 9.0), 1.0e-6,
        "最長(辺の中点)");
    RequireNear(analysis.Value().minimumReachMm, std::sqrt(1600.0 - 18.0), 1.0e-6,
        "最短(角)");
}

KACHA_V2_TEST(extrude_extent, 届かない先は部分的な形を作らずに断る)
{
    ExtrudeRequest request = BasicRequest();
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::ToTarget;
    request.targetKind = ExtrudeTargetKind::AnalyticSurface;
    AnalyticSurfaceInfo cylinder;
    cylinder.kind = AnalyticSurfaceKind::Cylinder;
    cylinder.origin = {0, 0, 0};
    cylinder.axis = {1, 0, 0};
    cylinder.reference = {0, 1, 0};
    cylinder.radiusMm = 10.0;   // 輪郭の y は 0〜20。半径10の外側へ出る点がある
    request.targetSurface = cylinder;
    RequireRefused(request, "EXT-003", "届かない先");
}

KACHA_V2_TEST(extrude_extent, 逆側にある平面へは届かない)
{
    ExtrudeRequest request = BasicRequest();
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::ToTarget;
    request.targetKind = ExtrudeTargetKind::Plane;
    WorkPlaneFrame target = StandardPlane(StandardPlaneKind::XY);
    target.origin = {0, 0, -20.0};
    request.targetPlane = target;
    RequireRefused(request, "EXT-003", "後ろにある平面");
}

KACHA_V2_TEST(extrude_extent, 押す先が選ばれていなければ断る)
{
    ExtrudeRequest request = BasicRequest();
    request.extent = ExtrudeExtentMode::ToTarget;
    RequireRefused(request, "EXT-010", "先が無い");
}

KACHA_V2_TEST(extrude_extent, 全部貫くのは引くときだけ)
{
    ExtrudeRequest request = BasicRequest();
    request.extent = ExtrudeExtentMode::ThroughAll;
    request.booleanMode = ExtrudeBooleanMode::NewPart;
    RequireRefused(request, "EXT-010", "新規部品で全部貫く");
}

KACHA_V2_TEST(extrude_extent, 距離0で部品は作れない)
{
    ExtrudeRequest request = BasicRequest();
    request.distanceMm = 0.0;
    RequireRefused(request, "EXT-006", "距離0で部品");
}

KACHA_V2_TEST(extrude_extent, 距離0のワイヤーは確認すれば作れる)
{
    ExtrudeRequest request = BasicRequest();
    request.outputs = {};
    request.outputs.endProfileWire = true;
    request.distanceMm = 0.0;
    RequireRefused(request, "EXT-006", "確認前");
    request.zeroDistanceConfirmed = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "確認後は通る");
}

// =====================================================================
//  AT-EXT-007 足す / 引く
// =====================================================================

KACHA_V2_TEST(extrude_boolean, 足す相手を選んでいなければ断る)
{
    ExtrudeRequest request = BasicRequest();
    request.booleanMode = ExtrudeBooleanMode::AddToPart;
    request.hasSelectedPart = false;
    RequireRefused(request, "EXT-010", "相手なしで足す");
}

KACHA_V2_TEST(extrude_boolean, 引く相手を選んでいなければ断る)
{
    ExtrudeRequest request = BasicRequest();
    request.booleanMode = ExtrudeBooleanMode::SubtractFromPart;
    request.hasSelectedPart = false;
    RequireRefused(request, "EXT-010", "相手なしで引く");
}

KACHA_V2_TEST(extrude_boolean, 相手を選べば足せる)
{
    ExtrudeRequest request = BasicRequest();
    request.booleanMode = ExtrudeBooleanMode::AddToPart;
    request.hasSelectedPart = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
}

KACHA_V2_TEST(extrude_boolean, 全部貫いて引ける)
{
    ExtrudeRequest request = BasicRequest();
    request.extent = ExtrudeExtentMode::ThroughAll;
    request.booleanMode = ExtrudeBooleanMode::SubtractFromPart;
    request.hasSelectedPart = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
}

// =====================================================================
//  向きの指定(§8.2)
// =====================================================================

KACHA_V2_TEST(extrude_direction, 世界座標の3軸を選べる)
{
    const std::vector<std::pair<ExtrudeDirectionMode, Vector3>> cases{
        {ExtrudeDirectionMode::WorldX, {1, 0, 0}},
        {ExtrudeDirectionMode::WorldY, {0, 1, 0}},
        {ExtrudeDirectionMode::WorldZ, {0, 0, 1}},
    };
    for (const auto& item : cases) {
        ExtrudeRequest request = BasicRequest();
        request.directionMode = item.first;
        request.outputs = {};
        request.outputs.endProfileWire = true;
        const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
        Require(analysis.HasValue(), "通ること");
        RequireNear((analysis.Value().direction - item.second).Length(), 0.0, 1.0e-12,
            "向き");
    }
}

KACHA_V2_TEST(extrude_direction, 自由な向きは正規化される)
{
    ExtrudeRequest request = BasicRequest();
    request.directionMode = ExtrudeDirectionMode::CustomXYZ;
    request.customDirection = {0.0, 3.0, 4.0};
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireNear(analysis.Value().direction.Length(), 1.0, 1.0e-12, "長さ1");
    RequireNear(analysis.Value().direction.y, 0.6, 1.0e-12, "y成分");
    RequireNear(analysis.Value().direction.z, 0.8, 1.0e-12, "z成分");
}

KACHA_V2_TEST(extrude_direction, 長さ0の向きは断る)
{
    ExtrudeRequest request = BasicRequest();
    request.directionMode = ExtrudeDirectionMode::CustomXYZ;
    request.customDirection = {0.0, 0.0, 0.0};
    RequireRefused(request, "EXT-007", "長さ0の向き");
}

KACHA_V2_TEST(extrude_direction, 輪郭と同じ平面の向きでは部品にならない)
{
    ExtrudeRequest request = BasicRequest();
    request.directionMode = ExtrudeDirectionMode::WorldX;
    RequireRefused(request, "EXT-007", "厚みが出ない向き");
}

KACHA_V2_TEST(extrude_direction, 斜めに押すと体積が減る)
{
    ExtrudeRequest request = BasicRequest();
    request.directionMode = ExtrudeDirectionMode::CustomXYZ;
    request.customDirection = {0.0, 1.0, 1.0};
    request.distanceMm = 30.0;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    // 法線方向の成分は 30 / sqrt(2)。
    RequireNear(analysis.Value().predictedVolumeMm3, 800.0 * 30.0 / std::sqrt(2.0),
        1.0e-6, "体積");
}

KACHA_V2_TEST(extrude_direction, 作業平面の法線を使える)
{
    ExtrudeRequest request = BasicRequest();
    request.directionMode = ExtrudeDirectionMode::WorkPlaneNormal;
    request.workPlane = StandardPlane(StandardPlaneKind::XY);
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireNear((analysis.Value().direction - Vector3{0, 0, 1}).Length(), 0.0, 1.0e-12,
        "向き");
}

KACHA_V2_TEST(extrude_direction, 壊れた作業平面は断る)
{
    ExtrudeRequest request = BasicRequest();
    request.directionMode = ExtrudeDirectionMode::WorkPlaneNormal;
    WorkPlaneFrame broken;
    broken.uAxis = {1, 0, 0};
    broken.vAxis = {1, 1, 0};
    broken.normal = {0, 0, 1};
    request.workPlane = broken;
    RequireRefused(request, "EXT-007", "壊れた作業平面");
}

// =====================================================================
//  AT-EXT-001 同一平面の判定(EXT-001)
// =====================================================================

KACHA_V2_TEST(extrude_planar, 同じ平面に無い輪郭は断る)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, 10, 10, 0.0), Rectangle(20, 0, 30, 10, 5.0)};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 3.0;
    request.outputs.part = true;
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    RequireRefused(request, "EXT-001", "高さの違う2つの輪郭");
}

KACHA_V2_TEST(extrude_planar, ねじれた輪郭は断る)
{
    ExtrudeRequest request;
    ExtrudeProfile twisted;
    twisted.closed = true;
    twisted.segments = {
        Line({0, 0, 0}, {10, 0, 0}),
        Line({10, 0, 0}, {10, 10, 5}),
        Line({10, 10, 5}, {0, 10, 0}),
        Line({0, 10, 0}, {0, 0, 0}),
    };
    request.profiles = {twisted};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 3.0;
    request.outputs.part = true;
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    RequireRefused(request, "EXT-001", "ねじれた輪郭");
}

KACHA_V2_TEST(extrude_planar, 傾いた平面の輪郭は通る)
{
    ExtrudeRequest request;
    ExtrudeProfile tilted;
    tilted.closed = true;
    tilted.segments = {
        Line({0, 0, 0}, {10, 0, 10}),
        Line({10, 0, 10}, {10, 20, 10}),
        Line({10, 20, 10}, {0, 20, 0}),
        Line({0, 20, 0}, {0, 0, 0}),
    };
    request.profiles = {tilted};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 5.0;
    request.outputs.part = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    const double area = 10.0 * std::sqrt(2.0) * 20.0;
    RequireNear(analysis.Value().predictedProfileAreaMm2, area, 1.0e-6, "断面積");
    RequireNear(analysis.Value().predictedVolumeMm3, area * 5.0, 1.0e-6, "体積");
}

// =====================================================================
//  壊れた入力
// =====================================================================

KACHA_V2_TEST(extrude_robust, 有限でない距離は断る)
{
    ExtrudeRequest request = BasicRequest();
    request.distanceMm = std::nan("");
    RequireRefused(request, "EXT-010", "NaNの距離");
}

KACHA_V2_TEST(extrude_robust, 有限でない座標は曲線の段階で止まる)
{
    // 押し出しへ届く前に、曲線を作る側が断る。だから押し出しは NaN を見ない。
    // ここを確かめておかないと、後で曲線側の検査を緩めたときに気づけない。
    const auto bad = CurveSegment::MakeLine({0, 0, 0}, {10, std::nan(""), 0});
    Require(!bad.HasValue(), "NaN の直線は作れない");
    const auto worse = CurveSegment::MakeLine({0, 0, 0},
        {std::numeric_limits<double>::infinity(), 0, 0});
    Require(!worse.HasValue(), "無限大の直線も作れない");
}

KACHA_V2_TEST(extrude_robust, 線の入っていない輪郭は断る)
{
    ExtrudeRequest request;
    request.profiles = {ExtrudeProfile{}};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 3.0;
    request.outputs.endProfileWire = true;
    RequireRefused(request, "EXT-010", "空の輪郭");
}

KACHA_V2_TEST(extrude_robust, どの入力でも例外を投げない)
{
    std::vector<ExtrudeRequest> broken;
    broken.push_back(ExtrudeRequest{});
    ExtrudeRequest a = BasicRequest();
    a.distanceMm = std::numeric_limits<double>::infinity();
    broken.push_back(a);
    ExtrudeRequest b = BasicRequest();
    b.extent = ExtrudeExtentMode::ToTarget;
    b.targetKind = ExtrudeTargetKind::AnalyticSurface;   // Unknown な面
    broken.push_back(b);
    ExtrudeRequest c = BasicRequest();
    c.customDirection = {std::nan(""), 0, 0};
    c.directionMode = ExtrudeDirectionMode::CustomXYZ;
    broken.push_back(c);
    for (const ExtrudeRequest& request : broken) {
        const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
        Require(!analysis.HasValue(), "断ること");
        Require(!analysis.Diagnostics().empty(), "診断があること");
        Require(!analysis.Diagnostics().front().summaryJa.empty(), "説明があること");
    }
}

KACHA_V2_TEST(extrude_robust, とても大きい形でも予測が合う)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, 5000, 3000)};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 2000.0;
    request.outputs.part = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireNear(analysis.Value().predictedVolumeMm3, 5000.0 * 3000.0 * 2000.0, 1.0e-3,
        "体積");
}

KACHA_V2_TEST(extrude_robust, とても小さい形でも予測が合う)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, 0.05, 0.02)};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 0.01;
    request.outputs.part = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireNear(analysis.Value().predictedVolumeMm3, 0.05 * 0.02 * 0.01, 1.0e-15, "体積");
}

KACHA_V2_TEST(extrude_robust, 同じ入力からは毎回同じ予測が出る)
{
    const ExtrudeRequest request = BasicRequest();
    double reference = 0.0;
    for (int attempt = 0; attempt < 10; ++attempt) {
        const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
        Require(analysis.HasValue(), "通ること");
        if (attempt == 0) {
            reference = analysis.Value().predictedVolumeMm3;
        } else {
            RequireNear(analysis.Value().predictedVolumeMm3, reference, 0.0, "毎回同じ");
        }
    }
}


// =====================================================================
//  断面積を厳密に出す(AT-EXT-004「円が多角形へ変わらない」の土台)
// =====================================================================

KACHA_V2_TEST(extrude_area, 半円を含む輪郭の面積が厳密に出る)
{
    // 幅40、高さ20の長方形の上に、半径20の半円を載せた形。
    const auto arc = CurveSegment::MakeCircularArc({20, 20, 0}, {0, 0, 1}, {1, 0, 0},
        20.0, 0.0, kPi);
    Require(arc.HasValue(), "半円が作れること");
    ExtrudeProfile profile;
    profile.closed = true;
    profile.segments = {
        Line({0, 0, 0}, {40, 0, 0}),
        Line({40, 0, 0}, {40, 20, 0}),
        arc.Value(),
        Line({0, 20, 0}, {0, 0, 0}),
    };
    ExtrudeRequest request;
    request.profiles = {profile};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 10.0;
    request.outputs.part = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    const double expected = 40.0 * 20.0 + 0.5 * kPi * 400.0;
    RequireNear(analysis.Value().predictedProfileAreaMm2, expected, 1.0e-9, "面積");
    Require(analysis.Value().areaIsExact, "厳密に出せている");
}

KACHA_V2_TEST(extrude_area, 角丸長方形の面積が厳密に出る)
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
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 3.0;
    request.outputs.part = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    const double expected = width * height - (4.0 - kPi) * radius * radius;
    RequireNear(analysis.Value().predictedProfileAreaMm2, expected, 1.0e-9, "面積");
}

KACHA_V2_TEST(extrude_area, 円弧の向きが逆でも面積が合う)
{
    // 時計回りに描いた円。面積の絶対値は同じでなければならない。
    const auto made = CurveSegment::MakeCircle({0, 0, 0}, {0, 0, -1}, {1, 0, 0}, 7.0);
    Require(made.HasValue(), "円が作れること");
    ExtrudeProfile profile;
    profile.closed = true;
    profile.segments = {made.Value()};
    ExtrudeRequest request;
    request.profiles = {profile};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 2.0;
    request.outputs.part = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    RequireNear(analysis.Value().predictedProfileAreaMm2, kPi * 49.0, 1.0e-9, "面積");
}

KACHA_V2_TEST(extrude_area, ベジェを含む輪郭は厳密でないと申告する)
{
    const auto curve = CurveSegment::MakeCubicBezier(
        {{40, 0, 0}, {40, 10, 0}, {10, 10, 0}, {0, 0, 0}});
    Require(curve.HasValue(), "ベジェが作れること");
    ExtrudeProfile profile;
    profile.closed = true;
    profile.segments = {Line({0, 0, 0}, {40, 0, 0}), curve.Value()};
    ExtrudeRequest request;
    request.profiles = {profile};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 5.0;
    request.outputs.part = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    Require(!analysis.Value().areaIsExact, "厳密でないと言う");
    Require(analysis.Value().predictedProfileAreaMm2 > 0.0, "面積は正");
}

KACHA_V2_TEST(extrude_area, 厳密なときは体積の突き合わせが厳しい)
{
    ExtrudeRequest request;
    request.profiles = {Circle({0, 0, 0}, 10.0)};
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 25.0;
    request.outputs.part = true;
    const auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "通ること");
    const double exact = kPi * 100.0 * 25.0;
    Require(CheckExtrudeResult(analysis.Value(), exact, 3, 1).volumeMatches,
        "厳密な値なら合格");
    // 円を32角形へ落とすと、面積は 0.6% ほど小さくなる。これは弾かなければならない。
    const double polygon = 0.5 * 32.0 * std::sin(2.0 * kPi / 32.0) * 100.0 * 25.0;
    Require(!CheckExtrudeResult(analysis.Value(), polygon, 3, 1).volumeMatches,
        "多角形へ化けた値は弾く");
}

KACHA_V2_TEST_MAIN("extrude_tests")
