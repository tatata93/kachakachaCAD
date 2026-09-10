// 数値で線を作る(app/DirectWireEntry.h)。V1 の右パネル「数値で線を作る」。
#include "kachakacha/app/DirectWireEntry.h"
#include "kachakacha/app/DrawingSession.h"
#include "kachakacha/base/TestHarness.h"

#include <cmath>
#include <string>

using kachakacha::v2::app::BuildDirectWire;
using kachakacha::v2::app::DirectWireKind;
using kachakacha::v2::app::DirectWireKinds;
using kachakacha::v2::app::DirectWirePointCount;
using kachakacha::v2::app::DirectWireRequest;
using kachakacha::v2::app::DrawingSession;
using kachakacha::v2::base::DeterministicIdGenerator;
using kachakacha::v2::base::DocumentId;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::StandardPlane;
using kachakacha::v2::modeling::StandardPlaneKind;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

KACHA_V2_TEST(direct_wire, 平面上の直線は作業平面のuvで置かれる)
{
    // 正面 ZX の上の (u, v) = (10, 20) → u 軸は Z、v 軸は X。世界では z=10, x=20。
    DirectWireRequest request;
    request.kind = DirectWireKind::PlanarLine;
    request.points = {{10.0, 20.0, 0.0}, {40.0, 20.0, 0.0}};
    const auto made = BuildDirectWire(request, StandardPlane(StandardPlaneKind::ZX));
    Require(made.HasValue(), "作れる");
    RequireEqual(std::to_string(made.Value().size()), std::string("1"), "1本");
    const auto start = made.Value().front().StartPoint();
    const auto end = made.Value().front().EndPoint();
    RequireNear(start.z, 10.0, 1e-9, "u は z");
    RequireNear(std::abs(start.x), 20.0, 1e-9, "v は x");
    RequireNear(start.y, 0.0, 1e-9, "平面の上なので y は 0");
    RequireNear((end - start).Length(), 30.0, 1e-9, "長さ 30");
}

KACHA_V2_TEST(direct_wire, 3D直線はそのまま世界座標)
{
    DirectWireRequest request;
    request.kind = DirectWireKind::SpatialLine;
    request.points = {{1.0, 2.0, 3.0}, {4.0, 6.0, 3.0}};
    const auto made = BuildDirectWire(request, StandardPlane(StandardPlaneKind::ZX));
    Require(made.HasValue(), "作れる");
    RequireNear(made.Value().front().EndPoint().y, 6.0, 1e-9, "y はそのまま");
    RequireNear((made.Value().front().EndPoint() - made.Value().front().StartPoint()).Length(),
        5.0, 1e-9, "長さ 5");
}

KACHA_V2_TEST(direct_wire, 円と円弧とベジェも作れる)
{
    DirectWireRequest circle;
    circle.kind = DirectWireKind::PlanarCircle;
    circle.points = {{0.0, 0.0, 0.0}};
    circle.radiusMm = 7.5;
    const auto madeCircle = BuildDirectWire(circle, StandardPlane(StandardPlaneKind::XY));
    Require(madeCircle.HasValue(), "円");
    Require(madeCircle.Value().front().Kind() == CurveKind::Circle, "種類は円");

    DirectWireRequest arc;
    arc.kind = DirectWireKind::PlanarArc;
    arc.points = {{0.0, 0.0, 0.0}, {10.0, 10.0, 0.0}, {20.0, 0.0, 0.0}};
    const auto madeArc = BuildDirectWire(arc, StandardPlane(StandardPlaneKind::XY));
    Require(madeArc.HasValue(), "円弧");
    Require(madeArc.Value().front().Kind() == CurveKind::CircularArc, "種類は円弧");

    DirectWireRequest bezier;
    bezier.kind = DirectWireKind::PlanarBezier;
    bezier.points = {{0.0, 0.0, 0.0}, {10.0, 10.0, 0.0}, {20.0, 10.0, 0.0}, {30.0, 0.0, 0.0}};
    const auto madeBezier = BuildDirectWire(bezier, StandardPlane(StandardPlaneKind::XY));
    Require(madeBezier.HasValue(), "ベジェ");
    Require(madeBezier.Value().front().Kind() == CurveKind::CubicBezier, "種類はベジェ");
}

