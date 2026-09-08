// 作業平面の11方式(AT-WPL-001)と、作れない条件(AT-WPL-002)。
//
// V1 には標準面とオフセットしか無く、実物の車体を描くときに
// 「この辺を通り、この面に接する平面」が置けなかった。
// ここでは11方式すべてを数値で確かめる。原点・基底・法線・距離・角度を見る。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::fabrication::AnalyticSurfaceInfo;
using kachakacha::v2::fabrication::AnalyticSurfaceKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Cross;
using kachakacha::v2::geometry::Dot;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::modeling::BuildWorkPlane;
using kachakacha::v2::modeling::IsOrthonormalRightHanded;
using kachakacha::v2::modeling::StandardPlane;
using kachakacha::v2::modeling::StandardPlaneKind;
using kachakacha::v2::modeling::WorkPlaneFrame;
using kachakacha::v2::modeling::WorkPlaneMethod;
using kachakacha::v2::modeling::WorkPlaneRequest;
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

[[nodiscard]] CurveSegment Arc(Vector3 center, Vector3 normal, Vector3 reference,
    double radius, double start, double sweep)
{
    const auto made =
        CurveSegment::MakeCircularArc(center, normal, reference, radius, start, sweep);
    Require(made.HasValue(), "円弧が作れること");
    return made.Value();
}

[[nodiscard]] std::string FirstCode(
    const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

void RequireVector(const Vector3& actual, const Vector3& expected, double tolerance,
    const std::string& why)
{
    RequireNear((actual - expected).Length(), 0.0, tolerance, why);
}

//! 作れた平面は必ず右手系の正規直交でなければならない。
void RequireGoodFrame(const WorkPlaneFrame& frame, const std::string& why)
{
    Require(IsOrthonormalRightHanded(frame, 1.0e-9), why + ": 右手系の正規直交");
    Require(frame.origin.IsFinite(), why + ": 原点が有限");
}

[[nodiscard]] WorkPlaneFrame BuildOrFail(const WorkPlaneRequest& request,
    const std::string& why)
{
    const auto built = BuildWorkPlane(request, Tolerance());
    Require(built.HasValue(), why + ": 作れること");
    RequireGoodFrame(built.Value(), why);
    return built.Value();
}

void RequireRefused(const WorkPlaneRequest& request, const std::string& code,
    const std::string& why)
{
    const auto built = BuildWorkPlane(request, Tolerance());
    Require(!built.HasValue(), why + ": 作れたことにしない");
    RequireEqual(FirstCode(built.Diagnostics()), code, why + ": 診断コード");
}

//! 半径 r、軸が +Z、原点が o の円筒。
[[nodiscard]] AnalyticSurfaceInfo Cylinder(Vector3 origin, double radius)
{
    AnalyticSurfaceInfo info;
    info.kind = AnalyticSurfaceKind::Cylinder;
    info.origin = origin;
    info.axis = {0.0, 0.0, 1.0};
    info.reference = {1.0, 0.0, 0.0};
    info.radiusMm = radius;
    return info;
}

} // namespace

// =====================================================================
//  1. 標準面
// =====================================================================

KACHA_V2_TEST(workplane_standard, XY面の基底が正しい)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::Standard;
    request.standard = StandardPlaneKind::XY;
    const WorkPlaneFrame frame = BuildOrFail(request, "XY面");
    RequireVector(frame.origin, {0, 0, 0}, 1.0e-12, "原点");
    RequireVector(frame.uAxis, {1, 0, 0}, 1.0e-12, "u軸");
    RequireVector(frame.vAxis, {0, 1, 0}, 1.0e-12, "v軸");
    RequireVector(frame.normal, {0, 0, 1}, 1.0e-12, "法線");
}

KACHA_V2_TEST(workplane_standard, YZ面の基底が正しい)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::Standard;
    request.standard = StandardPlaneKind::YZ;
    const WorkPlaneFrame frame = BuildOrFail(request, "YZ面");
    RequireVector(frame.uAxis, {0, 1, 0}, 1.0e-12, "u軸");
    RequireVector(frame.vAxis, {0, 0, 1}, 1.0e-12, "v軸");
    RequireVector(frame.normal, {1, 0, 0}, 1.0e-12, "法線");
}

KACHA_V2_TEST(workplane_standard, ZX面の基底が正しい)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::Standard;
    request.standard = StandardPlaneKind::ZX;
    const WorkPlaneFrame frame = BuildOrFail(request, "ZX面");
    RequireVector(frame.uAxis, {0, 0, 1}, 1.0e-12, "u軸");
    RequireVector(frame.vAxis, {1, 0, 0}, 1.0e-12, "v軸");
    RequireVector(frame.normal, {0, 1, 0}, 1.0e-12, "法線");
}

KACHA_V2_TEST(workplane_standard, 3つの標準面はどれも右手系)
{
    for (const StandardPlaneKind kind :
        {StandardPlaneKind::XY, StandardPlaneKind::YZ, StandardPlaneKind::ZX}) {
        RequireGoodFrame(StandardPlane(kind), "標準面");
    }
}

// =====================================================================
//  2. 平面から離す
// =====================================================================

