// 状態行と HUD の文言(指示書 C-11 / C-12)、測定の重ね道具(C-16)。
#include "kachakacha/app/EscapeAction.h"
#include "kachakacha/app/StatusLine.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::app::CursorText;
using kachakacha::v2::app::HudLines;
using kachakacha::v2::app::EscapeContext;
using kachakacha::v2::app::EscapeMessageJa;
using kachakacha::v2::app::EscapeStep;
using kachakacha::v2::app::PlanEscape;
using kachakacha::v2::app::StatusLeftText;
using kachakacha::v2::app::StatusLineParts;
using kachakacha::v2::app::StatusRightText;
using kachakacha::v2::app::ToolToResumeAfterMeasure;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

KACHA_V2_TEST(status_line, 左はモードと道具と案内)
{
    StatusLineParts parts;
    parts.mode = UiMode::Drawing;
    parts.toolJa = "線";
    parts.hintJa = "1点目を押してください";
    RequireEqual(StatusLeftText(parts), std::string("作図 ｜ 線 ｜ 1点目を押してください"), "3連");
    parts.toolJa.clear();
    parts.hintJa.clear();
    RequireEqual(StatusLeftText(parts), std::string("作図 ｜ 選択"), "道具が無ければ選択");
}

KACHA_V2_TEST(status_line, 右は座標とGridとSnapとキー)
{
    StatusLineParts parts;
    parts.cursorU = 12.04;
    parts.cursorV = -3.5;
    parts.gridMm = 10.0;
    parts.snapOn = true;
    const std::string right = StatusRightText(parts);
    Require(right.find("U 12.0  V -3.5") == 0, "座標が先頭: " + right);
    Require(right.find("Grid 10.0mm") != std::string::npos, "Grid の間隔");
    Require(right.find("Snap ON") != std::string::npos, "Snap");
    Require(right.find("Esc") != std::string::npos && right.find("Enter") != std::string::npos,
        "Enter/Esc の案内がいつも見える");
    parts.cursorU.reset();
    parts.snapOn = false;
    parts.gridMm = 0.0;
    RequireEqual(CursorText(parts), std::string("—"), "座標が無ければ —");
    const std::string off = StatusRightText(parts);
    Require(off.find("Grid なし") != std::string::npos && off.find("Snap OFF") != std::string::npos,
        "無し/OFF が読める");
}

KACHA_V2_TEST(status_line, HUDはモードと道具が1行目で案内が2行目)
{
    StatusLineParts parts;
    parts.mode = UiMode::Part;
    parts.toolJa = "押し出し";
    parts.hintJa = "輪郭を押してください";
    const auto lines = HudLines(parts);
    Require(lines.size() == 2, "2行");
    RequireEqual(lines[0], std::string("部品 › 押し出し"), "1行目");
    RequireEqual(lines[1], std::string("輪郭を押してください"), "2行目");
    parts.resumeToolJa = "線";
    const auto measuring = HudLines(parts);
    Require(measuring.size() == 3 && measuring[2].find("線") != std::string::npos
            && measuring[2].find("Esc") != std::string::npos,
        "測定を重ねていると戻り先が見える");
    StatusLineParts empty;
    Require(!HudLines(empty).empty(), "空にはならない");
}

KACHA_V2_TEST(status_line, 測定は元の道具を覚えて戻る)
{
    const auto fromLine = ToolToResumeAfterMeasure(DrawingTool::Line, DrawingTool::Measure);
    Require(fromLine.has_value() && *fromLine == DrawingTool::Line, "線から測定なら線へ戻る");
    Require(!ToolToResumeAfterMeasure(DrawingTool::Select, DrawingTool::Measure).has_value(),
        "選択からは戻り先を作らない");
    Require(!ToolToResumeAfterMeasure(DrawingTool::Measure, DrawingTool::Measure).has_value(),
        "測定から測定は作らない");
    Require(!ToolToResumeAfterMeasure(DrawingTool::Line, DrawingTool::Circle).has_value(),
        "測定以外へ持ち替えるときは作らない");
}

KACHA_V2_TEST(status_line, 測定を重ねているときのEscは元の道具へ戻る)
{
    EscapeContext context;
    context.measuringOverRunningTool = true;
    context.hasSelection = true;
    const auto steps = PlanEscape(context);
    bool resume = false;
    bool backToSelect = false;
    for (const EscapeStep step : steps) {
        resume = resume || step == EscapeStep::ResumeToolAfterMeasure;
        backToSelect = backToSelect || step == EscapeStep::BackToSelectTool;
    }
    Require(resume, "元の道具へ戻る手順がある");
    Require(!backToSelect, "選択道具へは落とさない");
    Require(std::string(EscapeMessageJa(steps)).find("元の道具") != std::string::npos,
        "一言に「元の道具」");
    // 重ねていなければ、ふだんどおり選択へ。
    context.measuringOverRunningTool = false;
    bool plain = false;
    for (const EscapeStep step : PlanEscape(context)) {
        plain = plain || step == EscapeStep::BackToSelectTool;
    }
    Require(plain, "重ねていなければ選択道具へ");
}

KACHA_V2_TEST_MAIN("status_line_tests")
