#pragma once

//! 意味状態(ui-ux-integrated-spec §3)。
//!
//! 「見た目はデータ種類より状態を優先する」という規則を1か所に置く。
//! どの線がどの状態かを画面側で書き直すと、線・点・形で判定が食い違い、
//! 「選んだのに色が変わらない線」が残る。判定はここだけにする。
//!
//! ここは色を持たない。Qt を知らない層なので、決めるのは **状態まで** である。
//! 色と線幅はテーマ(ViewportPalette)が決める。こうしておくと、
//! 標準テーマと Windows 95 テーマで「状態の意味」を変えずに色だけ差し替えられる
//! (§3 末尾「色の具体値はテーマ定義へ置く」)。

#include "kachakacha/app/Selection.h"
#include "kachakacha/base/Ids.h"

namespace kachakacha::v2::app {

//! いま画面が描き分ける意味状態。
//!
//! §3 は Input / Warning / Error / Locked / Hidden も挙げているが、
//! それらを出す元(ToolSession と診断の伝達)がまだ無い。
//! 出せないものを並べても、常に Default にしかならない欄が増えるだけである。
//! ToolSession が入った時点で足す(handover-ui-ux-2026-09-12.md §6.4)。
enum class SemanticState {
    Default,   //!< 通常表示。
    Hover,     //!< カーソル直下の候補。選択とは別の、軽い強調(§3 規則1)。
    Selected,  //!< 通常選択。
    Snap,      //!< 吸着点。画面上で一定の大きさに出す(§3 規則6)。
    Preview,   //!< まだ履歴へ入っていない生成結果(§3 規則3)。
};

//! 帯や診断に出す名前。番号で言われても、どの状態か読めない。
[[nodiscard]] const char* SemanticStateNameJa(SemanticState state) noexcept;

//! 線分1本がどの意味状態か。
//!
//! **選択が Hover より強い。** Hover だけで選択を変えない(§3 規則1)以上、
//! 選んだものの上へカーソルを置いたときに色が Hover へ落ちてはならない。
//! 落ちると「カーソルを載せると選択が外れた」ように見える。
//!
//! 照合は IsCurveSelected と同じ規則なので、物体ごと選べば全線分が Selected、
//! 線分を選べば **その線分だけ** が Selected になる。
//! hoveredSegmentId が Nil のときは、その物体のどの線分でも Hover とみなす
//! (塗った形のように線分を持たない候補を指しているとき)。
[[nodiscard]] SemanticState CurveSemanticState(const SelectionSet& selection,
    base::EntityId entityId, base::SegmentId segmentId,
    base::EntityId hoveredEntityId, base::SegmentId hoveredSegmentId);

//! 作図点1つがどの意味状態か。
//!
//! 点は線分を持たないので、照合は物体IDだけで行う。
//! 強弱の付け方(選択が Hover より強い)は線と同じにする。
//! 線と点で違う決め方をすると、同じ図の中で「選んだ印」の意味が2つになる。
[[nodiscard]] SemanticState PointSemanticState(const SelectionSet& selection,
    base::EntityId entityId, base::EntityId hoveredEntityId);

} // namespace kachakacha::v2::app
