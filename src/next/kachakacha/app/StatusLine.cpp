#include "kachakacha/app/StatusLine.h"

#include "kachakacha/app/ToolFooter.h"

#include <cstdio>

namespace kachakacha::v2::app {

namespace {

[[nodiscard]] std::string Fixed1(double value)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    return buffer;
}

[[nodiscard]] std::string ToolOrSelect(const StatusLineParts& parts)
{
    return parts.toolJa.empty() ? std::string("選択") : parts.toolJa;
}

} // namespace

std::string StatusLeftText(const StatusLineParts& parts)
{
    std::string text = std::string(UiModeNameJa(parts.mode)) + " ｜ " + ToolOrSelect(parts);
    if (!parts.hintJa.empty()) {
        text += " ｜ " + parts.hintJa;
    }
    return text;
}

std::string CursorText(const StatusLineParts& parts)
{
    if (!parts.cursorU.has_value() || !parts.cursorV.has_value()) {
        return "—";
    }
    return "U " + Fixed1(*parts.cursorU) + "  V " + Fixed1(*parts.cursorV);
}

std::string StatusRightText(const StatusLineParts& parts)
{
    std::string text = CursorText(parts);
    if (parts.gridMm > 0.0) {
        text += " ｜ Grid " + Fixed1(parts.gridMm) + "mm" + (parts.gridShown ? "" : "(隠)");
    } else {
        text += " ｜ Grid なし";
    }
    text += parts.snapOn ? " ｜ Snap ON" : " ｜ Snap OFF";
    text += " ｜ " + ToolKeyHintJa();
    return text;
}

std::vector<std::string> HudLines(const StatusLineParts& parts)
{
    std::vector<std::string> lines;
    lines.push_back(std::string(UiModeNameJa(parts.mode)) + " › " + ToolOrSelect(parts));
    if (!parts.hintJa.empty()) {
        lines.push_back(parts.hintJa);
    }
    if (!parts.resumeToolJa.empty()) {
        lines.push_back("測定中(Esc で「" + parts.resumeToolJa + "」へ戻る)");
    }
    return lines;
}

std::optional<modeling::DrawingTool> ToolToResumeAfterMeasure(modeling::DrawingTool before,
    modeling::DrawingTool next) noexcept
{
    using modeling::DrawingTool;
    if (next != DrawingTool::Measure || before == DrawingTool::Measure
        || before == DrawingTool::Select) {
        return std::nullopt;
    }
    return before;
}

} // namespace kachakacha::v2::app
