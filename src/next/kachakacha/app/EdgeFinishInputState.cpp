#include "kachakacha/app/EdgeFinishInputState.h"

#include <cstdio>

namespace kachakacha::v2::app {
namespace {

[[nodiscard]] std::string Millimeters(double value)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.3f mm", value);
    return buffer;
}

[[nodiscard]] std::string Cubic(double value)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.4f mm3", value);
    return buffer;
}

} // namespace

std::string_view EdgeFinishKindNameJa(int kind) noexcept
{
    return kind == 1 ? "面取り" : "フィレット";
}

std::string_view EdgeFinishSizeNameJa(int kind) noexcept
{
    return kind == 1 ? "距離" : "半径";
}

bool EdgeFinishKindForCommand(std::string_view commandId, int& kind) noexcept
{
    if (commandId == "part.fillet") {
        kind = 0;
        return true;
    }
    if (commandId == "part.chamfer") {
        kind = 1;
        return true;
    }
    return false;
}

EdgeFinishInputState WithEdgeFinishPick(const EdgeFinishInputState& state,
    const base::EntityId& part, const std::optional<geometry::Vector3>& edgeMidpoint, double joinMm)
{
    EdgeFinishInputState next = state;
    if (part.IsNil()) {
        return next;
    }
    if (!(next.part == part)) {
        // 別の部品。辺は前の部品のものなので持ち越さない。
        next.part = part;
        next.edges.clear();
    }
    if (!edgeMidpoint.has_value()) {
        return next;
    }
    for (auto it = next.edges.begin(); it != next.edges.end(); ++it) {
        if (geometry::Distance(*it, *edgeMidpoint) <= joinMm) {
            next.edges.erase(it);   // 入っている辺の近くを押した: 外す
            return next;
        }
    }
    next.edges.push_back(*edgeMidpoint);
    return next;
}

EdgeFinishInputState WithoutEdgeFinishPart(const EdgeFinishInputState& state)
{
    EdgeFinishInputState next = state;
    next.part = base::EntityId{};
    next.edges.clear();
    return next;
}

EdgeFinishInputState WithoutEdgeFinishEdges(const EdgeFinishInputState& state)
{
    EdgeFinishInputState next = state;
    next.edges.clear();
    return next;
}

bool EdgeFinishReady(const EdgeFinishInputState& state) noexcept
{
    return !state.part.IsNil() && !state.edges.empty() && state.sizeMm > 0.0;
}

std::string EdgeFinishHintJa(const EdgeFinishInputState& state)
{
    if (state.part.IsNil()) {
        return "次のクリック → 部品(丸めたい辺の近くを押す)";
    }
    if (state.edges.empty()) {
        return "次のクリック → 辺(部品の辺の近くを押す)";
    }
    return "Enter で確定(辺はいくつでも。入っている辺の近くを押すと外れます)";
}

std::vector<std::string> EdgeFinishStatusLinesJa(const EdgeFinishInputState& state,
    const EdgeFinishOutcome& outcome, bool previewShown)
{
    std::vector<std::string> lines;
    lines.push_back("▶ " + EdgeFinishHintJa(state));
    lines.push_back(std::string("部品: ") + (state.part.IsNil() ? "× まだ" : "✓ 入っている"));
    lines.push_back("辺: " + (state.edges.empty() ? std::string("× まだ")
                                                   : "✓ " + std::to_string(state.edges.size()) + " 本"));
    lines.push_back(std::string(EdgeFinishSizeNameJa(state.kind)) + ": "
        + (state.sizeMm > 0.0 ? Millimeters(state.sizeMm) : std::string("× 0 より大きくしてください")));
    if (!EdgeFinishReady(state) || !outcome.evaluated) {
        return lines;
    }
    if (!outcome.available) {
        lines.push_back("× " + (outcome.refusalJa.empty() ? std::string("作れません") : outcome.refusalJa));
        return lines;
    }
    lines.push_back("✓ 生成可能: 体積 " + Cubic(outcome.previousVolumeMm3) + " → " + Cubic(outcome.volumeMm3));
    lines.push_back(previewShown ? "✓ 下見を表示中(まだ文書へ保存していません)"
                                 : "× 下見が作れませんでした");
    return lines;
}

std::string EdgeFinishFooterLine(const EdgeFinishInputState& state, const std::string& partName,
    const EdgeFinishOutcome& outcome, bool previewShown)
{
    std::string line = std::string(EdgeFinishKindNameJa(state.kind)) + ": ";
    line += "PART=" + (state.part.IsNil() ? std::string("(なし)") : partName);
    line += " / EDGES=" + std::to_string(state.edges.size());
    line += std::string(state.kind == 1 ? " / C " : " / R ") + Millimeters(state.sizeMm);
    if (state.part.IsNil()) {
        line += " / NEXT=PART";
    } else if (state.edges.empty()) {
        line += " / NEXT=EDGE";
    }
    if (outcome.evaluated && outcome.available) {
        line += " / VOLUME=" + Cubic(outcome.volumeMm3);
    }
    line += previewShown ? " / Preview only" : " / no preview";
    return line;
}

} // namespace kachakacha::v2::app
