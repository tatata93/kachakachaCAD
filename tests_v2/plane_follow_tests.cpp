// 作業平面とワイヤーの関係(AT-WPL-003)。
//
// 3通りのうち LockedToPlane だけが、平面内 UV を保って追従する。
// ほかの2つは平面が動いても3D座標が変わらない。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <cmath>
#include <set>
#include <string>

using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveKindName;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::FollowPlane;
using kachakacha::v2::modeling::FollowPlaneCurve;
using kachakacha::v2::modeling::PlanePolicy;
using kachakacha::v2::modeling::PlanePolicyName;
using kachakacha::v2::modeling::PlanePolicyNameJa;
using kachakacha::v2::modeling::StandardPlane;
using kachakacha::v2::modeling::StandardPlaneKind;
using kachakacha::v2::modeling::WorkPlaneFrame;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

void RequireVectorNear(const Vector3& actual, const Vector3& expected, const char* what)
{
    RequireNear(actual.x, expected.x, 1e-9, what);
    RequireNear(actual.y, expected.y, 1e-9, what);
    RequireNear(actual.z, expected.z, 1e-9, what);
}

//! XY平面を +Z へ 25mm 動かした平面。
[[nodiscard]] WorkPlaneFrame Raised(double height)
{
    WorkPlaneFrame frame = StandardPlane(StandardPlaneKind::XY);
    frame.origin = Vector3{0.0, 0.0, height};
    return frame;
}

//! XY平面を Z軸まわりに回した平面。
[[nodiscard]] WorkPlaneFrame Turned(double angleRad)
{
    WorkPlaneFrame frame = StandardPlane(StandardPlaneKind::XY);
    frame.uAxis = Vector3{std::cos(angleRad), std::sin(angleRad), 0.0};
    frame.vAxis = Vector3{-std::sin(angleRad), std::cos(angleRad), 0.0};
    return frame;
}

const PlanePolicy kUnbound[] = {PlanePolicy::Free3D, PlanePolicy::ReferenceOnly};

} // namespace

KACHA_V2_TEST(plane_follow, 3通りの名前がそろっている)
{
    std::set<std::string> names;
    std::set<std::string> japanese;
    for (PlanePolicy policy : {PlanePolicy::Free3D, PlanePolicy::ReferenceOnly,
             PlanePolicy::LockedToPlane}) {
        Require(names.insert(std::string(PlanePolicyName(policy))).second, "名前が重ならない");
        Require(japanese.insert(std::string(PlanePolicyNameJa(policy))).second,
            "日本語も重ならない");
    }
    RequireEqual(std::to_string(names.size()), "3", "3通り");
}

KACHA_V2_TEST(plane_follow, 縛られていない点は平面を動かしても動かない)
{
    const Vector3 point{10.0, 20.0, 0.0};
    for (PlanePolicy policy : kUnbound) {
        const auto moved = FollowPlane(point, policy, StandardPlane(StandardPlaneKind::XY),
            Raised(25.0));
        Require(moved.HasValue(), "値が返る");
        RequireVectorNear(moved.Value(), point,
            std::string(PlanePolicyName(policy)).c_str());
    }
}

KACHA_V2_TEST(plane_follow, 縛られた点は平面について動く)
{
    const Vector3 point{10.0, 20.0, 0.0};
    const auto moved = FollowPlane(point, PlanePolicy::LockedToPlane,
        StandardPlane(StandardPlaneKind::XY), Raised(25.0));
    Require(moved.HasValue(), "値が返る");
    RequireVectorNear(moved.Value(), Vector3{10.0, 20.0, 25.0}, "25mm 持ち上がる");
}

