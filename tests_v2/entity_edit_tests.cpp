// 選んだものの数値編集(app/EntityEdit.h)。V1 の「編集」タブ。
#include "kachakacha/app/EntityEdit.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::app::BuildWireFromEditFields;
using kachakacha::v2::app::EditedPlaneDefinition;
using kachakacha::v2::app::EditedWireDefinition;
using kachakacha::v2::app::MeasureLine;
using kachakacha::v2::app::NothingToEditDiagnostic;
using kachakacha::v2::app::PlaneEditFields;
using kachakacha::v2::app::PlaneEditFieldsOf;
using kachakacha::v2::app::WireEditFields;
using kachakacha::v2::app::WireEditFieldsOf;
using kachakacha::v2::app::WireEditShape;
using kachakacha::v2::domain::CreateWireDefinition;
using kachakacha::v2::domain::CreateWorkPlaneDefinition;
using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::WorkPlaneFrame;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れる");
    return made.Value();
}

[[nodiscard]] WorkPlaneFrame TopFrame()
{
    return WorkPlaneFrame{};
}

} // namespace

KACHA_V2_TEST(entity_edit, 直線の欄は始点と終点で出て置き換えると線のIDが残る)
{
    kachakacha::v2::base::DeterministicIdGenerator ids;
    CreateWireDefinition wire;
    wire.segments = {Line({0, 0, 0}, {10, 0, 0})};
    wire.segmentIds = {ids.NextTyped<kachakacha::v2::base::IdKind::Segment>()};
    const auto fields = WireEditFieldsOf(wire);
    Require(fields.HasValue(), "欄が出る");
    Require(fields.Value().shape == WireEditShape::Line, "直線");
    RequireEqual(std::to_string(fields.Value().points.size()), std::string("2"), "点2つ");

    WireEditFields edited = fields.Value();
    edited.points[1] = {0, 20, 0};
    const auto result = EditedWireDefinition(wire, edited, TopFrame(), ids);
    Require(result.HasValue(), "置き換えられる");
    RequireNear(result.Value().segments.front().EndPoint().y, 20.0, 1e-9, "終点 y");
    Require(result.Value().segmentIds == wire.segmentIds, "線の ID は同じ");
}

KACHA_V2_TEST(entity_edit, 長さと平面内角度で終点を置き直す)
{
    WireEditFields fields;
    fields.shape = WireEditShape::Line;
    fields.points = {{0, 0, 0}, {10, 0, 0}};
    fields.lengthMm = 50.0;
    fields.angleDeg = 90.0;
    const auto made = BuildWireFromEditFields(fields, TopFrame());
    Require(made.HasValue(), "作れる");
    const Vector3 end = made.Value().front().EndPoint();
    RequireNear(end.x, 0.0, 1e-9, "x");
    RequireNear(end.y, 50.0, 1e-9, "y は 90° 方向");
    // 長さだけなら向きは元のまま。
    fields.angleDeg.reset();
    const auto onlyLength = BuildWireFromEditFields(fields, TopFrame());
    Require(onlyLength.HasValue(), "作れる");
    RequireNear(onlyLength.Value().front().EndPoint().x, 50.0, 1e-9, "x 方向へ 50");
}

KACHA_V2_TEST(entity_edit, 長さ0は断る)
{
    WireEditFields fields;
    fields.shape = WireEditShape::Line;
    fields.points = {{0, 0, 0}, {10, 0, 0}};
    fields.lengthMm = 0.0;
    const auto made = BuildWireFromEditFields(fields, TopFrame());
    Require(!made.HasValue(), "作れない");
    RequireEqual(made.Diagnostics().front().code, std::string("UI-E004"), "理由");
}

KACHA_V2_TEST(entity_edit, 折れ線は点の並びで出て円弧が混ざると断る)
{
    CreateWireDefinition polyline;
    polyline.segments = {Line({0, 0, 0}, {10, 0, 0}), Line({10, 0, 0}, {10, 10, 0})};
    const auto fields = WireEditFieldsOf(polyline);
    Require(fields.HasValue(), "欄が出る");
    Require(fields.Value().shape == WireEditShape::Polyline, "折れ線");
    RequireEqual(std::to_string(fields.Value().points.size()), std::string("3"), "点3つ");

    CreateWireDefinition mixed = polyline;
    const auto arc = CurveSegment::MakeCircularArc({10, 10, 0}, {0, 0, 1}, {1, 0, 0}, 5.0,
        0.0, 1.0);
    Require(arc.HasValue(), "円弧が作れる");
    mixed.segments.push_back(arc.Value());
    const auto refused = WireEditFieldsOf(mixed);
    Require(!refused.HasValue(), "混ざると断る");
    RequireEqual(refused.Diagnostics().front().code, std::string("UI-E003"), "理由");
}

