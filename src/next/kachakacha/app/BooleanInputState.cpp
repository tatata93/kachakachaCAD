#include "kachakacha/app/BooleanInputState.h"

#include <algorithm>
#include <cstdlib>

namespace kachakacha::v2::app {

namespace {

[[nodiscard]] std::string Cubic4(double value)
{
    // mm3 を小数4桁で。既存の状態文と同じ桁(RunBoolean の文言)。
    const long long scaled =
        static_cast<long long>(value * 10000.0 + (value < 0 ? -0.5 : 0.5));
    const long long whole = scaled / 10000;
    const long long rest = std::llabs(scaled % 10000);
    std::string tail = std::to_string(rest);
    while (tail.size() < 4) {
        tail = "0" + tail;
    }
    return std::to_string(whole) + "." + tail + " mm3";
}

} // namespace

std::string_view BooleanSlotKey(BooleanSlot slot) noexcept
{
    return slot == BooleanSlot::Target ? "TARGET" : "TOOL";
}

std::string_view BooleanSlotNameJa(BooleanSlot slot) noexcept
{
    return slot == BooleanSlot::Target ? "土台" : "相手";
}

std::string_view BooleanOperationLabelJa(bool cut) noexcept
{
    return cut ? "引く" : "足す";
}

BooleanSlot NextBooleanSlot(const BooleanInputState& state) noexcept
{
    if (state.activeSlot.has_value()) {
        return *state.activeSlot;
    }
    return state.target.IsNil() ? BooleanSlot::Target : BooleanSlot::Tool;
}

BooleanInputState WithBooleanPick(const BooleanInputState& state, const base::EntityId& id)
{
    BooleanInputState next = state;
    if (id.IsNil()) {
        return next;
    }
    // 入っているものを押せば外れる。どちらの欄でも同じ。
    if (next.target == id) {
        next.target = base::EntityId{};
        next.activeSlot = BooleanSlot::Target;
        return next;
    }
    if (next.tool == id) {
        next.tool = base::EntityId{};
        next.activeSlot = BooleanSlot::Tool;
        return next;
    }
    const BooleanSlot slot = NextBooleanSlot(next);
    if (slot == BooleanSlot::Target) {
        next.target = id;
    } else {
        next.tool = id;
    }
    next.activeSlot.reset();   // 明示した欄は満たされた。次は空いている欄へ。
    return next;
}

BooleanInputState WithoutBooleanEntries(const BooleanInputState& state,
    const std::vector<base::EntityId>& ids)
{
    BooleanInputState next = state;
    for (const base::EntityId& id : ids) {
        if (next.target == id) {
            next.target = base::EntityId{};
        }
        if (next.tool == id) {
            next.tool = base::EntityId{};
        }
    }
    return next;
}

BooleanInputState WithBooleanSlotCleared(const BooleanInputState& state, BooleanSlot slot)
{
    BooleanInputState next = state;
    if (slot == BooleanSlot::Target) {
        next.target = base::EntityId{};
    } else {
        next.tool = base::EntityId{};
    }
    next.activeSlot = slot;
    return next;
}

BooleanInputState WithActiveBooleanSlot(const BooleanInputState& state, BooleanSlot slot)
{
    BooleanInputState next = state;
    next.activeSlot = slot;
    return next;
}

std::vector<base::EntityId> BooleanEntries(const BooleanInputState& state)
{
    std::vector<base::EntityId> entries;
    if (!state.target.IsNil()) {
        entries.push_back(state.target);
    }
    if (!state.tool.IsNil()) {
        entries.push_back(state.tool);
    }
    return entries;
}

bool BooleanReady(const BooleanInputState& state) noexcept
{
    return !state.target.IsNil() && !state.tool.IsNil() && !(state.target == state.tool);
}

std::string BooleanHintJa(const BooleanInputState& state)
{
    if (BooleanReady(state) && !state.activeSlot.has_value()) {
        return "Enter で確定(3D で押し直すと外れます)";
    }
    return std::string("次のクリック → ") + std::string(BooleanSlotNameJa(NextBooleanSlot(state)));
}

std::vector<std::string> BooleanStatusLinesJa(const BooleanInputState& state,
    const BooleanPreviewOutcome& outcome, bool previewShown)
{
    std::vector<std::string> lines;
    lines.push_back("▶ " + BooleanHintJa(state));
    lines.push_back(std::string("土台: ") + (state.target.IsNil() ? "× まだ" : "✓ 入っている"));
    lines.push_back(std::string("相手: ") + (state.tool.IsNil() ? "× まだ" : "✓ 入っている"));
    if (!BooleanReady(state)) {
        lines.push_back("× 部品を2つ(土台と相手)入れると下見が出ます");
        return lines;
    }
    if (!outcome.evaluated) {
        return lines;
    }
    if (!outcome.available) {
        lines.push_back("× " + (outcome.refusalJa.empty() ? std::string("作れません")
                                                           : outcome.refusalJa));
        return lines;
    }
    lines.push_back("✓ 生成可能: 体積 " + Cubic4(outcome.previousVolumeMm3) + " → "
        + Cubic4(outcome.volumeMm3));
    lines.push_back(previewShown ? "✓ 下見を表示中(まだ文書へ保存していません)"
                                 : "× 下見が作れませんでした");
    return lines;
}

std::string BooleanFooterLine(const BooleanInputState& state, const std::string& targetName,
    const std::string& toolName, const BooleanPreviewOutcome& outcome, bool previewShown)
{
    std::string line = std::string(BooleanOperationLabelJa(state.cut)) + ": ";
    line += "TARGET=" + (state.target.IsNil() ? std::string("(なし)") : targetName);
    line += " / TOOL=" + (state.tool.IsNil() ? std::string("(なし)") : toolName);
    if (!BooleanReady(state) || state.activeSlot.has_value()) {
        line += " / NEXT=" + std::string(BooleanSlotKey(NextBooleanSlot(state)));
    }
    if (outcome.evaluated && outcome.available) {
        line += " / VOLUME=" + Cubic4(outcome.volumeMm3);
    }
    line += previewShown ? " / Preview only" : " / no preview";
    return line;
}

} // namespace kachakacha::v2::app
