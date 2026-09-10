// 作業平面の作り方12通りを、画面から選べるようにするための材料。
//
// 画面は Standard しか作れず、原点を通らない平面が作れなかった。
// 船体や車体の station ごとの断面を描くには、平面から離した面が要る。
#include "kachakacha/app/WorkPlaneOptions.h"
#include "kachakacha/base/TestHarness.h"

#include <cmath>
#include <string>

using kachakacha::v2::app::NeedsOf;
using kachakacha::v2::app::ValidateWorkPlaneChoice;
using kachakacha::v2::app::WorkPlaneFacts;
using kachakacha::v2::app::WorkPlaneMethods;
using kachakacha::v2::app::WorkPlaneNeedsJa;
using kachakacha::v2::modeling::WorkPlaneMethod;
using kachakacha::v2::modeling::WorkPlaneMethodNameJa;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

KACHA_V2_TEST(work_plane_options, 12通りすべてが並びに入っている)
{
    // 台帳の説明文が「12通りあります」と書いているので、12通り出さねばならない。
    // 12通り目は V1 の「位置と向きを数値指定」(plane_point_normal)。
    Require(WorkPlaneMethods().size() == 12, "12通り");
    for (const auto method : WorkPlaneMethods()) {
        Require(WorkPlaneMethodNameJa(method) != std::string_view("不明"), "名前がある");
    }
}

KACHA_V2_TEST(work_plane_options, 位置と向きを数値で指定した平面が作れる)
{
    // V1 の plane_point_normal。選ぶものは無く、数だけで作る。
    Require(!NeedsOf(WorkPlaneMethod::PointNormal).usesStandardKind, "種類は使わない");
    Require(ValidateWorkPlaneChoice(WorkPlaneMethod::PointNormal, WorkPlaneFacts{}).HasValue(),
        "何も選ばずに通る");
    kachakacha::v2::modeling::WorkPlaneRequest request;
    request.method = WorkPlaneMethod::PointNormal;
    request.origin = {4.0, 0.0, 0.0};
    request.normal = {1.0, 0.0, 0.0};
    request.uHint = {0.0, 1.0, 0.0};
    const auto made = kachakacha::v2::modeling::BuildWorkPlane(request,
        kachakacha::v2::geometry::GeometryTolerance{});
    Require(made.HasValue(), "作れる");
    Require((made.Value().origin - kachakacha::v2::geometry::Vector3{4.0, 0.0, 0.0}).Length()
            < 1e-12, "通過点");
    Require(std::abs(made.Value().normal.x - 1.0) < 1e-12, "法線 X");
    Require(std::abs(made.Value().uAxis.y - 1.0) < 1e-12, "横方向 Y");
    request.uHint = {1.0, 0.0, 0.0};   // 法線と平行
    Require(!kachakacha::v2::modeling::BuildWorkPlane(request,
                 kachakacha::v2::geometry::GeometryTolerance{})
                 .HasValue(),
        "横方向が法線と平行なら断る");
}

KACHA_V2_TEST(work_plane_options, 標準面は何も選ばずに作れる)
{
    Require(NeedsOf(WorkPlaneMethod::Standard).usesStandardKind, "種類を使う");
    Require(ValidateWorkPlaneChoice(WorkPlaneMethod::Standard, WorkPlaneFacts{})
                .HasValue(),
        "何も選ばなくても作れる");
}

KACHA_V2_TEST(work_plane_options, 平面から離すには平面が1つ要る)
{
    const auto needs = NeedsOf(WorkPlaneMethod::OffsetFromPlane);
    Require(needs.planes == 1, "平面1つ");
    Require(needs.usesOffset, "距離を使う");
    Require(!ValidateWorkPlaneChoice(WorkPlaneMethod::OffsetFromPlane, WorkPlaneFacts{})
                 .HasValue(),
        "選んでいなければ断る");
    WorkPlaneFacts facts;
    facts.planes = 1;
    Require(ValidateWorkPlaneChoice(WorkPlaneMethod::OffsetFromPlane, facts).HasValue(),
        "選んでいれば作れる");
}

KACHA_V2_TEST(work_plane_options, 3点を通るには点が3つ要る)
{
    WorkPlaneFacts facts;
    facts.points = 2;
    Require(!ValidateWorkPlaneChoice(WorkPlaneMethod::ThreePoints, facts).HasValue(),
        "2つでは断る");
    facts.points = 3;
    Require(ValidateWorkPlaneChoice(WorkPlaneMethod::ThreePoints, facts).HasValue(),
        "3つなら作れる");
}

