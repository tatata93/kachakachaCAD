// 押し出しで利用者が決めることと、その組み合わせが通るか(app/ExtrudeOptions.h)。
//
// core は向き7通り・終端5通り・出力3通り・部品演算3通りを持っているのに、
// 画面はそれぞれ1通りに固定していた。「ワイヤーだけ作る」も「あの面まで押す」も
// 選べなかった。ここは、選べるようにしたうえで、通らない組み合わせを断る。
#include "kachakacha/app/ExtrudeOptions.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::app::ExtrudeBooleanNameJa;
using kachakacha::v2::app::ExtrudeChoice;
using kachakacha::v2::app::ExtrudeDirectionNameJa;
using kachakacha::v2::app::ExtrudeExtentNameJa;
using kachakacha::v2::app::ExtrudeFacts;
using kachakacha::v2::app::ExtrudeSummaryJa;
using kachakacha::v2::app::ExtentUsesDistance;
using kachakacha::v2::app::ExtentUsesSecondDistance;
using kachakacha::v2::app::ExtentUsesTarget;
using kachakacha::v2::app::ToExtrudeRequest;
using kachakacha::v2::app::ValidateExtrudeChoice;
using kachakacha::v2::modeling::ExtrudeBooleanMode;
using kachakacha::v2::modeling::ExtrudeDirectionMode;
using kachakacha::v2::modeling::ExtrudeExtentMode;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

[[nodiscard]] ExtrudeFacts ClosedOne()
{
    ExtrudeFacts facts;
    facts.closedProfiles = 1;
    return facts;
}

[[nodiscard]] std::string FirstCode(
    const kachakacha::v2::base::Result<ExtrudeChoice>& result)
{
    return result.Diagnostics().empty() ? std::string() : result.Diagnostics().front().code;
}

} // namespace

KACHA_V2_TEST(extrude_options, 既定は閉じた輪郭から部品を作る)
{
    const auto ok = ValidateExtrudeChoice(ExtrudeChoice{}, ClosedOne());
    Require(ok.HasValue(), "通る");
}

KACHA_V2_TEST(extrude_options, 何も作らない指定は断る)
{
    ExtrudeChoice choice;
    choice.makePart = false;
    const auto made = ValidateExtrudeChoice(choice, ClosedOne());
    Require(!made.HasValue(), "断る");
    RequireEqual(FirstCode(made), std::string("EXT-U001"), "何を作るかが無いと言う");
}

KACHA_V2_TEST(extrude_options, 開いた輪郭でもワイヤーだけなら作れる)
{
    // ここが無かったので「押し出しと同じ要領でワイヤだけ作る」が出来なかった。
    ExtrudeChoice choice;
    choice.makePart = false;
    choice.makeEndProfileWire = true;
    ExtrudeFacts facts;
    facts.openProfiles = 1;
    Require(ValidateExtrudeChoice(choice, facts).HasValue(), "通る");
}

KACHA_V2_TEST(extrude_options, 開いた輪郭から部品は作れない)
{
    ExtrudeFacts facts;
    facts.openProfiles = 1;
    const auto made = ValidateExtrudeChoice(ExtrudeChoice{}, facts);
    Require(!made.HasValue(), "断る");
    RequireEqual(FirstCode(made), std::string("EXT-U002"), "閉じた輪郭が要ると言う");
}

KACHA_V2_TEST(extrude_options, 面まで押すなら相手が要る)
{
    ExtrudeChoice choice;
    choice.extent = ExtrudeExtentMode::ToTarget;
    const auto made = ValidateExtrudeChoice(choice, ClosedOne());
    Require(!made.HasValue(), "断る");
    RequireEqual(FirstCode(made), std::string("EXT-U003"), "相手が無いと言う");
    choice.targetEntityId = kachakacha::v2::base::EntityId{};
    Require(ValidateExtrudeChoice(choice, ClosedOne()).HasValue(), "相手があれば通る");
}

KACHA_V2_TEST(extrude_options, 足す引くは相手の部品を明示して選ぶ)
{
    // 近い部品を勝手に選ばない。どれに足したのか分からなくなる。
    ExtrudeChoice choice;
    choice.booleanMode = ExtrudeBooleanMode::AddToPart;
    const auto made = ValidateExtrudeChoice(choice, ClosedOne());
    Require(!made.HasValue(), "断る");
    RequireEqual(FirstCode(made), std::string("EXT-U004"), "相手を選べと言う");
    choice.hasSelectedPart = true;
    Require(ValidateExtrudeChoice(choice, ClosedOne()).HasValue(), "選んでいれば通る");
}

KACHA_V2_TEST(extrude_options, 距離0は断るがワイヤーだけなら承知の上で通す)
{
    ExtrudeChoice choice;
    choice.distanceMm = 0.0;
    const auto refused = ValidateExtrudeChoice(choice, ClosedOne());
    Require(!refused.HasValue(), "部品を作るなら断る");
    RequireEqual(FirstCode(refused), std::string("EXT-U005"), "距離が0と言う");
    choice.makePart = false;
    choice.makeEndProfileWire = true;
    Require(!ValidateExtrudeChoice(choice, ClosedOne()).HasValue(),
        "承知していなければ、ワイヤーだけでも断る");
    choice.zeroDistanceConfirmed = true;
    Require(ValidateExtrudeChoice(choice, ClosedOne()).HasValue(), "承知していれば通る");
}

