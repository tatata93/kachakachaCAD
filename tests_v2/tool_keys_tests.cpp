// 道具の最中の Enter / Esc の決め方(app/ToolKeys.h)。
//
// これまで Enter と Esc は 3D 画面の keyPressEvent にしか無かった。
// 右の欄へ数字を打った直後は焦点が欄にあるので届かず、3D を一度クリックして
// 焦点を戻すしかなかった。そのクリックで選択が変わる。
#include "kachakacha/app/ToolKeys.h"
#include "kachakacha/base/TestHarness.h"

using kachakacha::v2::app::ActionForCancelKey;
using kachakacha::v2::app::ActionForConfirmKey;
using kachakacha::v2::app::ToolKeyAction;
using kachakacha::v2::app::ToolKeyContext;
using kachakacha::v2::test::Require;

KACHA_V2_TEST(tool_keys, 道具が動いていなければ受け取らない)
{
    ToolKeyContext idle;
    idle.previewActive = false;
    Require(ActionForConfirmKey(idle) == ToolKeyAction::Ignore, "Enter は素通し");
    Require(ActionForCancelKey(idle) == ToolKeyAction::Ignore, "Esc も素通し");
}

KACHA_V2_TEST(tool_keys, 欄の外の_Enter_は確定する)
{
    ToolKeyContext context;
    context.previewActive = true;
    context.inTypingField = false;
    Require(ActionForConfirmKey(context) == ToolKeyAction::Confirm, "確定");
}

KACHA_V2_TEST(tool_keys, 値を変えた_Enter_は下見を作り直して待つ)
{
    // 打った値は必ず一度、下見で見える。見ていない値で作らない。
    ToolKeyContext context;
    context.previewActive = true;
    context.inTypingField = true;
    context.valueChanged = true;
    Require(ActionForConfirmKey(context) == ToolKeyAction::CommitValueAndWait,
        "入れて待つ");
}

KACHA_V2_TEST(tool_keys, 値を変えていない_Enter_は欄の中でも確定する)
{
    // ここが「確定」にならないと、欄に焦点があるかぎり永久に確定できない。
    ToolKeyContext context;
    context.previewActive = true;
    context.inTypingField = true;
    context.valueChanged = false;
    Require(ActionForConfirmKey(context) == ToolKeyAction::Confirm, "確定");
}

KACHA_V2_TEST(tool_keys, Esc_はどこで押してもやめる)
{
    // 焦点が右の欄にあっても、やめられなければならない。
    for (const bool typing : {false, true}) {
        ToolKeyContext context;
        context.previewActive = true;
        context.inTypingField = typing;
        Require(ActionForCancelKey(context) == ToolKeyAction::Cancel, "やめる");
    }
}

KACHA_V2_TEST_MAIN("tool_keys_tests")