KACHA_V2_TEST(workplane_offset, 指定した距離だけ法線方向へ動く)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::OffsetFromPlane;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    request.offsetMm = 12.5;
    const WorkPlaneFrame frame = BuildOrFail(request, "オフセット");
    RequireVector(frame.origin, {0, 0, 12.5}, 1.0e-12, "原点");
    RequireVector(frame.normal, {0, 0, 1}, 1.0e-12, "法線は変わらない");
}

KACHA_V2_TEST(workplane_offset, 負の距離は逆側へ動く)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::OffsetFromPlane;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    request.offsetMm = -4.0;
    const WorkPlaneFrame frame = BuildOrFail(request, "逆向きオフセット");
    RequireNear(frame.origin.z, -4.0, 1.0e-12, "原点");
}

KACHA_V2_TEST(workplane_offset, 傾いた面からでも法線方向へ動く)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::OffsetFromPlane;
    WorkPlaneFrame tilted;
    const double inverseRoot2 = 1.0 / std::sqrt(2.0);
    tilted.origin = {1, 2, 3};
    tilted.uAxis = {0, 1, 0};
    tilted.vAxis = {-inverseRoot2, 0, inverseRoot2};
    tilted.normal = Cross(tilted.uAxis, tilted.vAxis);
    request.referencePlane = tilted;
    request.offsetMm = 10.0;
    const WorkPlaneFrame frame = BuildOrFail(request, "傾いた面のオフセット");
    RequireNear((frame.origin - tilted.origin).Length(), 10.0, 1.0e-9, "移動量");
    RequireNear(Dot(frame.origin - tilted.origin, tilted.normal), 10.0, 1.0e-9,
        "法線方向の成分");
}

KACHA_V2_TEST(workplane_offset, 距離が数でなければ断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::OffsetFromPlane;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    request.offsetMm = std::nan("");
    RequireRefused(request, "GEO-P001", "NaNの距離");
}

KACHA_V2_TEST(workplane_offset, 元の平面が正規直交でなければ断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::OffsetFromPlane;
    WorkPlaneFrame broken;
    broken.uAxis = {1, 0, 0};
    broken.vAxis = {1, 1, 0};   // 直交していない
    broken.normal = {0, 0, 1};
    request.referencePlane = broken;
    request.offsetMm = 1.0;
    RequireRefused(request, "GEO-P001", "壊れた元の平面");
}

// =====================================================================
//  3. 点を通り平行
// =====================================================================

KACHA_V2_TEST(workplane_parallel, 指定した点を通る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThroughPointParallel;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    request.points = {{3.0, -7.0, 22.0}};
    const WorkPlaneFrame frame = BuildOrFail(request, "点を通り平行");
    RequireVector(frame.origin, {3.0, -7.0, 22.0}, 1.0e-12, "原点");
    RequireVector(frame.normal, {0, 0, 1}, 1.0e-12, "法線は同じ");
    RequireNear(frame.SignedDistance({3.0, -7.0, 22.0}), 0.0, 1.0e-12, "点は面上");
}

KACHA_V2_TEST(workplane_parallel, 元の面との距離が点のズレと一致する)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThroughPointParallel;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    request.points = {{3.0, -7.0, 22.0}};
    const WorkPlaneFrame frame = BuildOrFail(request, "点を通り平行");
    RequireNear(request.referencePlane.SignedDistance(frame.origin), 22.0, 1.0e-12,
        "元の面からの距離");
}

KACHA_V2_TEST(workplane_parallel, 点が無ければ断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThroughPointParallel;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    RequireRefused(request, "GEO-P001", "点なし");
}

// =====================================================================
//  4. 2面の中間
// =====================================================================

KACHA_V2_TEST(workplane_middle, 平行な2面のちょうど中間に来る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::MidBetweenPlanes;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    WorkPlaneFrame second = StandardPlane(StandardPlaneKind::XY);
    second.origin = {0, 0, 30.0};
    request.secondPlane = second;
    const WorkPlaneFrame frame = BuildOrFail(request, "2面の中間");
    RequireNear(frame.origin.z, 15.0, 1.0e-12, "中間の高さ");
    RequireNear(std::abs(Dot(frame.normal, Vector3{0, 0, 1})), 1.0, 1.0e-12, "法線");
}

KACHA_V2_TEST(workplane_middle, 法線が逆向きの2面でも中間が出る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::MidBetweenPlanes;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    WorkPlaneFrame second;
    second.origin = {0, 0, 20.0};
    second.uAxis = {0, 1, 0};
    second.vAxis = {1, 0, 0};
    second.normal = {0, 0, -1};
    Require(IsOrthonormalRightHanded(second, 1.0e-9), "相手の面が右手系");
    request.secondPlane = second;
    const WorkPlaneFrame frame = BuildOrFail(request, "逆向きの2面");
    RequireNear(frame.origin.z, 10.0, 1.0e-12, "中間の高さ");
}

KACHA_V2_TEST(workplane_middle, 両方の面から等しい距離になる)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::MidBetweenPlanes;
    WorkPlaneFrame first = StandardPlane(StandardPlaneKind::YZ);
    first.origin = {-8.0, 0, 0};
    WorkPlaneFrame second = StandardPlane(StandardPlaneKind::YZ);
    second.origin = {14.0, 0, 0};
    request.referencePlane = first;
    request.secondPlane = second;
    const WorkPlaneFrame frame = BuildOrFail(request, "YZの中間");
    RequireNear(std::abs(first.SignedDistance(frame.origin)), 11.0, 1.0e-12,
        "1つめからの距離");
    RequireNear(std::abs(second.SignedDistance(frame.origin)), 11.0, 1.0e-12,
        "2つめからの距離");
}