KACHA_V2_TEST(entity_edit, 円弧の欄は中心と軸と角度で出て半径を変えられる)
{
    CreateWireDefinition wire;
    const auto arc = CurveSegment::MakeCircularArc({5, 5, 0}, {0, 0, 1}, {1, 0, 0}, 10.0,
        0.0, 1.5707963267948966);
    Require(arc.HasValue(), "円弧が作れる");
    wire.segments = {arc.Value()};
    const auto fields = WireEditFieldsOf(wire);
    Require(fields.HasValue(), "欄が出る");
    Require(fields.Value().shape == WireEditShape::CircularArc, "円弧");
    RequireNear(fields.Value().radiusMm, 10.0, 1e-9, "半径");
    RequireNear(fields.Value().sweepAngleDeg, 90.0, 1e-9, "中心角は度");
    RequireNear(fields.Value().vAxis.y, 1.0, 1e-9, "円の Y 軸");

    WireEditFields edited = fields.Value();
    edited.radiusMm = 20.0;
    const auto made = BuildWireFromEditFields(edited, TopFrame());
    Require(made.HasValue(), "作れる");
    Require(made.Value().front().Kind() == CurveKind::CircularArc, "円弧のまま");
    RequireNear(made.Value().front().Radius(), 20.0, 1e-9, "半径 20");
    edited.radiusMm = 0.0;
    Require(!BuildWireFromEditFields(edited, TopFrame()).HasValue(), "半径0は断る");
}

KACHA_V2_TEST(entity_edit, 平面は原点と法線と平面内Xで置き換わり原点面は断る)
{
    CreateWorkPlaneDefinition plane;
    plane.method = 1;
    plane.inputs.push_back(kachakacha::v2::base::EntityId{});
    PlaneEditFields fields = PlaneEditFieldsOf(plane);
    fields.origin = {0, 0, 30};
    fields.normal = {0, 0, 2};   // 長さは気にしない。core が正規化する。
    fields.uDirection = {1, 1, 0};
    const auto edited = EditedPlaneDefinition(plane, fields, GeometryTolerance{});
    Require(edited.HasValue(), "置き換えられる");
    RequireEqual(std::to_string(edited.Value().method),
        std::to_string(static_cast<int>(kachakacha::v2::modeling::WorkPlaneMethod::PointNormal)),
        "作り方は数値指定になる");
    Require(edited.Value().inputs.empty(), "入力は要らなくなる");
    RequireNear(edited.Value().origin.z, 30.0, 1e-9, "原点");
    RequireNear(edited.Value().normal.z, 1.0, 1e-9, "法線は単位");
    RequireNear(edited.Value().uDirection.Length(), 1.0, 1e-9, "u 軸は単位");

    fields.uDirection = {0, 0, 1};   // 法線と平行
    Require(!EditedPlaneDefinition(plane, fields, GeometryTolerance{}).HasValue(),
        "u 軸が法線と平行なら断る");

    CreateWorkPlaneDefinition origin = plane;
    origin.isOriginPlane = true;
    const auto refused = EditedPlaneDefinition(origin, PlaneEditFieldsOf(origin),
        GeometryTolerance{});
    Require(!refused.HasValue(), "原点面は断る");
    RequireEqual(refused.Diagnostics().front().code, std::string("UI-E002"), "理由");
}

KACHA_V2_TEST(entity_edit, 直線の長さと角度を測る)
{
    const auto measure = MeasureLine({0, 0, 0}, {0, 30, 0}, TopFrame());
    RequireNear(measure.lengthMm, 30.0, 1e-9, "長さ");
    RequireNear(measure.angleDeg, 90.0, 1e-9, "角度");
    RequireEqual(NothingToEditDiagnostic(0).code, std::string("UI-E001"), "選んでいない");
    RequireEqual(NothingToEditDiagnostic(3).code, std::string("UI-E001"), "多すぎる");
}

KACHA_V2_TEST_MAIN("entity_edit")
