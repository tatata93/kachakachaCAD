#include "kachakacha/app/DisplaySettings.h"

namespace kachakacha::v2::app {

DisplayStage NextDisplayStage(DisplayStage stage) noexcept
{
    switch (stage) {
    case DisplayStage::All:            return DisplayStage::NoGrid;
    case DisplayStage::NoGrid:         return DisplayStage::NoConstruction;
    case DisplayStage::NoConstruction: return DisplayStage::All;
    }
    return DisplayStage::All;
}

DisplaySettings SettingsForStage(DisplayStage stage) noexcept
{
    switch (stage) {
    case DisplayStage::All:
        return DisplaySettings{true, true};
    case DisplayStage::NoGrid:
        return DisplaySettings{false, true};
    case DisplayStage::NoConstruction:
        // 補助線を消す段では、グリッドも消したままにする。
        // 片方だけ戻ると「いま何段目か」が見て分からない。
        return DisplaySettings{false, false};
    }
    return DisplaySettings{true, true};
}

std::string_view DisplayStageNameJa(DisplayStage stage) noexcept
{
    switch (stage) {
    case DisplayStage::All:            return "グリッドも補助線も出しています。";
    case DisplayStage::NoGrid:         return "グリッドを消しました。補助線は出ています。";
    case DisplayStage::NoConstruction: return "グリッドも補助線も消しました。形だけが出ています。";
    }
    return "";
}

} // namespace kachakacha::v2::app
