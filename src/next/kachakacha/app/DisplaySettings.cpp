#include "kachakacha/app/DisplaySettings.h"

#include <algorithm>
#include <cmath>

namespace kachakacha::v2::app {

DisplayStage NextDisplayStage(DisplayStage stage) noexcept
{
    switch (stage) {
    case DisplayStage::All:            return DisplayStage::NoGrid;
    case DisplayStage::NoGrid:         return DisplayStage::NoConstruction;
    case DisplayStage::NoConstruction: return DisplayStage::SelectionOnly;
    case DisplayStage::SelectionOnly:  return DisplayStage::All;
    }
    return DisplayStage::All;
}

DisplaySettings ApplyStage(DisplaySettings settings, DisplayStage stage) noexcept
{
    switch (stage) {
    case DisplayStage::All:
        settings.gridVisible = true;
        settings.constructionVisible = true;
        settings.selectionOnly = false;
        break;
    case DisplayStage::NoGrid:
        settings.gridVisible = false;
        settings.constructionVisible = true;
        settings.selectionOnly = false;
        break;
    case DisplayStage::NoConstruction:
        // 補助線を消す段では、グリッドも消したままにする。
        // 片方だけ戻ると「いま何段目か」が見て分からない。
        settings.gridVisible = false;
        settings.constructionVisible = false;
        settings.selectionOnly = false;
        break;
    case DisplayStage::SelectionOnly:
        // 選んだものだけ。グリッドも補助線も消す(V1 と同じ)。
        settings.gridVisible = false;
        settings.constructionVisible = false;
        settings.selectionOnly = true;
        break;
    }
    return settings;
}

DisplaySettings SettingsForStage(DisplayStage stage) noexcept
{
    return ApplyStage(DisplaySettings{}, stage);
}

std::string_view DisplayStageNameJa(DisplayStage stage) noexcept
{
    switch (stage) {
    case DisplayStage::All:            return "設計: グリッドも補助線も出しています。";
    case DisplayStage::NoGrid:         return "グリッドを消しました。補助線は出ています。";
    case DisplayStage::NoConstruction: return "完成形: グリッドも補助線も消しました。形だけが出ています。";
    case DisplayStage::SelectionOnly:  return "選択だけ: 選んでいるものだけを出しています。";
    }
    return "";
}

std::string_view DisplayStageLabelJa(DisplayStage stage) noexcept
{
    switch (stage) {
    case DisplayStage::All:            return "設計";
    case DisplayStage::NoGrid:         return "グリッド無し";
    case DisplayStage::NoConstruction: return "完成形";
    case DisplayStage::SelectionOnly:  return "選択だけ";
    }
    return "";
}

std::string_view LineStyleNameJa(LineStyle style) noexcept
{
    switch (style) {
    case LineStyle::Solid:  return "実線";
    case LineStyle::Dashed: return "破線";
    case LineStyle::Dotted: return "点線";
    }
    return "";
}

double ClampLineWidthPx(double width) noexcept
{
    if (!std::isfinite(width)) {
        return 2.0;
    }
    return std::clamp(width, 0.25, 12.0);
}

} // namespace kachakacha::v2::app
