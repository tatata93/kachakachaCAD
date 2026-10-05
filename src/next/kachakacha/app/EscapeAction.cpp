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

    // Esc は入力と選択を取り消す。道具を置く操作は V / 選択へ分離する。
    // 測定も同じ道具を維持し、次の対象を測れるようにする。
    if (context.hasSelection) {
        steps.push_back(EscapeStep::ClearSelection);
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
    if (Has(steps, EscapeStep::ResumeToolAfterMeasure)) {
        return "測定を終えて、元の道具へ戻りました。";
    }
    if (Has(steps, EscapeStep::CancelDrawing) || Has(steps, EscapeStep::CloseCursorInput)) {
        return "途中の入力を取り消しました。道具はそのまま続けられます。";
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
