#include "kachakacha/app/ThickenInputState.h"

#include <cstdio>

namespace kachakacha::v2::app {

namespace {

using fabrication::ThicknessPlacement;

//! mm を小数1桁で。「2.0mm」のような、一番下の一行に出す形。
[[nodiscard]] std::string FormatMm1(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    return std::string(buffer);
}

} // namespace

ThickenInputState WithThickenPick(const ThickenInputState& state, const base::EntityId& id)
{
    ThickenInputState next = state;
    if (id.IsNil()) {
        return next;
    }
    // 入っているものを押せば外れる。ここは欄が1つだけなので、迷う余地がない。
    if (next.surface == id) {
        next.surface = base::EntityId{};
        return next;
    }
    next.surface = id;
    return next;
}

ThickenInputState WithoutThickenSurface(const ThickenInputState& state, const base::EntityId& id)
{
    ThickenInputState next = state;
    if (next.surface == id) {
        next.surface = base::EntityId{};
    }
    return next;
}

bool ThickenReadyToBuild(const ThickenInputState& state) noexcept
{
    if (state.surface.IsNil()) {
        return false;
    }
    if (state.toPlane) {
        return !state.targetPlane.IsNil();
    }
    return state.thicknessMm > 0.0;
}

std::vector<std::string> ThickenStatusLinesJa(const ThickenInputState& state,
    const ThickenPreviewOutcome& outcome, bool previewShown)
{
    std::vector<std::string> lines;
    lines.push_back(state.surface.IsNil()
            ? "▶ 次のクリック → 面"
            : "▶ Enter で確定(3D で押し直すと外れます)");
    lines.push_back(std::string("面: ") + (state.surface.IsNil() ? "× まだ" : "✓ 入っている"));
    if (state.surface.IsNil()) {
        lines.push_back("× 3D で形状ガイドの面を押してください");
        return lines;
    }
    if (state.toPlane) {
        lines.push_back(std::string("相手の作業平面: ")
            + (state.targetPlane.IsNil() ? "× まだ" : "✓ 選んでいる"));
        if (state.targetPlane.IsNil()) {
            lines.push_back("× 相手の作業平面を選んでください");
            return lines;
        }
    } else if (state.thicknessMm <= 0.0) {
        lines.push_back("× 厚みは 0 より大きくしてください");
        return lines;
    }
    if (!outcome.evaluated) {
        return lines;
    }
    if (!outcome.available) {
        lines.push_back("× "
            + (outcome.refusalJa.empty() ? std::string("作れません") : outcome.refusalJa));
        return lines;
    }
    lines.push_back("✓ 生成可能: 体積 " + FormatMm1(outcome.volumeMm3) + " mm3、厚み "
        + FormatMm1(outcome.thicknessMm) + " mm");
    lines.push_back(previewShown ? "✓ 下見を表示中(まだ文書へ保存していません)"
                                  : "× 下見が作れませんでした");
    return lines;
}

std::string ThickenFooterLine(const ThickenInputState& state, const std::string& surfaceName,
    const std::string& planeName, const ThickenPreviewOutcome& outcome, bool previewShown)
{
    std::string line = "厚み: ";
    line += "SURFACE=" + (state.surface.IsNil() ? std::string("(なし)") : surfaceName);
    line += " / ";
    if (state.toPlane) {
        line += "PLANE=" + (state.targetPlane.IsNil() ? std::string("(なし)") : planeName);
        line += " / 平面まで";
    } else {
        line += FormatMm1(state.thicknessMm) + "mm";
        line += " / " + std::string(ThicknessPlacementNameJa(state.placement));
    }
    if (outcome.evaluated && outcome.available) {
        line += " / VOLUME=" + FormatMm1(outcome.volumeMm3) + "mm3";
    }
    line += previewShown ? " / Preview only" : " / no preview";
    return line;
}

} // namespace kachakacha::v2::app
