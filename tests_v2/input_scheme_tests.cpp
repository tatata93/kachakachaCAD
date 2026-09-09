// V1同等の操作系(docs/v2/v1-input-parity.md)。
//
// Esc と Shift の拘束は、画面を出さずに確かめられる形にしてある。
// 画面側に書くと、Qt を組み立てられる機械でしか確かめられない。
#include "kachakacha/app/EscapeAction.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/DrawingConstraint.h"

#include <algorithm>
#include <cmath>
#include <string>

using kachakacha::v2::app::EscapeContext;
using kachakacha::v2::app::EscapeMessageJa;
using kachakacha::v2::app::EscapeStep;
using kachakacha::v2::app::PlanEscape;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::ApplyAxisConstraint;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::modeling::ToolUsesAxisConstraint;
using kachakacha::v2::modeling::WorkPlaneFrame;
using kachakacha::v2::test::Require;

namespace {

[[nodiscard]] bool Has(const std::vector<EscapeStep>& steps, EscapeStep step)
{
    return std::find(steps.begin(), steps.end(), step) != steps.end();
}

//! XY平面。u が X、v が Y。
[[nodiscard]] WorkPlaneFrame PlaneXY()
{
    WorkPlaneFrame plane;
    plane.origin = Vector3{0.0, 0.0, 0.0};
    plane.uAxis = Vector3{1.0, 0.0, 0.0};
    plane.vAxis = Vector3{0.0, 1.0, 0.0};
    plane.normal = Vector3{0.0, 0.0, 1.0};
    return plane;
}

} // namespace

KACHA_V2_TEST(escape, 何もしていなくても選択道具へ戻る)
{
    // V1 の Esc は必ず「選択道具・何も選んでいない」で終わる。
    EscapeContext context;
    context.toolIsSelect = false;
    const auto steps = PlanEscape(context);
    Require(Has(steps, EscapeStep::BackToSelectTool), "選択道具へ戻る");
}

KACHA_V2_TEST(escape, すでに選択道具で何も選んでいなければ何もしない)
{
    // 何も起きていないのに「解除しました」と出すと、何が起きたか分からなくなる。
    EscapeContext context;
    context.toolIsSelect = true;
    context.hasSelection = false;
    Require(PlanEscape(context).empty(), "することが無い");
    Require(EscapeMessageJa(PlanEscape(context)).empty(), "言うことも無い");
}

KACHA_V2_TEST(escape, 選択は解除される)
{
    EscapeContext context;
    context.toolIsSelect = true;
    context.hasSelection = true;
    const auto steps = PlanEscape(context);
    Require(Has(steps, EscapeStep::ClearSelection), "選択を空にする");
    Require(!Has(steps, EscapeStep::BackToSelectTool), "すでに選択道具なので戻らない");
}

KACHA_V2_TEST(escape, 作図の途中なら作図だけを取り消してから戻る)
{
    EscapeContext context;
    context.toolHasPoints = true;
    context.hasSelection = true;
    const auto steps = PlanEscape(context);
    Require(Has(steps, EscapeStep::CancelDrawing), "作図を取り消す");
    Require(Has(steps, EscapeStep::ClearSelection), "選択も解除する");
    Require(Has(steps, EscapeStep::BackToSelectTool), "選択道具へ戻る");
    // 取り消しは1つだけ。
    Require(!Has(steps, EscapeStep::CancelPick), "拾いは待っていない");
}

KACHA_V2_TEST(escape, やりかけの取り消しは上から1つだけ)
{
    // 1回のEscで全部消すと、どこまで戻ったのか分からなくなる。
    EscapeContext context;
    context.draggingGadget = true;
    context.draggingCube = true;
    context.waitingForPick = true;
    context.cursorInputOpen = true;
    context.toolHasPoints = true;
    const auto steps = PlanEscape(context);
    int cancels = 0;
    for (const EscapeStep step : steps) {
        if (step == EscapeStep::CancelGadgetDrag || step == EscapeStep::CancelCubeDrag
            || step == EscapeStep::CancelPick || step == EscapeStep::CloseCursorInput
            || step == EscapeStep::CancelDrawing) {
            ++cancels;
        }
    }
    Require(cancels == 1, "取り消しは1つ");
    Require(Has(steps, EscapeStep::CancelGadgetDrag), "いちばん上のものを取り消す");
}

KACHA_V2_TEST(escape, 取り消す順は決まっている)
{
    // 押すたびに順が変わると、Escを何回押せばよいかが読めない。
    const struct {
        EscapeContext context;
        EscapeStep expected;
    } cases[] = {
        {[] { EscapeContext c; c.draggingCube = true; c.waitingForPick = true;
              c.toolHasPoints = true; return c; }(), EscapeStep::CancelCubeDrag},
        {[] { EscapeContext c; c.waitingForPick = true; c.cursorInputOpen = true;
              c.toolHasPoints = true; return c; }(), EscapeStep::CancelPick},
        {[] { EscapeContext c; c.cursorInputOpen = true; c.toolHasPoints = true;
              return c; }(), EscapeStep::CloseCursorInput},
        {[] { EscapeContext c; c.toolHasPoints = true; return c; }(),
            EscapeStep::CancelDrawing},
    };
    for (const auto& item : cases) {
        Require(Has(PlanEscape(item.context), item.expected), "順のとおりに取り消す");
    }
}