KACHA_V2_TEST(workplane_middle, 平行でない2面は断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::MidBetweenPlanes;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    request.secondPlane = StandardPlane(StandardPlaneKind::YZ);
    RequireRefused(request, "GEO-P003", "直交する2面");
}

KACHA_V2_TEST(workplane_middle, わずかに傾いた2面も断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::MidBetweenPlanes;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    const double angle = 0.5 * kPi / 180.0;   // 0.5度
    WorkPlaneFrame second;
    second.origin = {0, 0, 10};
    second.uAxis = {1, 0, 0};
    second.vAxis = {0, std::cos(angle), std::sin(angle)};
    second.normal = Cross(second.uAxis, second.vAxis);
    request.secondPlane = second;
    RequireRefused(request, "GEO-P003", "0.5度傾いた2面");
}

// =====================================================================
//  5. 円筒の中心
// =====================================================================

KACHA_V2_TEST(workplane_cylinder, 中心軸を含む平面になる)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::CylinderAxis;
    request.surface = Cylinder({5.0, 6.0, 7.0}, 10.0);
    const WorkPlaneFrame frame = BuildOrFail(request, "円筒の中心");
    // 軸上の点は、どれも平面の上にある。
    for (const double along : {-50.0, 0.0, 50.0}) {
        const Vector3 point = Vector3{5.0, 6.0, 7.0} + Vector3{0, 0, 1} * along;
        RequireNear(frame.SignedDistance(point), 0.0, 1.0e-9, "軸上の点は面上");
    }
}

KACHA_V2_TEST(workplane_cylinder, 法線は軸に直交する)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::CylinderAxis;
    request.surface = Cylinder({0, 0, 0}, 4.0);
    const WorkPlaneFrame frame = BuildOrFail(request, "円筒の中心");
    RequireNear(Dot(frame.normal, Vector3{0, 0, 1}), 0.0, 1.0e-12, "法線と軸は直交");
}

KACHA_V2_TEST(workplane_cylinder, 円錐でも中心軸が使える)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::CylinderAxis;
    AnalyticSurfaceInfo cone;
    cone.kind = AnalyticSurfaceKind::Cone;
    cone.origin = {1, 1, 1};
    cone.axis = {0, 1, 0};
    cone.reference = {1, 0, 0};
    cone.halfAngleRad = 0.3;
    request.surface = cone;
    const WorkPlaneFrame frame = BuildOrFail(request, "円錐の中心");
    RequireNear(Dot(frame.normal, Vector3{0, 1, 0}), 0.0, 1.0e-12, "法線と軸は直交");
}

KACHA_V2_TEST(workplane_cylinder, 平面を選んだら断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::CylinderAxis;
    AnalyticSurfaceInfo plane;
    plane.kind = AnalyticSurfaceKind::Plane;
    request.surface = plane;
    RequireRefused(request, "GEO-P004", "平面には中心軸が無い");
}

KACHA_V2_TEST(workplane_cylinder, 正体の分からない面は断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::CylinderAxis;
    RequireRefused(request, "GEO-P004", "正体不明の面");
}

// =====================================================================
//  6. 辺まわりに角度
// =====================================================================

KACHA_V2_TEST(workplane_angle, 90度回すと法線が直交する)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::AngleAboutEdge;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    request.edges = {Line({0, 0, 0}, {10, 0, 0})};   // X軸まわり
    request.angleRad = kPi / 2.0;
    const WorkPlaneFrame frame = BuildOrFail(request, "90度");
    RequireNear(Dot(frame.normal, Vector3{0, 0, 1}), 0.0, 1.0e-12, "元の法線と直交");
    RequireNear(std::abs(Dot(frame.normal, Vector3{0, 1, 0})), 1.0, 1.0e-12,
        "法線はY方向");
}

KACHA_V2_TEST(workplane_angle, 0度なら元の面と同じ向き)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::AngleAboutEdge;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    request.edges = {Line({0, 0, 0}, {10, 0, 0})};
    request.angleRad = 0.0;
    const WorkPlaneFrame frame = BuildOrFail(request, "0度");
    RequireNear(Dot(frame.normal, Vector3{0, 0, 1}), 1.0, 1.0e-12, "法線は同じ");
}

KACHA_V2_TEST(workplane_angle, 任意の角度で法線の角度が合う)
{
    for (const double degrees : {15.0, 30.0, 45.0, 60.0, 120.0, 179.0}) {
        WorkPlaneRequest request;
        request.method = WorkPlaneMethod::AngleAboutEdge;
        request.referencePlane = StandardPlane(StandardPlaneKind::XY);
        request.edges = {Line({0, 0, 0}, {10, 0, 0})};
        request.angleRad = degrees * kPi / 180.0;
        const WorkPlaneFrame frame = BuildOrFail(request, "回転");
        const double cosine = Dot(frame.normal, Vector3{0, 0, 1});
        RequireNear(std::acos(std::clamp(cosine, -1.0, 1.0)) * 180.0 / kPi, degrees,
            1.0e-9, "回した角度");
    }
}