KACHA_V2_TEST(direct_wire, 足りない点と正でない半径と一直線の3点は断る)
{
    DirectWireRequest few;
    few.kind = DirectWireKind::PlanarArc;
    few.points = {{0.0, 0.0, 0.0}, {10.0, 10.0, 0.0}};
    const auto madeFew = BuildDirectWire(few, StandardPlane(StandardPlaneKind::XY));
    Require(!madeFew.HasValue(), "点が足りないと断る");
    RequireEqual(madeFew.Diagnostics().front().code, std::string("UI-D001"), "理由の番号");

    DirectWireRequest radius;
    radius.kind = DirectWireKind::PlanarCircle;
    radius.points = {{0.0, 0.0, 0.0}};
    radius.radiusMm = 0.0;
    const auto madeRadius = BuildDirectWire(radius, StandardPlane(StandardPlaneKind::XY));
    Require(!madeRadius.HasValue(), "半径 0 は断る");
    RequireEqual(madeRadius.Diagnostics().front().code, std::string("UI-D002"), "理由の番号");

    DirectWireRequest flat;
    flat.kind = DirectWireKind::PlanarArc;
    flat.points = {{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {20.0, 0.0, 0.0}};
    const auto madeFlat = BuildDirectWire(flat, StandardPlane(StandardPlaneKind::XY));
    Require(!madeFlat.HasValue(), "一直線の3点は断る");
    Require(!madeFlat.Diagnostics().front().code.empty(), "理由の番号がある");

    DirectWireRequest same;
    same.kind = DirectWireKind::PlanarLine;
    same.points = {{5.0, 5.0, 0.0}, {5.0, 5.0, 0.0}};
    Require(!BuildDirectWire(same, StandardPlane(StandardPlaneKind::XY)).HasValue(),
        "同じ2点は断る");
}

KACHA_V2_TEST(direct_wire, 種類ごとの点の数と並び)
{
    RequireEqual(std::to_string(DirectWireKinds().size()), std::string("5"), "5種類");
    RequireEqual(std::to_string(DirectWirePointCount(DirectWireKind::PlanarBezier)),
        std::string("4"), "ベジェは4点");
    RequireEqual(std::to_string(DirectWirePointCount(DirectWireKind::PlanarCircle)),
        std::string("1"), "円は中心1点");
}

KACHA_V2_TEST(direct_wire, 作った線は文書へ入り名前が付き吸着の相手になる)
{
    DeterministicIdGenerator ids{11};
    DrawingSession session(DocumentId{}, ids);
    kachakacha::v2::modeling::SnapScene scene;
    scene.workPlane.active = true;
    scene.workPlane.normal = {0.0, 0.0, 1.0};
    session.SetScene(scene);
    DirectWireRequest request;
    request.kind = DirectWireKind::PlanarLine;
    request.points = {{0.0, 0.0, 0.0}, {30.0, 0.0, 0.0}};
    const auto made = BuildDirectWire(request, StandardPlane(StandardPlaneKind::XY));
    Require(made.HasValue(), "作れる");
    const auto added = session.AddWire(made.Value(), false, "数値の線");
    Require(added.committed, "文書へ入る");
    RequireEqual(std::to_string(session.GetDocument().Snapshot().entities.size()),
        std::string("1"), "1つ");
    RequireEqual(session.GetDocument().Snapshot().entities.front().displayName,
        std::string("数値の線"), "名前");
    RequireEqual(std::to_string(session.Scene().curves.size()), std::string("1"),
        "吸着の相手になる");
    Require(session.GetDocument().CanUndo(), "元に戻せる");
    Require(!session.AddWire({}, false, "").committed, "線が無ければ入らない");
}

KACHA_V2_TEST_MAIN("direct_wire_entry")