KACHA_V2_TEST(plane_follow, 縛られた点は平面内のUVを保つ)
{
    const WorkPlaneFrame before = StandardPlane(StandardPlaneKind::XY);
    const WorkPlaneFrame after = Turned(kPi / 3.0);
    const Vector3 point{10.0, 20.0, 0.0};
    const double u = before.CoordinateU(point);
    const double v = before.CoordinateV(point);
    const auto moved = FollowPlane(point, PlanePolicy::LockedToPlane, before, after);
    Require(moved.HasValue(), "値が返る");
    RequireNear(after.CoordinateU(moved.Value()), u, 1e-9, "u が保たれる");
    RequireNear(after.CoordinateV(moved.Value()), v, 1e-9, "v が保たれる");
    // 3D座標そのものは変わっている。
    Require((moved.Value() - point).Length() > 1.0, "動いている");
}

KACHA_V2_TEST(plane_follow, 面から浮いていた分も保つ)
{
    const WorkPlaneFrame before = StandardPlane(StandardPlaneKind::XY);
    const Vector3 point{5.0, 5.0, 3.0};
    const auto moved = FollowPlane(point, PlanePolicy::LockedToPlane, before, Raised(25.0));
    Require(moved.HasValue(), "値が返る");
    RequireNear(moved.Value().z, 28.0, 1e-9, "浮いた3mmも保つ");
    RequireNear(Raised(25.0).SignedDistance(moved.Value()), 3.0, 1e-9, "面からの距離");
}

KACHA_V2_TEST(plane_follow, 平面を動かさなければ点も動かない)
{
    const WorkPlaneFrame plane = StandardPlane(StandardPlaneKind::XY);
    const Vector3 point{7.0, -3.0, 2.0};
    const auto moved = FollowPlane(point, PlanePolicy::LockedToPlane, plane, plane);
    Require(moved.HasValue(), "値が返る");
    RequireVectorNear(moved.Value(), point, "同じ");
}

KACHA_V2_TEST(plane_follow, 動かして戻せば元の位置へ戻る)
{
    const WorkPlaneFrame first = StandardPlane(StandardPlaneKind::XY);
    const WorkPlaneFrame second = Turned(0.7);
    const Vector3 point{12.0, -5.0, 1.5};
    const auto there = FollowPlane(point, PlanePolicy::LockedToPlane, first, second);
    Require(there.HasValue(), "動く");
    const auto back = FollowPlane(there.Value(), PlanePolicy::LockedToPlane, second, first);
    Require(back.HasValue(), "戻る");
    RequireVectorNear(back.Value(), point, "元の位置");
}

KACHA_V2_TEST(plane_follow, 数値でない点を断る)
{
    const auto refused = FollowPlane(Vector3{std::nan(""), 0.0, 0.0},
        PlanePolicy::LockedToPlane, StandardPlane(StandardPlaneKind::XY), Raised(1.0));
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "GEO-P001", "数値でない");
}

KACHA_V2_TEST(plane_follow, 壊れた平面を断る)
{
    WorkPlaneFrame broken = StandardPlane(StandardPlaneKind::XY);
    broken.uAxis = Vector3{0.0, 0.0, 0.0};
    const auto refused = FollowPlane(Vector3{1, 1, 0}, PlanePolicy::LockedToPlane,
        StandardPlane(StandardPlaneKind::XY), broken);
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "GEO-P001", "壊れた平面");
}

KACHA_V2_TEST(plane_follow, 縛られていなければ壊れた平面でも点は動かない)
{
    // 追従しないのだから、平面が壊れていても関係がない。
    WorkPlaneFrame broken = StandardPlane(StandardPlaneKind::XY);
    broken.normal = Vector3{0.0, 0.0, 0.0};
    for (PlanePolicy policy : kUnbound) {
        const auto moved = FollowPlane(Vector3{1, 2, 3}, policy,
            StandardPlane(StandardPlaneKind::XY), broken);
        Require(moved.HasValue(), "値が返る");
        RequireVectorNear(moved.Value(), Vector3{1, 2, 3}, "動かない");
    }
}