KACHA_V2_TEST(workplane_angle, 回した平面は辺を含む)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::AngleAboutEdge;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    request.edges = {Line({2, 0, 0}, {12, 0, 0})};
    request.angleRad = 37.0 * kPi / 180.0;
    const WorkPlaneFrame frame = BuildOrFail(request, "辺を含む");
    RequireNear(frame.SignedDistance({2, 0, 0}), 0.0, 1.0e-12, "始点は面上");
    RequireNear(frame.SignedDistance({12, 0, 0}), 0.0, 1.0e-12, "終点は面上");
}

KACHA_V2_TEST(workplane_angle, 長さ0の辺は断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::AngleAboutEdge;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    // MakeLine が長さ0を断るので、極端に短い辺で代用する。
    request.edges = {Line({0, 0, 0}, {1.0e-12, 0, 0})};
    request.angleRad = kPi / 4.0;
    RequireRefused(request, "GEO-P002", "長さ0の辺");
}

KACHA_V2_TEST(workplane_angle, 角度が数でなければ断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::AngleAboutEdge;
    request.referencePlane = StandardPlane(StandardPlaneKind::XY);
    request.edges = {Line({0, 0, 0}, {10, 0, 0})};
    request.angleRad = std::nan("");
    RequireRefused(request, "GEO-P001", "NaNの角度");
}

// =====================================================================
//  7. 3点を通る
// =====================================================================

KACHA_V2_TEST(workplane_three_points, 3点すべてが面の上に載る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThreePoints;
    request.points = {{0, 0, 0}, {10, 0, 4}, {0, 8, -2}};
    const WorkPlaneFrame frame = BuildOrFail(request, "3点");
    for (const Vector3& point : request.points) {
        RequireNear(frame.SignedDistance(point), 0.0, 1.0e-9, "点が面上");
    }
}

KACHA_V2_TEST(workplane_three_points, 原点は1点目になる)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThreePoints;
    request.points = {{3, 4, 5}, {13, 4, 5}, {3, 14, 5}};
    const WorkPlaneFrame frame = BuildOrFail(request, "3点");
    RequireVector(frame.origin, {3, 4, 5}, 1.0e-12, "原点");
    RequireVector(frame.uAxis, {1, 0, 0}, 1.0e-12, "u軸は1点目から2点目");
    RequireNear(std::abs(Dot(frame.normal, Vector3{0, 0, 1})), 1.0, 1.0e-12, "法線");
}

KACHA_V2_TEST(workplane_three_points, 一直線に並んだ3点は断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThreePoints;
    request.points = {{0, 0, 0}, {10, 0, 0}, {25, 0, 0}};
    RequireRefused(request, "GEO-P002", "一直線の3点");
}

KACHA_V2_TEST(workplane_three_points, ほとんど一直線でも断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThreePoints;
    request.points = {{0, 0, 0}, {100, 0, 0}, {50, 1.0e-9, 0}};
    RequireRefused(request, "GEO-P002", "ほぼ一直線の3点");
}

KACHA_V2_TEST(workplane_three_points, わずかにずれていれば作れる)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThreePoints;
    request.points = {{0, 0, 0}, {100, 0, 0}, {50, 0.001, 0}};
    const WorkPlaneFrame frame = BuildOrFail(request, "細長い三角形");
    RequireNear(std::abs(Dot(frame.normal, Vector3{0, 0, 1})), 1.0, 1.0e-9, "法線");
}

KACHA_V2_TEST(workplane_three_points, 同じ点が2つあれば断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThreePoints;
    request.points = {{1, 2, 3}, {1, 2, 3}, {5, 6, 7}};
    RequireRefused(request, "GEO-P002", "重なった点");
}

KACHA_V2_TEST(workplane_three_points, 点が2つしか無ければ断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThreePoints;
    request.points = {{0, 0, 0}, {1, 0, 0}};
    RequireRefused(request, "GEO-P001", "点が足りない");
}

KACHA_V2_TEST(workplane_three_points, 有限でない座標は断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThreePoints;
    request.points = {{0, 0, 0}, {1, 0, 0}, {0, std::nan(""), 0}};
    RequireRefused(request, "GEO-P001", "NaNの座標");
}

// =====================================================================
//  8. 2辺を含む
// =====================================================================

KACHA_V2_TEST(workplane_two_edges, 交わる2辺から面が決まる)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TwoEdges;
    request.edges = {Line({0, 0, 0}, {10, 0, 0}), Line({0, 0, 0}, {0, 10, 0})};
    const WorkPlaneFrame frame = BuildOrFail(request, "交わる2辺");
    RequireNear(std::abs(Dot(frame.normal, Vector3{0, 0, 1})), 1.0, 1.0e-12, "法線");
    RequireNear(frame.SignedDistance({10, 0, 0}), 0.0, 1.0e-12, "1辺目の終点");
    RequireNear(frame.SignedDistance({0, 10, 0}), 0.0, 1.0e-12, "2辺目の終点");
}