KACHA_V2_TEST(escape, 何を取り消したかを言う)
{
    EscapeContext drawing;
    drawing.toolHasPoints = true;
    Require(!EscapeMessageJa(PlanEscape(drawing)).empty(), "作図のときは言う");
    EscapeContext picking;
    picking.waitingForPick = true;
    Require(EscapeMessageJa(PlanEscape(picking)).find("拾う") != std::string_view::npos,
        "拾いをやめたと言う");
}

KACHA_V2_TEST(constraint, 直線は水平か垂直へ寄る)
{
    // Shift が無いと、水平な線1本を引くのにピクセル単位で狙うことになる。
    const WorkPlaneFrame plane = PlaneXY();
    const Vector3 anchor{10.0, 20.0, 0.0};
    // 横へ大きく動かしたら、縦を捨てて水平にする。
    const Vector3 flat = ApplyAxisConstraint(DrawingTool::Line, plane, anchor,
        Vector3{60.0, 23.0, 0.0});
    Require(std::abs(flat.x - 60.0) < 1e-9, "横はそのまま");
    Require(std::abs(flat.y - 20.0) < 1e-9, "縦は基準にそろう");
    // 縦へ大きく動かしたら、垂直にする。
    const Vector3 tall = ApplyAxisConstraint(DrawingTool::Line, plane, anchor,
        Vector3{13.0, 90.0, 0.0});
    Require(std::abs(tall.x - 10.0) < 1e-9, "横が基準にそろう");
    Require(std::abs(tall.y - 90.0) < 1e-9, "縦はそのまま");
}

KACHA_V2_TEST(constraint, ちょうど並んだときは横を選ぶ)
{
    // 迷うたびに勝手が変わらないようにする。
    const WorkPlaneFrame plane = PlaneXY();
    const Vector3 made = ApplyAxisConstraint(DrawingTool::Line, plane,
        Vector3{0.0, 0.0, 0.0}, Vector3{30.0, 30.0, 0.0});
    Require(std::abs(made.y) < 1e-9, "横になる");
}

KACHA_V2_TEST(constraint, 矩形は正方形になる)
{
    const WorkPlaneFrame plane = PlaneXY();
    const Vector3 made = ApplyAxisConstraint(DrawingTool::Rectangle, plane,
        Vector3{0.0, 0.0, 0.0}, Vector3{80.0, 30.0, 0.0});
    // 長いほうの辺に合わせる。短いほうに合わせると、動かしたぶんが黙って捨てられる。
    Require(std::abs(made.x - 80.0) < 1e-9, "横は80");
    Require(std::abs(made.y - 80.0) < 1e-9, "縦も80");
}

KACHA_V2_TEST(constraint, 負の向きでも正方形になる)
{
    const WorkPlaneFrame plane = PlaneXY();
    const Vector3 made = ApplyAxisConstraint(DrawingTool::Rectangle, plane,
        Vector3{0.0, 0.0, 0.0}, Vector3{-20.0, 50.0, 0.0});
    Require(std::abs(made.x + 50.0) < 1e-9, "左へ50");
    Require(std::abs(made.y - 50.0) < 1e-9, "上へ50");
}

KACHA_V2_TEST(constraint, 拘束を使わない道具は動かさない)
{
    // 円の半径を水平へ寄せても意味がない。勝手に寄せると狙った円が作れない。
    const WorkPlaneFrame plane = PlaneXY();
    const Vector3 point{33.0, 44.0, 0.0};
    for (const DrawingTool tool : {DrawingTool::Circle, DrawingTool::Arc,
             DrawingTool::Select, DrawingTool::Measure}) {
        Require(!ToolUsesAxisConstraint(tool), "拘束を使わない");
        const Vector3 made = ApplyAxisConstraint(tool, plane,
            Vector3{0.0, 0.0, 0.0}, point);
        Require(std::abs(made.x - point.x) < 1e-12
                && std::abs(made.y - point.y) < 1e-12,
            "そのまま返る");
    }
}

KACHA_V2_TEST(constraint, 作業平面の上で効く)
{
    // XY平面ではなく、立てた平面でも同じように効くこと。
    WorkPlaneFrame plane;
    plane.origin = Vector3{0.0, 0.0, 0.0};
    plane.uAxis = Vector3{0.0, 1.0, 0.0};   // u は Y
    plane.vAxis = Vector3{0.0, 0.0, 1.0};   // v は Z
    plane.normal = Vector3{1.0, 0.0, 0.0};
    const Vector3 made = ApplyAxisConstraint(DrawingTool::Line, plane,
        Vector3{0.0, 10.0, 5.0}, Vector3{0.0, 70.0, 8.0});
    Require(std::abs(made.y - 70.0) < 1e-9, "平面の横方向はそのまま");
    Require(std::abs(made.z - 5.0) < 1e-9, "平面の縦方向が基準にそろう");
}

KACHA_V2_TEST_MAIN("input_scheme_tests")