KACHA_V2_TEST(plane_follow, 曲線は種類を保ったまま追従する)
{
    const WorkPlaneFrame before = StandardPlane(StandardPlaneKind::XY);
    const WorkPlaneFrame after = Raised(25.0);
    const CurveSegment segments[] = {
        CurveSegment::MakeLine({0, 0, 0}, {10, 0, 0}).Value(),
        CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 20.0, 0.0, 1.2)
            .Value(),
        CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 15.0).Value(),
        CurveSegment::MakeCubicBezier({{0, 0, 0}, {5, 5, 0}, {10, 5, 0}, {15, 0, 0}})
            .Value(),
        CurveSegment::MakeCubicBSpline(
            {{0, 0, 0}, {5, 8, 0}, {12, 2, 0}, {18, 9, 0}, {24, 4, 0}})
            .Value(),
    };
    for (const CurveSegment& segment : segments) {
        const auto moved = FollowPlaneCurve(segment, PlanePolicy::LockedToPlane, before,
            after);
        Require(moved.HasValue(),
            std::string("追従できる: ") + std::string(CurveKindName(segment.Kind())));
        RequireEqual(std::string(CurveKindName(moved.Value().Kind())),
            std::string(CurveKindName(segment.Kind())), "種類が変わらない");
        // 25mm 持ち上がっている。
        RequireNear(moved.Value().StartPoint().z, segment.StartPoint().z + 25.0, 1e-9,
            "持ち上がる");
        RequireNear(moved.Value().EndPoint().z, segment.EndPoint().z + 25.0, 1e-9,
            "終点も持ち上がる");
    }
}

KACHA_V2_TEST(plane_follow, 円弧は半径も掃引角も変わらない)
{
    const CurveSegment arc =
        CurveSegment::MakeCircularArc({3, 4, 0}, {0, 0, 1}, {1, 0, 0}, 20.0, 0.3, 1.7)
            .Value();
    const auto moved = FollowPlaneCurve(arc, PlanePolicy::LockedToPlane,
        StandardPlane(StandardPlaneKind::XY), Turned(0.9));
    Require(moved.HasValue(), "追従できる");
    RequireNear(moved.Value().Radius(), 20.0, 1e-9, "半径");
    RequireNear(moved.Value().SweepAngleRad(), 1.7, 1e-9, "掃引角");
    RequireNear(moved.Value().TotalLength(1e-6), arc.TotalLength(1e-6), 1e-6, "長さ");
}

KACHA_V2_TEST(plane_follow, 縛られていない曲線は動かない)
{
    const CurveSegment line = CurveSegment::MakeLine({0, 0, 0}, {10, 0, 0}).Value();
    for (PlanePolicy policy : kUnbound) {
        const auto moved = FollowPlaneCurve(line, policy,
            StandardPlane(StandardPlaneKind::XY), Raised(25.0));
        Require(moved.HasValue(), "値が返る");
        RequireVectorNear(moved.Value().StartPoint(), line.StartPoint(), "始点");
        RequireVectorNear(moved.Value().EndPoint(), line.EndPoint(), "終点");
    }
}

KACHA_V2_TEST(plane_follow, 追従しても曲線の長さと形は保たれる)
{
    // 平面の平行移動と回転は剛体運動なので、長さは変わらない。
    // ここが崩れると、平面を動かしただけで寸法が変わる。
    const CurveSegment bezier =
        CurveSegment::MakeCubicBezier({{0, 0, 0}, {5, 12, 0}, {18, 12, 0}, {24, 0, 0}})
            .Value();
    const WorkPlaneFrame before = StandardPlane(StandardPlaneKind::XY);
    for (double angle : {0.0, 0.4, 1.1, 2.9}) {
        WorkPlaneFrame after = Turned(angle);
        after.origin = Vector3{3.0, -7.0, 11.0};
        const auto moved = FollowPlaneCurve(bezier, PlanePolicy::LockedToPlane, before,
            after);
        Require(moved.HasValue(), "追従できる");
        RequireNear(moved.Value().TotalLength(1e-6), bezier.TotalLength(1e-6), 1e-6,
            "長さが保たれる");
    }
}