KACHA_V2_TEST(workplane_two_edges, 平行な2辺から面が決まる)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TwoEdges;
    request.edges = {Line({0, 0, 0}, {10, 0, 0}), Line({0, 6, 0}, {10, 6, 0})};
    const WorkPlaneFrame frame = BuildOrFail(request, "平行な2辺");
    RequireNear(std::abs(Dot(frame.normal, Vector3{0, 0, 1})), 1.0, 1.0e-12, "法線");
    RequireNear(frame.SignedDistance({0, 6, 0}), 0.0, 1.0e-12, "2辺目が面上");
}

KACHA_V2_TEST(workplane_two_edges, 同じ直線上の2辺は断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TwoEdges;
    request.edges = {Line({0, 0, 0}, {10, 0, 0}), Line({20, 0, 0}, {30, 0, 0})};
    RequireRefused(request, "GEO-P002", "同一直線上の2辺");
}

KACHA_V2_TEST(workplane_two_edges, ねじれの位置にある2辺は断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TwoEdges;
    request.edges = {Line({0, 0, 0}, {10, 0, 0}), Line({0, 0, 5}, {0, 10, 5})};
    RequireRefused(request, "GEO-P006", "ねじれの2辺");
}

KACHA_V2_TEST(workplane_two_edges, 辺が1本しか無ければ断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TwoEdges;
    request.edges = {Line({0, 0, 0}, {10, 0, 0})};
    RequireRefused(request, "GEO-P001", "辺が足りない");
}

KACHA_V2_TEST(workplane_two_edges, 傾いた2辺でも両方が面上に来る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TwoEdges;
    request.edges = {Line({1, 2, 3}, {11, 4, 9}), Line({1, 2, 3}, {3, 12, 5})};
    const WorkPlaneFrame frame = BuildOrFail(request, "傾いた2辺");
    RequireNear(frame.SignedDistance({11, 4, 9}), 0.0, 1.0e-9, "1辺目");
    RequireNear(frame.SignedDistance({3, 12, 5}), 0.0, 1.0e-9, "2辺目");
}

// =====================================================================
//  9. 辺を通り接する
// =====================================================================

KACHA_V2_TEST(workplane_tangent_edge, 円筒の母線に接する平面ができる)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TangentThroughEdge;
    request.surface = Cylinder({0, 0, 0}, 10.0);
    // 半径10、X方向の母線。
    request.edges = {Line({10, 0, 0}, {10, 0, 25})};
    const WorkPlaneFrame frame = BuildOrFail(request, "母線に接する");
    RequireVector(frame.normal, {1, 0, 0}, 1.0e-12, "法線は半径方向");
    RequireNear(frame.SignedDistance({10, 0, 0}), 0.0, 1.0e-12, "母線が面上");
    // 中心軸からの距離が半径に等しい = 接している。
    RequireNear(std::abs(frame.SignedDistance({0, 0, 0})), 10.0, 1.0e-12, "接している");
}

KACHA_V2_TEST(workplane_tangent_edge, 別の角度の母線でも接する)
{
    const double angle = 0.7;
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TangentThroughEdge;
    request.surface = Cylinder({0, 0, 0}, 6.0);
    const Vector3 base{6.0 * std::cos(angle), 6.0 * std::sin(angle), 0.0};
    request.edges = {Line(base, base + Vector3{0, 0, 20})};
    const WorkPlaneFrame frame = BuildOrFail(request, "斜めの母線");
    RequireNear(std::abs(frame.SignedDistance({0, 0, 0})), 6.0, 1.0e-9, "接している");
}

KACHA_V2_TEST(workplane_tangent_edge, 母線でない辺は断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TangentThroughEdge;
    request.surface = Cylinder({0, 0, 0}, 10.0);
    // 半径方向へ伸びる辺。接する平面には載らない。
    request.edges = {Line({10, 0, 0}, {20, 0, 0})};
    RequireRefused(request, "GEO-P006", "母線でない辺");
}

KACHA_V2_TEST(workplane_tangent_edge, 中心軸の上では断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TangentThroughEdge;
    request.surface = Cylinder({0, 0, 0}, 10.0);
    request.edges = {Line({0, 0, 0}, {0, 0, 20})};
    RequireRefused(request, "GEO-P002", "軸の上");
}

KACHA_V2_TEST(workplane_tangent_edge, 相手の面が分からなければ断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TangentThroughEdge;
    request.edges = {Line({10, 0, 0}, {10, 0, 25})};
    RequireRefused(request, "GEO-P004", "正体不明の面");
}

// =====================================================================
//  10. 点を通り接する
// =====================================================================

KACHA_V2_TEST(workplane_tangent_point, 円筒面上の点で接する)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TangentThroughPoint;
    request.surface = Cylinder({0, 0, 0}, 8.0);
    request.points = {{0, 8, 3}};
    const WorkPlaneFrame frame = BuildOrFail(request, "円筒に接する");
    RequireVector(frame.normal, {0, 1, 0}, 1.0e-12, "法線");
    RequireNear(std::abs(frame.SignedDistance({0, 0, 3})), 8.0, 1.0e-12, "接している");
}

