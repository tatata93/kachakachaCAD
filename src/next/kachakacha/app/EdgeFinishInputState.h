#pragma once

//! 「辺の丸め・面取り」(立体の辺のフィレット・面取り、matrix P-12)の入力の状態。
//!
//! 道具から始める(何も選んでいなくても棚が出る)。3D で部品を押すと、その部品が相手になり、
//! 押した点に一番近い辺が入る。同じ部品をもう一度押すと、近い辺を足し引きする
//! (入っている辺の近くなら外れる)。別の部品を押すと相手が替わり、辺は空に戻る。
//! 辺は **真ん中の点** で持つ(文書にもそのまま残る。番号は作り直しで変わりうる)。
//! 何が入るかは、ここが決める。画面は押した点から辺の真ん中を核に尋ねて渡すだけ。

#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/Vector3.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

struct EdgeFinishInputState {
    //! 0 = 丸め(フィレット)、1 = 面取り。
    int kind = 0;
    base::EntityId part;
    //! 選んだ辺の真ん中の点(押した順)。
    std::vector<geometry::Vector3> edges;
    //! 半径 / 距離(mm)。
    double sizeMm = 1.0;
};

[[nodiscard]] std::string_view EdgeFinishKindNameJa(int kind) noexcept;   //!< フィレット / 面取り
[[nodiscard]] std::string_view EdgeFinishSizeNameJa(int kind) noexcept;   //!< 半径 / 距離

//! 命令の名前(part.fillet / part.chamfer)から種類。無ければ偽。
[[nodiscard]] bool EdgeFinishKindForCommand(std::string_view commandId, int& kind) noexcept;

//! 3D で部品を押した。edgeMidpoint は押した点に一番近い辺の真ん中(無ければ値を持たない)。
//! 別の部品なら相手を替えて辺を空にしてから入れる。同じ部品なら辺を足し引きする
//! (joinMm 以内に同じ真ん中があれば外す)。
[[nodiscard]] EdgeFinishInputState WithEdgeFinishPick(const EdgeFinishInputState& state,
    const base::EntityId& part, const std::optional<geometry::Vector3>& edgeMidpoint,
    double joinMm);

//! 「解除」。部品を外すと辺も空になる。
[[nodiscard]] EdgeFinishInputState WithoutEdgeFinishPart(const EdgeFinishInputState& state);
[[nodiscard]] EdgeFinishInputState WithoutEdgeFinishEdges(const EdgeFinishInputState& state);

//! 部品・辺 1 本以上・大きさが 0 より大きい。
[[nodiscard]] bool EdgeFinishReady(const EdgeFinishInputState& state) noexcept;

//! 「次のクリック → 部品(辺の近くを押す)」のような一行。
[[nodiscard]] std::string EdgeFinishHintJa(const EdgeFinishInputState& state);

//! 実際に作った結果の要約。下見と確定は同じものを使う。
struct EdgeFinishOutcome {
    bool evaluated = false;
    bool available = false;
    double volumeMm3 = 0.0;
    double previousVolumeMm3 = 0.0;
    std::string refusalJa;
};

//! 棚の「状態」に出す行。
[[nodiscard]] std::vector<std::string> EdgeFinishStatusLinesJa(const EdgeFinishInputState& state,
    const EdgeFinishOutcome& outcome, bool previewShown);

//! 一番下の一行。「フィレット: PART=箱 / EDGES=2 / R 3.0 mm / Preview only」
[[nodiscard]] std::string EdgeFinishFooterLine(const EdgeFinishInputState& state,
    const std::string& partName, const EdgeFinishOutcome& outcome, bool previewShown);

} // namespace kachakacha::v2::app
