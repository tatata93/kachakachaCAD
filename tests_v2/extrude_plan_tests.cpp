// 選んだものから押し出しの意味を決める(オーナー指示 2026-09-14 の A〜E)。
#include "kachakacha/app/ExtrudePlan.h"
#include "kachakacha/app/FacePushPull.h"
#include "kachakacha/base/TestHarness.h"

#include <algorithm>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using kachakacha::v2::app::ExplainExtrudePlanJa;
using kachakacha::v2::app::ExtrudeInputKind;
using kachakacha::v2::app::ExtrudeSelectionFacts;
using kachakacha::v2::app::PlanExtrude;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::modeling::ExtrudeBooleanMode;
using kachakacha::v2::test::Require;

namespace {

[[nodiscard]] EntityId Ent(std::uint8_t number)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = number;
    return EntityId(kachakacha::v2::base::Uuid(bytes));
}

} // namespace

KACHA_V2_TEST(extrude_plan, 輪郭だけなら新しい立体を作る)
{
    // A。いちばん多い使い方。窓枠を描いて押し出す。
    ExtrudeSelectionFacts facts;
    facts.closedWires = 1;
    const auto plan = PlanExtrude(facts, {}, {Ent(4)}, false);
    Require(plan.kind == ExtrudeInputKind::ProfileOnly, "輪郭だけ");
    Require(plan.readyToPreview, "すぐ下見できる");
    Require(plan.targetSolid.IsNil(), "相手の立体は無い");
    Require(plan.operations.size() == 1, "選べる操作は1つ");
    Require(plan.defaultOperation == ExtrudeBooleanMode::NewPart, "新規立体");
}

KACHA_V2_TEST(extrude_plan, 立体だけなら何を選べばよいかを言う)
{
    // C。ここで「不正な入力です」とだけ言うのが、いちばんいけない。
    ExtrudeSelectionFacts facts;
    facts.solids = 1;
    const auto plan = PlanExtrude(facts, {Ent(1)}, {}, false);
    Require(plan.kind == ExtrudeInputKind::SolidOnly, "立体だけ");
    Require(!plan.readyToPreview, "まだ下見できない");
    Require(plan.targetSolid == Ent(1), "相手は決まっている");
    Require(plan.needsJa.find("選んでください") != std::string::npos, "何を選ぶかを言う");
    Require(plan.needsJa.find("面") != std::string::npos, "面か輪郭かを言う");
}

KACHA_V2_TEST(extrude_plan, 立体と輪郭は順番に関係なく役割が決まる)
{
    // D。どちらを先に選んだかは関係ない。型で決まる。
    ExtrudeSelectionFacts facts;
    facts.solids = 1;
    facts.closedWires = 1;
    const auto plan = PlanExtrude(facts, {Ent(1)}, {Ent(4)}, false);
    Require(plan.kind == ExtrudeInputKind::SolidAndProfile, "立体と輪郭");
    Require(plan.targetSolid == Ent(1), "立体が相手");
    Require(plan.profiles.size() == 1 && plan.profiles.front() == Ent(4), "輪郭が形");
    Require(plan.readyToPreview, "下見できる");
    // 立体に輪郭を当てるのは、たいてい窓や穴のため。
    Require(plan.defaultOperation == ExtrudeBooleanMode::SubtractFromPart, "既定は切削");
    Require(plan.operations.size() >= 3, "追加・切削・新規が選べる");
}

KACHA_V2_TEST(extrude_plan, 立体と面は押し引きになる)
{
    // E。
    ExtrudeSelectionFacts facts;
    facts.solids = 1;
    facts.faces = 1;
    const auto plan = PlanExtrude(facts, {Ent(1)}, {Ent(7)}, true);
    Require(plan.kind == ExtrudeInputKind::SolidAndFace, "立体と面");
    Require(plan.profileIsFace, "形は面");
    Require(plan.readyToPreview, "下見できる");
    Require(plan.defaultOperation == ExtrudeBooleanMode::AddToPart, "既定は追加");
}