KACHA_V2_TEST(workplane_tangent_point, 球面上の点で接する)
{
    AnalyticSurfaceInfo sphere;
    sphere.kind = AnalyticSurfaceKind::Sphere;
    sphere.origin = {1, 2, 3};
    sphere.axis = {0, 0, 1};
    sphere.reference = {1, 0, 0};
    sphere.radiusMm = 5.0;
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TangentThroughPoint;
    request.surface = sphere;
    request.points = {{1, 2, 8}};   // 真上
    const WorkPlaneFrame frame = BuildOrFail(request, "球に接する");
    RequireVector(frame.normal, {0, 0, 1}, 1.0e-12, "法線");
    RequireNear(std::abs(frame.SignedDistance({1, 2, 3})), 5.0, 1.0e-12, "接している");
}

KACHA_V2_TEST(workplane_tangent_point, 球の中心では断る)
{
    AnalyticSurfaceInfo sphere;
    sphere.kind = AnalyticSurfaceKind::Sphere;
    sphere.origin = {0, 0, 0};
    sphere.radiusMm = 5.0;
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TangentThroughPoint;
    request.surface = sphere;
    request.points = {{0, 0, 0}};
    RequireRefused(request, "GEO-P002", "球の中心");
}

KACHA_V2_TEST(workplane_tangent_point, 円筒の軸の上では断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TangentThroughPoint;
    request.surface = Cylinder({0, 0, 0}, 8.0);
    request.points = {{0, 0, 5}};
    RequireRefused(request, "GEO-P002", "軸の上");
}

KACHA_V2_TEST(workplane_tangent_point, 同じ入力からは毎回同じ基底が出る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TangentThroughPoint;
    request.surface = Cylinder({0, 0, 0}, 8.0);
    request.points = {{0, 8, 3}};
    const WorkPlaneFrame first = BuildOrFail(request, "1回目");
    for (int attempt = 0; attempt < 5; ++attempt) {
        const WorkPlaneFrame again = BuildOrFail(request, "くり返し");
        RequireVector(again.uAxis, first.uAxis, 0.0, "u軸が毎回同じ");
        RequireVector(again.vAxis, first.vAxis, 0.0, "v軸が毎回同じ");
    }
}

KACHA_V2_TEST(workplane_tangent_point, 円錐面上の点で接する)
{
    AnalyticSurfaceInfo cone;
    cone.kind = AnalyticSurfaceKind::Cone;
    cone.origin = {0, 0, 0};
    cone.axis = {0, 0, 1};
    cone.reference = {1, 0, 0};
    cone.radiusMm = 10.0;
    cone.halfAngleRad = kPi / 4.0;   // 45度
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::TangentThroughPoint;
    request.surface = cone;
    request.points = {{10, 0, 0}};
    const WorkPlaneFrame frame = BuildOrFail(request, "円錐に接する");
    // 45度の円錐の法線は、半径方向と軸方向が同じ大きさ。
    RequireNear(frame.normal.x, 1.0 / std::sqrt(2.0), 1.0e-12, "法線のX");
    RequireNear(frame.normal.z, -1.0 / std::sqrt(2.0), 1.0e-12, "法線のZ");
}

// =====================================================================
//  11. 点で曲線に直角
// =====================================================================

KACHA_V2_TEST(workplane_normal_curve, 円弧の中点で接線に直角な面ができる)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::NormalToCurveAtPoint;
    request.edges = {Arc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0, 0.0, kPi / 2.0)};
    request.curveParameter = 0.5;
    const WorkPlaneFrame frame = BuildOrFail(request, "円弧に直角");
    // 45度の位置。接線は (-sin45, cos45, 0)。
    const double root2 = 1.0 / std::sqrt(2.0);
    RequireVector(frame.normal, {-root2, root2, 0}, 1.0e-9, "法線は接線");
    RequireVector(frame.origin, {10.0 * root2, 10.0 * root2, 0}, 1.0e-9, "原点");
}

KACHA_V2_TEST(workplane_normal_curve, u軸は曲率法線を向く)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::NormalToCurveAtPoint;
    request.edges = {Arc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0, 0.0, kPi / 2.0)};
    request.curveParameter = 0.0;
    const WorkPlaneFrame frame = BuildOrFail(request, "円弧の始点");
    // 始点 (10,0,0) での曲率法線は中心を向く = -X。
    RequireVector(frame.uAxis, {-1, 0, 0}, 1.0e-9, "u軸は中心向き");
}

KACHA_V2_TEST(workplane_normal_curve, 曲線上の点が面の原点になる)
{
    for (const double parameter : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        WorkPlaneRequest request;
        request.method = WorkPlaneMethod::NormalToCurveAtPoint;
        request.edges = {Arc({2, 3, 4}, {0, 1, 0}, {1, 0, 0}, 7.0, 0.2, 1.1)};
        request.curveParameter = parameter;
        const WorkPlaneFrame frame = BuildOrFail(request, "円弧上の点");
        RequireVector(frame.origin, request.edges.front().Evaluate(parameter), 1.0e-9,
            "原点は曲線上");
    }
}

KACHA_V2_TEST(workplane_normal_curve, 直線は曲率法線が無いので断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::NormalToCurveAtPoint;
    request.edges = {Line({0, 0, 0}, {10, 5, 2})};
    request.curveParameter = 0.5;
    RequireRefused(request, "GEO-P005", "直線の曲率法線");
}

