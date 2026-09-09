// 作業平面の作り方11通りを、画面から選べるようにするための材料。
//
// 画面は Standard しか作れず、原点を通らない平面が作れなかった。
// 船体や車体の station ごとの断面を描くには、平面から離した面が要る。
#include "kachakacha/app/WorkPlaneOptions.h"
#include "kachakacha/base/TestHarness.h"

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

KACHA_V2_TEST(work_plane_options, 11通りすべてが並びに入っている)
{
    // 台帳の説明文が「11通りあります」と書いているので、11通り出さねばならない。
    Require(WorkPlaneMethods().size() == 11, "11通り");
    for (const auto method : WorkPlaneMethods()) {
        Require(WorkPlaneMethodNameJa(method) != std::string_view("不明"), "名前がある");
    }
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

KACHA_V2_TEST(work_plane_options, 曲線に直角には線と点が要る)
{
    // station ごとの断面を置くのに、いちばん効くのがこれである。
    const auto needs = NeedsOf(WorkPlaneMethod::NormalToCurveAtPoint);
    Require(needs.edges == 1 && needs.points == 1, "線1本と点1つ");
    WorkPlaneFacts facts;
    facts.edges = 1;
    Require(!ValidateWorkPlaneChoice(WorkPlaneMethod::NormalToCurveAtPoint, facts)
                 .HasValue(),
        "点が無ければ断る");
    facts.points = 1;
    Require(ValidateWorkPlaneChoice(WorkPlaneMethod::NormalToCurveAtPoint, facts)
                .HasValue(),
        "そろえば作れる");
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
