#include "kachakacha/app/OperationGuide.h"

#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

namespace {

//! 仮置きの文字。1つでも残っていたら案内として不十分である。
//!
//! 綴りを2つに割って組み立てているのは、この行そのものが
//! 「未完成の印が残っている」検査(AT-ARC-005)に引っかからないようにするため。
//! 印を探す側が印を持っていては、検査が回らない。
[[nodiscard]] const std::vector<std::string>& Placeholders()
{
    static const std::vector<std::string> needles = {
        std::string("TO") + "DO",
        std::string("to") + "do",
        std::string("FIX") + "ME",
        "未定",
        "???",
        "...",
        "…",
    };
    return needles;
}

[[nodiscard]] std::string Text(std::string_view value)
{
    return std::string(value);
}

} // namespace

bool ContainsPlaceholder(const std::string& text)
{
    for (const std::string& needle : Placeholders()) {
        if (text.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string OperationGuide::ToStatusLine() const
{
    return toolNameJa + ": " + currentStepJa + " 次: " + nextStepJa + " ["
        + confirmJa + " / " + cancelJa + "] 選択 " + std::to_string(selectionCount)
        + " 件";
}

OperationGuide BuildToolGuide(const CommandDescriptor& command,
    const modeling::ToolPrompt& prompt, int selectionCount)
{
    OperationGuide guide;
    guide.toolNameJa = Text(command.labelJa);
    guide.selectionCount = selectionCount;

    if (!prompt.messageJa.empty()) {
        guide.currentStepJa = prompt.messageJa;
    } else {
        guide.currentStepJa = Text(command.operationGuideJa);
    }

    if (prompt.acceptsMorePoints) {
        guide.nextStepJa = "点を続けて置きます。";
    } else if (prompt.remainingPoints > 1) {
        guide.nextStepJa = "あと " + std::to_string(prompt.remainingPoints)
            + " 点置きます。";
    } else if (prompt.remainingPoints == 1) {
        guide.nextStepJa = "次の点で決まります。";
    } else {
        guide.nextStepJa = "この道具はここまでです。";
    }

    guide.confirmJa = prompt.canFinish ? "右クリックか Enter で決める"
                                       : "点を置くと進む";
    guide.cancelJa = "Esc でやめる / Backspace で1点戻す";
    return guide;
}

OperationGuide BuildCommandGuide(const CommandDescriptor& command, int selectionCount,
    bool enabled)
{
    OperationGuide guide;
    guide.toolNameJa = Text(command.labelJa);
    guide.selectionCount = selectionCount;
    guide.currentStepJa = enabled
        ? Text(command.operationGuideJa)
        : Text(command.predicateFailureJa);
    if (guide.currentStepJa.empty()) {
        guide.currentStepJa = Text(command.operationGuideJa);
    }

    switch (command.mode) {
    case CommandMode::Instant:
        guide.nextStepJa = enabled ? "選ぶとすぐ効きます。"
                                   : "使えるようになると効きます。";
        guide.confirmJa = "選ぶと決まる";
        guide.cancelJa = "元に戻すで戻せる";
        break;
    case CommandMode::Dialog:
        guide.nextStepJa = "窓で設定してから決めます。";
        guide.confirmJa = "窓の OK で決める";
        guide.cancelJa = "窓のキャンセルでやめる";
        break;
    case CommandMode::Modeless:
        guide.nextStepJa = "窓を開いたまま作業できます。";
        guide.confirmJa = "窓を閉じると終わる";
        guide.cancelJa = "窓を閉じるとやめる";
        break;
    case CommandMode::Tool:
        guide.nextStepJa = "画面で点を置きます。";
        guide.confirmJa = "右クリックか Enter で決める";
        guide.cancelJa = "Esc でやめる";
        break;
    }
    return guide;
}

OperationGuide BuildGuide(const CommandDescriptor& command,
    const modeling::ToolPrompt& prompt, int selectionCount, bool enabled)
{
    if (command.mode == CommandMode::Tool && enabled) {
        return BuildToolGuide(command, prompt, selectionCount);
    }
    return BuildCommandGuide(command, selectionCount, enabled);
}

} // namespace kachakacha::v2::app