KACHA_V2_TEST(workplane_normal_curve, 曲率法線を使わない設定なら直線でも作れる)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::NormalToCurveAtPoint;
    request.edges = {Line({0, 0, 0}, {10, 0, 0})};
    request.curveParameter = 0.5;
    request.useCurvatureNormal = false;
    const WorkPlaneFrame frame = BuildOrFail(request, "直線に直角");
    RequireVector(frame.normal, {1, 0, 0}, 1.0e-12, "法線は直線の向き");
    RequireVector(frame.origin, {5, 0, 0}, 1.0e-12, "原点は中点");
}

KACHA_V2_TEST(workplane_normal_curve, 位置が0から1の外なら断る)
{
    for (const double parameter : {-0.1, 1.5}) {
        WorkPlaneRequest request;
        request.method = WorkPlaneMethod::NormalToCurveAtPoint;
        request.edges = {Arc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0, 0.0, kPi / 2.0)};
        request.curveParameter = parameter;
        RequireRefused(request, "GEO-P001", "範囲外の位置");
    }
}

KACHA_V2_TEST(workplane_normal_curve, 辺が無ければ断る)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::NormalToCurveAtPoint;
    request.curveParameter = 0.5;
    RequireRefused(request, "GEO-P001", "辺なし");
}

KACHA_V2_TEST(workplane_normal_curve, ベジェでも作れる)
{
    const auto made = CurveSegment::MakeCubicBezier(
        {{0, 0, 0}, {2, 6, 0}, {8, 6, 0}, {10, 0, 0}});
    Require(made.HasValue(), "ベジェが作れること");
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::NormalToCurveAtPoint;
    request.edges = {made.Value()};
    request.curveParameter = 0.5;
    const WorkPlaneFrame frame = BuildOrFail(request, "ベジェに直角");
    // 対称なベジェの中点では、接線はX方向。
    RequireNear(std::abs(Dot(frame.normal, Vector3{1, 0, 0})), 1.0, 1.0e-9, "法線");
}

// =====================================================================
//  共通の性質
// =====================================================================

KACHA_V2_TEST(workplane_common, 全11方式が右手系の正規直交を返す)
{
    std::vector<WorkPlaneRequest> requests;

    WorkPlaneRequest standard;
    standard.method = WorkPlaneMethod::Standard;
    requests.push_back(standard);

    WorkPlaneRequest offset;
    offset.method = WorkPlaneMethod::OffsetFromPlane;
    offset.referencePlane = StandardPlane(StandardPlaneKind::ZX);
    offset.offsetMm = 5.0;
    requests.push_back(offset);

    WorkPlaneRequest parallel;
    parallel.method = WorkPlaneMethod::ThroughPointParallel;
    parallel.referencePlane = StandardPlane(StandardPlaneKind::YZ);
    parallel.points = {{4, 5, 6}};
    requests.push_back(parallel);

    WorkPlaneRequest middle;
    middle.method = WorkPlaneMethod::MidBetweenPlanes;
    middle.referencePlane = StandardPlane(StandardPlaneKind::XY);
    middle.secondPlane = StandardPlane(StandardPlaneKind::XY);
    middle.secondPlane.origin = {0, 0, 20};
    requests.push_back(middle);

    WorkPlaneRequest cylinder;
    cylinder.method = WorkPlaneMethod::CylinderAxis;
    cylinder.surface = Cylinder({1, 2, 3}, 9.0);
    requests.push_back(cylinder);

    WorkPlaneRequest angle;
    angle.method = WorkPlaneMethod::AngleAboutEdge;
    angle.referencePlane = StandardPlane(StandardPlaneKind::XY);
    angle.edges = {Line({0, 0, 0}, {10, 0, 0})};
    angle.angleRad = 0.6;
    requests.push_back(angle);

    WorkPlaneRequest three;
    three.method = WorkPlaneMethod::ThreePoints;
    three.points = {{0, 0, 0}, {10, 1, 2}, {1, 9, 3}};
    requests.push_back(three);

    WorkPlaneRequest two;
    two.method = WorkPlaneMethod::TwoEdges;
    two.edges = {Line({0, 0, 0}, {10, 0, 0}), Line({0, 0, 0}, {0, 10, 0})};
    requests.push_back(two);

    WorkPlaneRequest tangentEdge;
    tangentEdge.method = WorkPlaneMethod::TangentThroughEdge;
    tangentEdge.surface = Cylinder({0, 0, 0}, 10.0);
    tangentEdge.edges = {Line({10, 0, 0}, {10, 0, 25})};
    requests.push_back(tangentEdge);

    WorkPlaneRequest tangentPoint;
    tangentPoint.method = WorkPlaneMethod::TangentThroughPoint;
    tangentPoint.surface = Cylinder({0, 0, 0}, 10.0);
    tangentPoint.points = {{0, 10, 4}};
    requests.push_back(tangentPoint);

    WorkPlaneRequest normal;
    normal.method = WorkPlaneMethod::NormalToCurveAtPoint;
    normal.edges = {Arc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0, 0.0, kPi / 2.0)};
    normal.curveParameter = 0.4;
    requests.push_back(normal);

    RequireEqual(std::to_string(requests.size()), "11", "11方式ある");
    for (const WorkPlaneRequest& request : requests) {
        const auto built = BuildWorkPlane(request, Tolerance());
        Require(built.HasValue(),
            std::string(kachakacha::v2::modeling::WorkPlaneMethodNameJa(request.method))
                + ": 作れること");
        RequireGoodFrame(built.Value(),
            std::string(kachakacha::v2::modeling::WorkPlaneMethodName(request.method)));
    }
}