KACHA_V2_TEST(extrude_plan, 面を1枚拾ったらその立体を押し引きする)
{
    // B。**2026-09-16 に既定を変えた。**
    // それまでは「新しい立体」だった。面をつまんで引いたのに離れた立体が
    // もう1つ出来るので、押し引きのつもりで押した人には何が起きたのか
    // 分からなかった(人の道の試験 HP-EX-03 が捕まえた)。
    // 面は立体の一部である。その立体が相手になる。
    ExtrudeSelectionFacts facts;
    facts.faces = 1;
    const auto plan = PlanExtrude(facts, {}, {Ent(7)}, true);
    Require(plan.kind == ExtrudeInputKind::SolidAndFace, "立体と面");
    Require(plan.targetSolid == Ent(7), "面が乗っている立体が相手");
    Require(plan.readyToPreview, "下見できる");
    Require(plan.defaultOperation == ExtrudeBooleanMode::AddToPart, "外へ引けば足す");
    // 「新しい部品」も残っている。既定を変えただけで、取り上げていない。
    Require(std::find(plan.operations.begin(), plan.operations.end(),
                ExtrudeBooleanMode::NewPart)
            != plan.operations.end(),
        "新しい部品も選べる");
}

KACHA_V2_TEST(extrude_plan, 何も選んでいなければ次にすることを言う)
{
    const auto plan = PlanExtrude(ExtrudeSelectionFacts{}, {}, {}, false);
    Require(plan.kind == ExtrudeInputKind::Nothing, "何も無い");
    Require(!plan.readyToPreview, "下見できない");
    Require(!plan.needsJa.empty(), "次にすることを言う");
    Require(plan.needsJa.find("選んでください") != std::string::npos, "選べと言う");
}

KACHA_V2_TEST(extrude_plan, 開いた輪郭は理由をつけて断る)
{
    ExtrudeSelectionFacts facts;
    facts.openWires = 2;
    const auto plan = PlanExtrude(facts, {}, {Ent(4), Ent(5)}, false);
    Require(plan.kind == ExtrudeInputKind::Unusable, "使えない");
    Require(plan.needsJa.find("閉じた輪郭") != std::string::npos, "何なら通るかを言う");
}

KACHA_V2_TEST(extrude_plan, 曲面は厚みを付けるほうへ案内する)
{
    ExtrudeSelectionFacts facts;
    facts.surfaces = 1;
    const auto plan = PlanExtrude(facts, {}, {}, false);
    Require(plan.kind == ExtrudeInputKind::Unusable, "使えない");
    Require(plan.needsJa.find("厚み") != std::string::npos, "代わりの道を教える");
}

KACHA_V2_TEST(extrude_plan, 面と輪郭を両方選んだら決められないと言う)
{
    ExtrudeSelectionFacts facts;
    facts.faces = 1;
    facts.closedWires = 1;
    const auto plan = PlanExtrude(facts, {}, {Ent(4)}, false);
    Require(plan.kind == ExtrudeInputKind::Unusable, "使えない");
    Require(plan.needsJa.find("片方だけ") != std::string::npos, "どうすればよいかを言う");
}

KACHA_V2_TEST(extrude_plan, 立体が2つなら加工する相手を絞らせる)
{
    ExtrudeSelectionFacts facts;
    facts.solids = 2;
    facts.closedWires = 1;
    const auto plan = PlanExtrude(facts, {Ent(1), Ent(2)}, {Ent(4)}, false);
    Require(plan.kind == ExtrudeInputKind::Unusable, "使えない");
    Require(plan.needsJa.find("1つ") != std::string::npos, "1つにしろと言う");
}

KACHA_V2_TEST(extrude_plan, 読み取った意味を日本語で見せられる)
{
    // 「CADがいまの選択をどう読んだか」は必ず見せる。
    ExtrudeSelectionFacts facts;
    facts.solids = 1;
    facts.closedWires = 1;
    const auto plan = PlanExtrude(facts, {Ent(1)}, {Ent(4)}, false);
    const std::string text = ExplainExtrudePlanJa(plan, "本体001", {"輪郭004"},
        ExtrudeBooleanMode::SubtractFromPart);
    Require(text.find("対象立体：本体001") != std::string::npos, "対象を出す");
    Require(text.find("輪郭：輪郭004") != std::string::npos, "輪郭を出す");
    Require(text.find("操作：") != std::string::npos, "操作を出す");
    // 内部の言葉は出さない。
    Require(text.find("Wire") == std::string::npos, "Wire と書かない");
    Require(text.find("Solid") == std::string::npos, "Solid と書かない");
}