KACHA_V2_TEST(plane_follow, 2Dで描いても3Dで描いても同じ型で出る)
{
    // 契約の「2D/3D作図の出力が同じWire3d型である」。
    // どちらも CurveSegment のまま扱えることを、種類の名前で見る。
    const CurveSegment onPlane = CurveSegment::MakeLine({0, 0, 0}, {10, 0, 0}).Value();
    const CurveSegment inSpace = CurveSegment::MakeLine({0, 0, 0}, {10, 5, 7}).Value();
    RequireEqual(std::string(CurveKindName(onPlane.Kind())),
        std::string(CurveKindName(inSpace.Kind())), "同じ型");
    const auto movedPlane = FollowPlaneCurve(onPlane, PlanePolicy::LockedToPlane,
        StandardPlane(StandardPlaneKind::XY), Raised(5.0));
    const auto movedSpace = FollowPlaneCurve(inSpace, PlanePolicy::Free3D,
        StandardPlane(StandardPlaneKind::XY), Raised(5.0));
    Require(movedPlane.HasValue() && movedSpace.HasValue(), "どちらも扱える");
    RequireNear(movedPlane.Value().EndPoint().z, 5.0, 1e-9, "縛られた方は動く");
    RequireNear(movedSpace.Value().EndPoint().z, 7.0, 1e-9, "縛られない方は動かない");
}

KACHA_V2_TEST(plane_follow, 平面をずらしても円弧の向きは傾かない)
{
    // 円弧の法線と基準方向は「向き」であって「位置」ではない。
    // 位置と同じ式で動かすと、平面の原点の移動が二重にかかって傾く。
    WorkPlaneFrame after = StandardPlane(StandardPlaneKind::XY);
    after.origin = Vector3{10.0, 20.0, 30.0};
    const CurveSegment arc =
        CurveSegment::MakeCircularArc({5, 5, 0}, {0, 0, 1}, {1, 0, 0}, 12.0, 0.0, 1.0)
            .Value();
    const auto moved = FollowPlaneCurve(arc, PlanePolicy::LockedToPlane,
        StandardPlane(StandardPlaneKind::XY), after);
    Require(moved.HasValue(), "追従できる");
    RequireVectorNear(moved.Value().Normal(), Vector3{0.0, 0.0, 1.0}, "法線は真上のまま");
    RequireVectorNear(moved.Value().ReferenceDirection(), Vector3{1.0, 0.0, 0.0},
        "基準方向も変わらない");
    RequireVectorNear(moved.Value().Center(), Vector3{15.0, 25.0, 30.0}, "中心だけ動く");
}

KACHA_V2_TEST(plane_follow, 回した平面では円弧の基準方向も同じだけ回る)
{
    WorkPlaneFrame after = Turned(kPi * 0.5);
    after.origin = Vector3{7.0, -3.0, 2.0};
    const CurveSegment arc =
        CurveSegment::MakeCircularArc({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, 10.0, 0.0, 1.0)
            .Value();
    const auto moved = FollowPlaneCurve(arc, PlanePolicy::LockedToPlane,
        StandardPlane(StandardPlaneKind::XY), after);
    Require(moved.HasValue(), "追従できる");
    // 基準方向は 90度 回って +Y になる。平面の移動は向きに効かない。
    RequireVectorNear(moved.Value().ReferenceDirection(), Vector3{0.0, 1.0, 0.0},
        "90度回る");
    RequireVectorNear(moved.Value().Normal(), Vector3{0.0, 0.0, 1.0}, "法線はそのまま");
    // 円弧の始点は、中心 + 半径 x 基準方向。
    RequireVectorNear(moved.Value().StartPoint(), Vector3{7.0, 7.0, 2.0}, "始点");
}

KACHA_V2_TEST_MAIN("plane_follow_tests")
