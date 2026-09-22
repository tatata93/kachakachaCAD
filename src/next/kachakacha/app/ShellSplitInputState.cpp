#include "kachakacha/app/ShellSplitInputState.h"

#include <cstdio>

namespace kachakacha::v2::app {
namespace {

[[nodiscard]] std::string Millimeters(double value)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.3f mm", value);
    return buffer;
}

[[nodiscard]] std::string SignedMillimeters(double value)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%+.3f mm", value);
    return buffer;
}

[[nodiscard]] std::string Cubic(double value)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.4f mm3", value);
    return buffer;
}

[[nodiscard]] bool IsSplit(const ShellSplitInputState& state) noexcept
{
    return state.method == 1;
}

} // namespace

std::string_view ShellSplitMethodNameJa(int method) noexcept
{
    return method == 1 ? "分割" : "シェル";
}

bool ShellSplitMethodForCommand(std::string_view commandId, int& method) noexcept
{
    if (commandId == "part.shell") {
        method = 0;
        return true;
    }
    if (commandId == "part.split") {
        method = 1;
        return true;
    }
    return false;
}

ShellSplitInputState WithShellSplitPick(const ShellSplitInputState& state, const base::EntityId& part,
    const std::optional<geometry::Vector3>& facePoint, const std::optional<std::size_t>& sameFaceAs)
{
    ShellSplitInputState next = state;
    if (part.IsNil()) {
        return next;
    }
    if (!(next.part == part)) {
        // 別の部品。面は前の部品のものなので持ち越さない。
        next.part = part;
        next.faces.clear();
    }
    if (IsSplit(next) || !facePoint.has_value()) {
        return next;
    }
    if (sameFaceAs.has_value() && *sameFaceAs < next.faces.size()) {
        next.faces.erase(next.faces.begin() + static_cast<std::ptrdiff_t>(*sameFaceAs));
        return next;   // 入っている面を押した: 外す
    }
    next.faces.push_back(*facePoint);
    return next;
}

ShellSplitInputState WithShellSplitMethod(const ShellSplitInputState& state, int method)
{
    ShellSplitInputState next = state;
    if (next.method != method) {
        next.method = method;
        next.faces.clear();
    }
    return next;
}

ShellSplitInputState WithoutShellSplitPart(const ShellSplitInputState& state)
{
    ShellSplitInputState next = state;
    next.part = base::EntityId{};
    next.faces.clear();
    return next;
}

ShellSplitInputState WithoutShellSplitFaces(const ShellSplitInputState& state)
{
    ShellSplitInputState next = state;
    next.faces.clear();
    return next;
}

bool ShellSplitReady(const ShellSplitInputState& state) noexcept
{
    if (state.part.IsNil()) {
        return false;
    }
    if (IsSplit(state)) {
        return true;
    }
    return !state.faces.empty() && state.thicknessMm > 0.0;
}

std::string ShellSplitHintJa(const ShellSplitInputState& state)
{
    if (IsSplit(state)) {
        if (state.part.IsNil()) {
            return "次のクリック → 部品(分けたい部品を押す)";
        }
        return "Enter で確定(いまの作業平面で 2 つに分けます。位置は「ずらす」で動かせます)";
    }
    if (state.part.IsNil()) {
        return "次のクリック → 部品(開けたい面を押す)";
    }
    if (state.faces.empty()) {
        return "次のクリック → 面(部品の開けたい面を押す)";
    }
    return "Enter で確定(面はいくつでも。入っている面を押すと外れます)";
}

std::vector<std::string> ShellSplitStatusLinesJa(const ShellSplitInputState& state,
    const ShellSplitOutcome& outcome, bool previewShown)
{
    std::vector<std::string> lines;
    lines.push_back("▶ " + ShellSplitHintJa(state));
    lines.push_back(std::string("部品: ") + (state.part.IsNil() ? "× まだ" : "✓ 入っている"));
    if (IsSplit(state)) {
        lines.push_back("平面: ✓ いまの作業平面から " + SignedMillimeters(state.splitOffsetMm));
    } else {
        lines.push_back("抜く面: " + (state.faces.empty() ? std::string("× まだ")
                                     : "✓ " + std::to_string(state.faces.size()) + " 枚"));
        lines.push_back("肉厚: " + (state.thicknessMm > 0.0 ? Millimeters(state.thicknessMm)
                                                             : std::string("× 0 より大きくしてください")));
    }
    if (!ShellSplitReady(state) || !outcome.evaluated) {
        return lines;
    }
    if (!outcome.available) {
        lines.push_back("× " + (outcome.refusalJa.empty() ? std::string("作れません") : outcome.refusalJa));
        return lines;
    }
    if (IsSplit(state)) {
        lines.push_back("✓ 生成可能: 法線の側 " + Cubic(outcome.volumeMm3) + " / 反対の側 "
            + Cubic(outcome.otherVolumeMm3));
        if (outcome.positivePieces > 1 || outcome.negativePieces > 1) {
            lines.push_back("! 片側が離れた塊に分かれます(法線の側 " + std::to_string(outcome.positivePieces)
                + " / 反対の側 " + std::to_string(outcome.negativePieces)
                + ")。それぞれ 1 つの部品として残ります。");
        }
    } else {
        lines.push_back("✓ 生成可能: 体積 " + Cubic(outcome.previousVolumeMm3) + " → "
            + Cubic(outcome.volumeMm3));
    }
    lines.push_back(previewShown ? "✓ 下見を表示中(まだ文書へ保存していません)"
                                 : "× 下見が作れませんでした");
    return lines;
}

std::string ShellSplitFooterLine(const ShellSplitInputState& state, const std::string& partName,
    const ShellSplitOutcome& outcome, bool previewShown)
{
    std::string line = std::string(ShellSplitMethodNameJa(state.method)) + ": ";
    line += "PART=" + (state.part.IsNil() ? std::string("(なし)") : partName);
    if (IsSplit(state)) {
        line += " / PLANE=作業平面 " + SignedMillimeters(state.splitOffsetMm);
    } else {
        line += " / FACES=" + std::to_string(state.faces.size());
        line += " / T " + Millimeters(state.thicknessMm);
    }
    if (state.part.IsNil()) {
        line += " / NEXT=PART";
    } else if (!IsSplit(state) && state.faces.empty()) {
        line += " / NEXT=FACE";
    }
    if (outcome.evaluated && outcome.available) {
        line += IsSplit(state) ? " / VOLUME=" + Cubic(outcome.volumeMm3) + " + "
                    + Cubic(outcome.otherVolumeMm3)
                               : " / VOLUME=" + Cubic(outcome.volumeMm3);
    }
    line += previewShown ? " / Preview only" : " / no preview";
    return line;
}

} // namespace kachakacha::v2::app
