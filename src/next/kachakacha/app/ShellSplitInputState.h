#pragma once

//! 「シェル・分割」(部品の形状編集、matrix P-13)の入力の状態。
//!
//! 道具から始める(何も選んでいなくても棚が出る)。3D で部品を押すと、その部品が相手になる。
//!   シェル: 押した点に一番近い面が「抜く面」に入る。同じ面をもう一度押すと外れる。
//!           別の部品を押すと相手が替わり、面は空に戻る。面は **面の上の点** で持つ
//!           (文書にもそのまま残る。番号は作り直しで変わりうる)。
//!   分割 : 部品だけを押す。分ける平面は **いまの作業平面**(から法線の向きへずらした平面)。
//!           1 回の分割で両側の 2 つの部品を作る。
//! 何が入るかは、ここが決める。画面は押した点から面の点・同じ面かどうかを核に尋ねて渡すだけ。

#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/Vector3.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

struct ShellSplitInputState {
    //! 0 = シェル、1 = 分割。
    int method = 0;
    base::EntityId part;
    //! シェル: 抜く面の上の点(押した順)。
    std::vector<geometry::Vector3> faces;
    //! シェル: 残す肉厚(mm)。
    double thicknessMm = 2.0;
    //! 分割: 作業平面から法線の向きへずらす量(mm。負なら反対へ)。
    double splitOffsetMm = 0.0;
};

[[nodiscard]] std::string_view ShellSplitMethodNameJa(int method) noexcept;   //!< シェル / 分割

//! 命令の名前(part.shell / part.split)から作り方。無ければ偽。
[[nodiscard]] bool ShellSplitMethodForCommand(std::string_view commandId, int& method) noexcept;

//! 3D で部品を押した。facePoint は押した点に一番近い面の上の点(シェルのとき。無ければ値なし)。
//! sameFaceAs は「入っている面のうち、押した面と同じ面の並び番号」(核が決める。無ければ値なし)。
//! 別の部品なら相手を替えて面を空にしてから入れる。同じ面なら外す。分割は部品だけを替える。
[[nodiscard]] ShellSplitInputState WithShellSplitPick(const ShellSplitInputState& state,
    const base::EntityId& part, const std::optional<geometry::Vector3>& facePoint,
    const std::optional<std::size_t>& sameFaceAs);

//! 作り方を替える。面は作り方をまたいで持ち越さない(分割には面が無い)。
[[nodiscard]] ShellSplitInputState WithShellSplitMethod(const ShellSplitInputState& state, int method);

//! 「解除」。部品を外すと面も空になる。
[[nodiscard]] ShellSplitInputState WithoutShellSplitPart(const ShellSplitInputState& state);
[[nodiscard]] ShellSplitInputState WithoutShellSplitFaces(const ShellSplitInputState& state);

//! シェル: 部品・面 1 枚以上・肉厚が 0 より大きい。分割: 部品。
[[nodiscard]] bool ShellSplitReady(const ShellSplitInputState& state) noexcept;

//! 「次のクリック → 部品(抜く面を押す)」のような一行。
[[nodiscard]] std::string ShellSplitHintJa(const ShellSplitInputState& state);

//! 実際に作った結果の要約。下見と確定は同じものを使う。
struct ShellSplitOutcome {
    bool evaluated = false;
    bool available = false;
    //! シェル: 前と後の体積。分割: 法線の側(volumeMm3)と反対の側(otherVolumeMm3)。
    double volumeMm3 = 0.0;
    double previousVolumeMm3 = 0.0;
    double otherVolumeMm3 = 0.0;
    //! 分割: 片側がさらに離れた塊に分かれたときの数(2 以上なら知らせる)。
    int positivePieces = 0;
    int negativePieces = 0;
    std::string refusalJa;
};

//! 棚の「状態」に出す行。
[[nodiscard]] std::vector<std::string> ShellSplitStatusLinesJa(const ShellSplitInputState& state,
    const ShellSplitOutcome& outcome, bool previewShown);

//! 一番下の一行。「シェル: PART=箱 / FACES=1 / T 2.000 mm / Preview only」
[[nodiscard]] std::string ShellSplitFooterLine(const ShellSplitInputState& state,
    const std::string& partName, const ShellSplitOutcome& outcome, bool previewShown);

} // namespace kachakacha::v2::app