KACHA_V2_TEST(extrude_options, 全部貫くのは引くときだけ)
{
    ExtrudeChoice choice;
    choice.extent = ExtrudeExtentMode::ThroughAll;
    const auto made = ValidateExtrudeChoice(choice, ClosedOne());
    Require(!made.HasValue(), "断る");
    RequireEqual(FirstCode(made), std::string("EXT-U006"), "引くときだけと言う");
    choice.booleanMode = ExtrudeBooleanMode::SubtractFromPart;
    choice.hasSelectedPart = true;
    Require(ValidateExtrudeChoice(choice, ClosedOne()).HasValue(), "引くなら通る");
}

KACHA_V2_TEST(extrude_options, どの終端でどの欄を出すかが決まっている)
{
    Require(ExtentUsesDistance(ExtrudeExtentMode::Distance), "距離は距離を使う");
    Require(ExtentUsesDistance(ExtrudeExtentMode::SymmetricDistance), "対称も使う");
    Require(!ExtentUsesDistance(ExtrudeExtentMode::ToTarget), "面までは距離を使わない");
    Require(!ExtentUsesDistance(ExtrudeExtentMode::ThroughAll), "貫くも使わない");
    Require(ExtentUsesSecondDistance(ExtrudeExtentMode::TwoDistances), "両方向は2つ");
    Require(!ExtentUsesSecondDistance(ExtrudeExtentMode::Distance), "片方向は1つ");
    Require(ExtentUsesTarget(ExtrudeExtentMode::ToTarget), "面までは相手を使う");
    Require(!ExtentUsesTarget(ExtrudeExtentMode::Distance), "距離は相手を使わない");
}

KACHA_V2_TEST(extrude_options, 並びと名前がすべてそろっている)
{
    Require(kachakacha::v2::app::ExtrudeDirections().size() == 7, "向きは7通り");
    Require(kachakacha::v2::app::ExtrudeExtents().size() == 5, "終端は5通り");
    Require(kachakacha::v2::app::ExtrudeBooleans().size() == 3, "部品演算は3通り");
    for (const auto mode : kachakacha::v2::app::ExtrudeDirections()) {
        Require(ExtrudeDirectionNameJa(mode) != std::string_view("不明"), "向きに名前");
    }
    for (const auto mode : kachakacha::v2::app::ExtrudeExtents()) {
        Require(ExtrudeExtentNameJa(mode) != std::string_view("不明"), "終端に名前");
    }
    for (const auto mode : kachakacha::v2::app::ExtrudeBooleans()) {
        Require(ExtrudeBooleanNameJa(mode) != std::string_view("不明"), "演算に名前");
    }
}

KACHA_V2_TEST(extrude_options, 決めたことが一文になる)
{
    ExtrudeChoice choice;
    choice.distanceMm = 1.5;
    const std::string text = ExtrudeSummaryJa(choice);
    Require(text.find("作業平面に垂直") != std::string::npos, "向きが出る");
    Require(text.find("1.5mm") != std::string::npos, "距離が出る");
    Require(text.find("部品") != std::string::npos, "作るものが出る");
    choice.reversed = true;
    Require(ExtrudeSummaryJa(choice).find("逆向き") != std::string::npos, "逆向きが出る");
}

KACHA_V2_TEST(extrude_options, 決めたことがそのまま要求へ写る)
{
    // 写し方を2か所に書くと、必ず食い違う。
    ExtrudeChoice choice;
    choice.direction = ExtrudeDirectionMode::WorldZ;
    choice.reversed = true;
    choice.extent = ExtrudeExtentMode::TwoDistances;
    choice.distanceMm = 2.0;
    choice.secondDistanceMm = 3.0;
    choice.makeEndProfileWire = true;
    choice.booleanMode = ExtrudeBooleanMode::SubtractFromPart;
    choice.hasSelectedPart = true;
    const auto request = ToExtrudeRequest(choice, {},
        kachakacha::v2::modeling::WorkPlaneFrame{}, std::nullopt);
    Require(request.directionMode == ExtrudeDirectionMode::WorldZ, "向き");
    Require(request.reversed, "逆向き");
    Require(request.extent == ExtrudeExtentMode::TwoDistances, "終端");
    Require(request.distanceMm == 2.0 && request.secondDistanceMm == 3.0, "2つの距離");
    Require(request.outputs.part && request.outputs.endProfileWire, "作るもの");
    Require(request.booleanMode == ExtrudeBooleanMode::SubtractFromPart, "演算");
    Require(request.hasSelectedPart, "相手を選んでいる");
    Require(request.targetKind == kachakacha::v2::modeling::ExtrudeTargetKind::None,
        "面までではないので相手は無い");
}

KACHA_V2_TEST(extrude_options, 面までのときは相手の平面が写る)
{
    ExtrudeChoice choice;
    choice.extent = ExtrudeExtentMode::ToTarget;
    choice.targetEntityId = kachakacha::v2::base::EntityId{};
    kachakacha::v2::modeling::WorkPlaneFrame target;
    target.origin = kachakacha::v2::geometry::Vector3{0.0, 0.0, 10.0};
    const auto request = ToExtrudeRequest(choice, {},
        kachakacha::v2::modeling::WorkPlaneFrame{}, target);
    Require(request.targetKind == kachakacha::v2::modeling::ExtrudeTargetKind::Plane,
        "相手は平面");
    Require(request.targetPlane.origin.z == 10.0, "その平面が写っている");
}

KACHA_V2_TEST_MAIN("extrude_options_tests")