KACHA_V2_TEST(work_plane_options, 曲線に直角には線が要り位置は欄か点で決める)
{
    // station ごとの断面を置くのに、いちばん効くのがこれである。
    // V1 と同じく「線上位置」の欄(0〜1)で決める。点を選んでいれば最寄りの位置。
    const auto needs = NeedsOf(WorkPlaneMethod::NormalToCurveAtPoint);
    Require(needs.edges == 1 && needs.points == 0, "線1本。点は無くてよい");
    WorkPlaneFacts facts;
    Require(!ValidateWorkPlaneChoice(WorkPlaneMethod::NormalToCurveAtPoint, facts)
                 .HasValue(),
        "線が無ければ断る");
    facts.edges = 1;
    Require(ValidateWorkPlaneChoice(WorkPlaneMethod::NormalToCurveAtPoint, facts)
                .HasValue(),
        "線があれば作れる");
    kachakacha::v2::app::WorkPlaneChoice choice;
    choice.method = WorkPlaneMethod::NormalToCurveAtPoint;
    choice.curveParameter = 0.25;
    kachakacha::v2::app::WorkPlaneMaterials materials;
    materials.edges.push_back(kachakacha::v2::geometry::CurveSegment::MakeLine(
        {0.0, 0.0, 0.0}, {100.0, 0.0, 0.0}).Value());
    const auto byField = kachakacha::v2::app::BuildWorkPlaneRequest(choice, materials);
    Require(byField.HasValue() && std::abs(byField.Value().curveParameter - 0.25) < 1e-12,
        "欄の位置が渡る");
    materials.points.push_back({80.0, 5.0, 0.0});
    const auto byPoint = kachakacha::v2::app::BuildWorkPlaneRequest(choice, materials);
    Require(byPoint.HasValue() && std::abs(byPoint.Value().curveParameter - 0.8) < 1e-9,
        "点を選んでいれば最寄りの位置");
}

KACHA_V2_TEST(work_plane_options, 3点と回転軸は選んでいなければ数の欄で作る)
{
    kachakacha::v2::app::WorkPlaneChoice choice;
    choice.method = WorkPlaneMethod::ThreePoints;
    choice.threePoints = {kachakacha::v2::geometry::Vector3{0.0, 0.0, 5.0},
        kachakacha::v2::geometry::Vector3{10.0, 0.0, 5.0},
        kachakacha::v2::geometry::Vector3{0.0, 10.0, 5.0}};
    const auto three = kachakacha::v2::app::BuildWorkPlaneRequest(choice,
        kachakacha::v2::app::WorkPlaneMaterials{});
    Require(three.HasValue() && three.Value().points.size() == 3, "3点は欄から");
    choice.method = WorkPlaneMethod::AngleAboutEdge;
    choice.axisPoint = {0.0, 0.0, 0.0};
    choice.axisDirection = {0.0, 1.0, 0.0};
    choice.angleDeg = 30.0;
    kachakacha::v2::app::WorkPlaneMaterials materials;
    materials.referencePlane = kachakacha::v2::modeling::StandardPlane(
        kachakacha::v2::modeling::StandardPlaneKind::XY);
    const auto tilted = kachakacha::v2::app::BuildWorkPlaneRequest(choice, materials);
    Require(tilted.HasValue() && tilted.Value().edges.size() == 1, "軸は欄から");
    choice.axisDirection = {0.0, 0.0, 0.0};
    Require(!kachakacha::v2::app::BuildWorkPlaneRequest(choice, materials).HasValue(),
        "向き 0 の軸は断る");
    choice.name = "断面 station 3";
    Require(kachakacha::v2::app::WorkPlaneDisplayName(choice) == "断面 station 3", "名前");
}

KACHA_V2_TEST(work_plane_options, 辺まわりに角度は平面と線と角度を使う)
{
    const auto needs = NeedsOf(WorkPlaneMethod::AngleAboutEdge);
    Require(needs.planes == 1 && needs.edges == 1, "平面1つと線1本");
    Require(needs.usesAngle, "角度を使う");
    Require(!needs.usesOffset, "距離は使わない");
}

KACHA_V2_TEST(work_plane_options, 何を選べばよいかを日本語で言う)
{
    const std::string text = WorkPlaneNeedsJa(WorkPlaneMethod::MidBetweenPlanes);
    Require(text.find("作業平面を2つ") != std::string::npos, "数が出る");
    RequireEqual(WorkPlaneNeedsJa(WorkPlaneMethod::Standard),
        std::string("選ぶものはありません。"), "標準面は何も要らない");
}

KACHA_V2_TEST(work_plane_options, 断るときは何が足りないかを言う)
{
    const auto made = ValidateWorkPlaneChoice(WorkPlaneMethod::TwoEdges, WorkPlaneFacts{});
    Require(!made.HasValue(), "断る");
    Require(!made.Diagnostics().empty(), "理由がある");
    RequireEqual(made.Diagnostics().front().code, std::string("UI-W001"), "UI-W001");
    Require(made.Diagnostics().front().detailsJa.find("線を2つ") != std::string::npos,
        "足りないものを言う");
}

KACHA_V2_TEST(work_plane_options, 多く選んでいても作れる)
{
    // ちょうどでなければ断る、にすると、線を引きながら平面を作れなくなる。
    WorkPlaneFacts facts;
    facts.points = 5;
    Require(ValidateWorkPlaneChoice(WorkPlaneMethod::ThreePoints, facts).HasValue(),
        "多い分には作れる");
}

KACHA_V2_TEST_MAIN("work_plane_options_tests")
