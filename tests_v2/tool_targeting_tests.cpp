// 道具と相手の順番(オーナー指摘「ツールを選ぶ→対象を選択の順で行う」)。
#include "kachakacha/app/ToolTargeting.h"
#include "kachakacha/base/TestHarness.h"

using kachakacha::v2::app::ClickPicksTarget;
using kachakacha::v2::app::PendingAction;
using kachakacha::v2::app::PendingCommandAction;
using kachakacha::v2::app::PredicateIsExact;
using kachakacha::v2::app::SelectionPredicate;
using kachakacha::v2::app::ToolAfterModeChange;
using kachakacha::v2::app::ToolNeedsTargetWire;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::test::Require;

KACHA_V2_TEST(tool_targeting, モードを変えたら選択道具へ戻る)
{
    // 前の道具が残っていると、部品モードへ移った直後に画面を押して線が引ける。
    Require(ToolAfterModeChange() == DrawingTool::Select, "選択へ戻る");
}

KACHA_V2_TEST(tool_targeting, 動かす道具は相手が要る)
{
    for (const DrawingTool tool : {DrawingTool::Move, DrawingTool::Copy,
             DrawingTool::Mirror, DrawingTool::Rotate}) {
        Require(ToolNeedsTargetWire(tool), "相手が要る");
    }
}

KACHA_V2_TEST(tool_targeting, 線を引く道具は相手が要らない)
{
    for (const DrawingTool tool : {DrawingTool::Line, DrawingTool::Rectangle,
             DrawingTool::Circle, DrawingTool::Arc, DrawingTool::Select,
             DrawingTool::Measure}) {
        Require(!ToolNeedsTargetWire(tool), "相手は要らない");
    }
}

KACHA_V2_TEST(tool_targeting, 何も選んでいなければ最初の押しで相手を選ぶ)
{
    Require(ClickPicksTarget(DrawingTool::Move, false, 0), "1回目は相手を選ぶ");
}

KACHA_V2_TEST(tool_targeting, 選んでいれば最初から点を置く)
{
    Require(!ClickPicksTarget(DrawingTool::Move, true, 0), "選んでいれば点");
}

KACHA_V2_TEST(tool_targeting, 点を置き始めたら選び直させない)
{
    // 置いた点が無駄になる。
    Require(!ClickPicksTarget(DrawingTool::Move, false, 1), "2回目からは点");
}

KACHA_V2_TEST(tool_targeting, 線を引く道具は相手を選ばない)
{
    Require(!ClickPicksTarget(DrawingTool::Line, false, 0), "そのまま点を置く");
}

KACHA_V2_TEST(tool_targeting, ちょうどの条件はそろえば走る)
{
    Require(PredicateIsExact(SelectionPredicate::TwoWireChains), "ちょうど2");
    Require(PredicateIsExact(SelectionPredicate::OnePart), "ちょうど1");
    Require(PredicateIsExact(SelectionPredicate::OneClosedProfile), "ちょうど1");
}

KACHA_V2_TEST(tool_targeting, 以上の条件は足す余地がある)
{
    Require(!PredicateIsExact(SelectionPredicate::OneOrMoreWires), "1つ以上");
    Require(!PredicateIsExact(SelectionPredicate::TwoOrMoreWires), "2つ以上");
    Require(!PredicateIsExact(SelectionPredicate::OneOrMorePatterns), "1つ以上");
}

KACHA_V2_TEST(tool_targeting, 足りなければ待つ)
{
    Require(PendingCommandAction(SelectionPredicate::TwoWireChains, false, false)
            == PendingAction::Wait,
        "待つ");
    Require(PendingCommandAction(SelectionPredicate::OneOrMoreWires, false, true)
            == PendingAction::Wait,
        "そろう前は、承知していても待つ");
}

KACHA_V2_TEST(tool_targeting, ちょうどそろえばすぐ走る)
{
    Require(PendingCommandAction(SelectionPredicate::TwoWireChains, true, false)
            == PendingAction::RunNow,
        "2本目で走る");
}

KACHA_V2_TEST(tool_targeting, 足せる条件は言われるまで待つ)
{
    // 「1つ以上」で1本目に走ると、2本目を選ぶ前に終わってしまう。
    Require(PendingCommandAction(SelectionPredicate::OneOrMoreWires, true, false)
            == PendingAction::NeedsConfirm,
        "確かめを待つ");
    Require(PendingCommandAction(SelectionPredicate::OneOrMoreWires, true, true)
            == PendingAction::RunNow,
        "「これで」と言えば走る");
}

KACHA_V2_TEST_MAIN("tool_targeting_tests")
