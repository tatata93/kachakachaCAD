#include "kachakacha/app/EscapeAction.h"

#include <algorithm>

namespace kachakacha::v2::app {
namespace {

[[nodiscard]] bool Has(const std::vector<EscapeStep>& steps, EscapeStep step)
{
    return std::find(steps.begin(), steps.end(), step) != steps.end();
}

} // namespace

std::vector<EscapeStep> PlanEscape(const EscapeContext& context)
{
    std::vector<EscapeStep> steps;
    // やりかけの取り消しは、上から1つだけ。
    // 1回のEscで全部消すと、どこまで戻ったのか分からなくなる。
    if (context.draggingGadget) {
        steps.push_back(EscapeStep::CancelGadgetDrag);
    } else if (context.draggingCube) {
        steps.push_back(EscapeStep::CancelCubeDrag);
    } else if (context.waitingForPick) {
        steps.push_back(EscapeStep::CancelPick);
    } else if (context.cursorInputOpen) {
        steps.push_back(EscapeStep::CloseCursorInput);
    } else if (context.toolHasPoints) {
        steps.push_back(EscapeStep::CancelDrawing);
    }

    // そのうえで、必ず「選択道具・何も選んでいない」で終わる。
    // すでにそうなっているものは足さない。足すと、帯に「解除しました」と
    // 出るのに何も変わらない、ということになる。
    if (context.hasSelection) {
        steps.push_back(EscapeStep::ClearSelection);
    }
    if (!context.toolIsSelect) {
        steps.push_back(EscapeStep::BackToSelectTool);
    }
    return steps;
}

std::string_view EscapeMessageJa(const std::vector<EscapeStep>& steps)
{
    if (steps.empty()) {
        return "";
    }
    if (Has(steps, EscapeStep::CancelGadgetDrag) || Has(steps, EscapeStep::CancelCubeDrag)) {
        return "視点の操作をやめました。";
    }
    if (Has(steps, EscapeStep::CancelPick)) {
        return "拾うのをやめました。";
    }
    if (Has(steps, EscapeStep::CancelDrawing) || Has(steps, EscapeStep::CloseCursorInput)) {
        return "作図をやめました。選択に戻ります。";
    }
    if (Has(steps, EscapeStep::ClearSelection) && Has(steps, EscapeStep::BackToSelectTool)) {
        return "選択を解除し、選択道具に戻りました。";
    }
    if (Has(steps, EscapeStep::ClearSelection)) {
        return "選択を解除しました。";
    }
    return "選択道具に戻りました。";
}

} // namespace kachakacha::v2::app