KACHA_V2_TEST(workplane_common, 方式の名前が全部そろっている)
{
    const std::vector<WorkPlaneMethod> methods{
        WorkPlaneMethod::Standard,
        WorkPlaneMethod::OffsetFromPlane,
        WorkPlaneMethod::ThroughPointParallel,
        WorkPlaneMethod::MidBetweenPlanes,
        WorkPlaneMethod::CylinderAxis,
        WorkPlaneMethod::AngleAboutEdge,
        WorkPlaneMethod::ThreePoints,
        WorkPlaneMethod::TwoEdges,
        WorkPlaneMethod::TangentThroughEdge,
        WorkPlaneMethod::TangentThroughPoint,
        WorkPlaneMethod::NormalToCurveAtPoint,
    };
    RequireEqual(std::to_string(methods.size()), "11", "11方式");
    for (const WorkPlaneMethod method : methods) {
        Require(kachakacha::v2::modeling::WorkPlaneMethodName(method) != "unknown",
            "英語名がある");
        Require(kachakacha::v2::modeling::WorkPlaneMethodNameJa(method) != "不明",
            "日本語名がある");
    }
}

KACHA_V2_TEST(workplane_common, uv座標と3D座標が往復する)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThreePoints;
    request.points = {{1, 2, 3}, {11, 4, 9}, {3, 12, 5}};
    const WorkPlaneFrame frame = BuildOrFail(request, "3点");
    for (const double u : {-30.0, 0.0, 17.5}) {
        for (const double v : {-8.0, 0.0, 44.25}) {
            const Vector3 point = frame.PointAt(u, v);
            RequireNear(frame.CoordinateU(point), u, 1.0e-9, "u が戻る");
            RequireNear(frame.CoordinateV(point), v, 1.0e-9, "v が戻る");
            RequireNear(frame.SignedDistance(point), 0.0, 1.0e-9, "面上にある");
        }
    }
}

KACHA_V2_TEST(workplane_common, 原点から遠い場所でも基底が崩れない)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThreePoints;
    request.points = {{1.0e6, 2.0e6, 3.0e6}, {1.0e6 + 10, 2.0e6, 3.0e6},
        {1.0e6, 2.0e6 + 10, 3.0e6}};
    const WorkPlaneFrame frame = BuildOrFail(request, "遠い場所");
    RequireNear(std::abs(Dot(frame.normal, Vector3{0, 0, 1})), 1.0, 1.0e-9, "法線");
}

KACHA_V2_TEST(workplane_common, とても小さい三角形でも基底が崩れない)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThreePoints;
    request.points = {{0, 0, 0}, {0.001, 0, 0}, {0, 0.001, 0}};
    const WorkPlaneFrame frame = BuildOrFail(request, "小さい三角形");
    RequireNear(std::abs(Dot(frame.normal, Vector3{0, 0, 1})), 1.0, 1.0e-12, "法線");
}

KACHA_V2_TEST(workplane_common, 断ったときに例外を投げない)
{
    // 壊れた入力を並べて、どれも診断で返ることを確かめる。
    std::vector<WorkPlaneRequest> broken;
    WorkPlaneRequest a;
    a.method = WorkPlaneMethod::ThreePoints;
    broken.push_back(a);
    WorkPlaneRequest b;
    b.method = WorkPlaneMethod::TwoEdges;
    broken.push_back(b);
    WorkPlaneRequest c;
    c.method = WorkPlaneMethod::CylinderAxis;
    broken.push_back(c);
    WorkPlaneRequest d;
    d.method = WorkPlaneMethod::NormalToCurveAtPoint;
    broken.push_back(d);
    WorkPlaneRequest e;
    e.method = WorkPlaneMethod::TangentThroughPoint;
    broken.push_back(e);
    WorkPlaneRequest f;
    f.method = WorkPlaneMethod::MidBetweenPlanes;
    f.referencePlane = StandardPlane(StandardPlaneKind::XY);
    f.secondPlane = StandardPlane(StandardPlaneKind::ZX);
    broken.push_back(f);
    for (const WorkPlaneRequest& request : broken) {
        const auto built = BuildWorkPlane(request, Tolerance());
        Require(!built.HasValue(), "断ること");
        Require(!built.Diagnostics().empty(), "診断があること");
        Require(!built.Diagnostics().front().code.empty(), "診断コードが空でない");
        Require(!built.Diagnostics().front().summaryJa.empty(), "日本語の説明がある");
    }
}

KACHA_V2_TEST(workplane_common, 断っても入力を書き換えない)
{
    WorkPlaneRequest request;
    request.method = WorkPlaneMethod::ThreePoints;
    request.points = {{0, 0, 0}, {10, 0, 0}, {25, 0, 0}};
    const std::vector<Vector3> before = request.points;
    const auto built = BuildWorkPlane(request, Tolerance());
    Require(!built.HasValue(), "断ること");
    RequireEqual(std::to_string(request.points.size()), std::to_string(before.size()),
        "点の数が変わらない");
    for (std::size_t index = 0; index < before.size(); ++index) {
        Require(request.points[index] == before[index], "点が変わらない");
    }
}

KACHA_V2_TEST_MAIN("work_plane_tests")