KACHA_V2_TEST(extrude_plan, 足りないときの案内も同じ場所から出る)
{
    ExtrudeSelectionFacts facts;
    facts.solids = 1;
    const auto plan = PlanExtrude(facts, {Ent(1)}, {}, false);
    const std::string text = ExplainExtrudePlanJa(plan, "本体001", {},
        ExtrudeBooleanMode::SubtractFromPart);
    Require(text.find("対象立体：本体001") != std::string::npos, "決まった分は出す");
    Require(text.find("輪郭：") == std::string::npos, "空の欄は出さない");
    Require(text.find("操作：") == std::string::npos, "まだ操作は出さない");
    Require(text.find("選んでください") != std::string::npos, "次にすることを出す");
}

KACHA_V2_TEST(extrude_plan, どの組み合わせにも名前がある)
{
    constexpr ExtrudeInputKind kAll[] = {ExtrudeInputKind::Nothing,
        ExtrudeInputKind::ProfileOnly, ExtrudeInputKind::FaceOnly,
        ExtrudeInputKind::SolidOnly, ExtrudeInputKind::SolidAndProfile,
        ExtrudeInputKind::SolidAndFace, ExtrudeInputKind::Unusable};
    for (const auto kind : kAll) {
        Require(!kachakacha::v2::app::ExtrudeInputKindNameJa(kind).empty(), "名前がある");
    }
}

KACHA_V2_TEST(extrude_plan, 面を外へ引くと足しになり中へ押すと引きになる)
{
    using kachakacha::v2::app::PlanFacePushPull;
    // EX-02。作る人は「外へ引けば増える、中へ押せば減る」と思って引く。
    const auto out = PlanFacePushPull(3.5);
    Require(out.ready, "外へ引けば作れる");
    Require(out.booleanMode == ExtrudeBooleanMode::AddToPart, "外は足し");
    Require(!out.reversed, "外は向きそのまま");
    Require(out.distanceMm > 3.49 && out.distanceMm < 3.51, "距離はそのまま");

    const auto in = PlanFacePushPull(-2.0);
    Require(in.ready, "中へ押しても作れる");
    Require(in.booleanMode == ExtrudeBooleanMode::SubtractFromPart, "中は引き");
    Require(in.reversed, "中は向きを反転する");
    Require(in.distanceMm > 1.99 && in.distanceMm < 2.01,
        "押し出しへ渡す距離は必ず正にする");
}

KACHA_V2_TEST(extrude_plan, 0mmの押し引きは作らずに次を案内する)
{
    using kachakacha::v2::app::PlanFacePushPull;
    // 0mm は形を変えない。「できた」と言ってはならない。
    const auto none = PlanFacePushPull(0.0);
    Require(!none.ready, "0mm では作らない");
    Require(!none.messageJa.empty(), "次に何をすればよいかを言う");
    Require(none.messageJa.find("矢印") != std::string::npos, "矢印を引くよう案内する");
}

KACHA_V2_TEST(extrude_plan, 押し引きの結果を作る人の言葉で言う)
{
    using kachakacha::v2::app::DescribeFacePushPullJa;
    using kachakacha::v2::app::PlanFacePushPull;
    const auto out = DescribeFacePushPullJa(PlanFacePushPull(5.0));
    Require(out.find("外へ") != std::string::npos, "外へ引くと言う");
    Require(out.find("増え") != std::string::npos, "材料が増えると言う");
    Require(out.find("5.00") != std::string::npos, "距離を出す");
    const auto in = DescribeFacePushPullJa(PlanFacePushPull(-5.0));
    Require(in.find("中へ") != std::string::npos, "中へ押すと言う");
    Require(in.find("減り") != std::string::npos, "材料が減ると言う");
    // 内部の言葉を出さない。
    Require(out.find("Boolean") == std::string::npos
            && out.find("Subtract") == std::string::npos,
        "内部の言葉を出さない");
}

KACHA_V2_TEST_MAIN("extrude_plan_tests")
