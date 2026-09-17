#pragma once

//! 「面を作る」の入力スロット(オーナー指示 2026-09-15 §4・§10〜§13、UIの正本)。
//!
//! いまの面生成は、人から見て2本立てだった。
//!   - 「形状ガイド」= 選んだ線を全部 **断面** にして、本数で方式が決まる
//!   - 役割表      = 方式を先に決め、役割を選んで行を足し、表から作る
//! 前者では平面も案内付きロフトも境界埋めも作れず、後者は後ろの札にあって、
//! しかも **方式を変える前に要らない行を自分で消さないと UI-R003 で断られた。**
//!
//! ここでは入力を1組のスロットとして持つ。**画面の欄と1対1。**
//!   作り方 / 断面 / ガイド / 境界 / 断面順
//!
//! 方式を変えても入力は捨てない。使うか、使わないか、足りないかを言い直すだけ。
//! 内部で `GuideTable` を組み立てるのは、作る直前の1回だけにする。

#include "kachakacha/base/Ids.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! 断面の並べ方(UIの正本「3. 断面順」)。
enum class SurfaceOrdering {
    //! CAD が幾何の位置から並べる。**採用した順を画面へ番号で出す。**
    Auto,
    //! 画面の 1,2,3... をそのまま生成順にする。カーネルは並べ替えない。
    ManualLock,
};

//! スロットが、いまの作り方でどう扱われるか。
enum class SurfaceSlotState {
    //! 使う。入っている。
    Used,
    //! 使う。まだ入っていない。
    Missing,
    //! この作り方では使わない。**入っていても捨てない。**
    NotUsedByMethod,
};

//! 画面の1欄ぶん。
struct SurfaceSlotView {
    modeling::ChainRole role = modeling::ChainRole::Section;
    SurfaceSlotState state = SurfaceSlotState::Missing;
    std::size_t count = 0;
};

//! 「面を作る」の入力。**画面の欄と1対1。**
struct SurfaceInputState {
    modeling::GuideSurfaceMethod method = modeling::GuideSurfaceMethod::LoftSections;
    //! 人が作り方を選んだか。選んでいなければ、選択から推奨したものを使う。
    bool methodChosenByUser = false;
    //! 断面。並びはそのまま生成順の候補になる。
    std::vector<base::EntityId> sections;
    //! ガイド(外形U)。
    std::vector<base::EntityId> guides;
    //! 境界(境界辺)。
    std::vector<base::EntityId> boundaries;
    //! 離した面のもと(OffsetGuide のとき)。
    std::vector<base::EntityId> sourceSurfaces;
    SurfaceOrdering ordering = SurfaceOrdering::Auto;
    //! 手動固定のときの並び。空なら `sections` の並びをそのまま使う。
    std::vector<base::EntityId> explicitOrder;
    //! **3D の次のクリックが入る欄**(引継ぎ 2026-09-17 の 1)。
    //! 欄の鍵は `SurfaceSlotKey` と同じ(Section / GuideU / BoundarySide / SourceSurface)。
    //! これが無いと、断面を入れたあとにガイドを入れる道が無かった。
    //! 常に画面に出す。「今どこへ入るのか」を人に推測させない。
    modeling::ChainRole activeSlot = modeling::ChainRole::Section;

    [[nodiscard]] bool Empty() const noexcept
    {
        return sections.empty() && guides.empty() && boundaries.empty()
            && sourceSurfaces.empty();
    }
};

//! 選んだものから分かる事実。画面が数えて渡す。
struct SurfaceSelectionFacts {
    //! 閉じていて、同じ平面に載っている輪郭の数。
    std::size_t closedPlanarWires = 0;
    //! 閉じた輪郭(平面かどうかは問わない)。
    std::size_t closedWires = 0;
    //! 開いた輪郭。
    std::size_t openWires = 0;
    //! 形状ガイドの面。
    std::size_t guideSurfaces = 0;
};

//! 選んだものから作り方を薦める(§11)。**強制ではない。**
//!
//!   閉じた同一平面の輪郭1本 → 平面
//!   断面2本                 → ルールド
//!   断面3本以上             → ロフト
//!   形状ガイドの面1枚       → 離した面
//!
//! 「案内付きロフト」と「境界埋め」は、役割を人が決めたときに選ばれる。
//! 選んだ線だけからは、断面なのかガイドなのかを決められない。
[[nodiscard]] modeling::GuideSurfaceMethod RecommendSurfaceMethod(
    const SurfaceSelectionFacts& facts) noexcept;

//! 画面の欄は「断面 / ガイド / 境界」の3つ(UIの正本「2. 入力」)。
//! その3つを表す鍵。内部の役割(ChainRole)はもう少し細かい。
//!
//!   断面 → Section(曲線網のときだけ 外形U)
//!   ガイド → 外形U(曲線網のときだけ 外形V)
//!   境界 → 境界辺(平面のときだけ 外形)
//!
//! **UI_DEVIATION_REQUEST(正本の曖昧点)**
//!   正本の「面を作る」は入力欄が3つで、その中に「曲線網(U/Vネットワーク)」が
//!   ある。U と V を別々に入れる欄が正本に無い。ここでは
//!   「断面 = U、ガイド = V」として実装した。正本の
//!   「案内付きロフト = 断面 + ガイド」と同じ読み方で、欄を増やしていない。
//!   別の割り当てが正しければ指示をください。欄は勝手に増やしていない。
[[nodiscard]] constexpr modeling::ChainRole SurfaceSlotKey(int index) noexcept
{
    return index == 0 ? modeling::ChainRole::Section
                      : (index == 1 ? modeling::ChainRole::GuideU
                                    : modeling::ChainRole::BoundarySide);
}

//! 画面の欄が、その作り方で受け持つ内部の役割。使わない欄なら偽を返す。
[[nodiscard]] bool RoleForSurfaceSlot(modeling::GuideSurfaceMethod method,
    modeling::ChainRole slot, modeling::ChainRole& role) noexcept;

//! 選んだものを最初に入れる欄。「面を作る」を押した直後に使う。
[[nodiscard]] modeling::ChainRole DefaultSurfaceIntakeSlot(
    modeling::GuideSurfaceMethod method) noexcept;

//! いまの入力を、作り方から見てどう扱うか(§11)。
//!
//! **入力を捨てない。**使うか、使わないか、足りないかを言うだけ。
//! これまでは方式を変える前に要らない行を自分で消さないと UI-R003 で断られた。
[[nodiscard]] std::vector<SurfaceSlotView> SurfaceSlotsFor(const SurfaceInputState& state);

//! その役割に入っているものを取り出す。
[[nodiscard]] const std::vector<base::EntityId>& SurfaceSlotEntries(
    const SurfaceInputState& state, modeling::ChainRole role);

//! その役割へ入れる。同じものは二度入れない。
[[nodiscard]] SurfaceInputState WithSurfaceEntries(const SurfaceInputState& state,
    modeling::ChainRole role, const std::vector<base::EntityId>& ids, bool replace);

//! 3D で押したものを、いまの欄へ **入れる/外す**(引継ぎ 2026-09-17 の 1)。
//!
//! 入っていなければ足し、入っていれば外す。Ctrl は要らない。
//! 押したものが別の欄に入っているときは、その欄から外して(1つのものが
//! 2つの役割を持たない)いまの欄へ入れる。
[[nodiscard]] SurfaceInputState WithSurfaceEntriesToggled(const SurfaceInputState& state,
    modeling::ChainRole slot, const std::vector<base::EntityId>& ids);

//! それらをどの欄からも外す(解除)。手動固定の並びからも外す。古い入力を残さない。
[[nodiscard]] SurfaceInputState WithoutSurfaceEntries(const SurfaceInputState& state,
    const std::vector<base::EntityId>& ids);

//! その欄を空にする。ほかの欄は触らない。
[[nodiscard]] SurfaceInputState WithSurfaceSlotCleared(const SurfaceInputState& state,
    modeling::ChainRole slot);

//! いまの欄を変える。その作り方で使わない欄は選べない(偽を返し、変えない)。
[[nodiscard]] bool CanActivateSurfaceSlot(const SurfaceInputState& state,
    modeling::ChainRole slot) noexcept;
[[nodiscard]] SurfaceInputState WithActiveSurfaceSlot(const SurfaceInputState& state,
    modeling::ChainRole slot);

//! 作り方を変えたあと、いまの欄がその作り方で使えなければ既定の欄へ戻す。
[[nodiscard]] SurfaceInputState WithActiveSlotSettled(const SurfaceInputState& state);

//! どの欄にも入っているものの合計(重複なし)。3D の選択の印をこれに合わせる。
[[nodiscard]] std::vector<base::EntityId> AllSurfaceEntries(const SurfaceInputState& state);

//! そのものが入っている欄。どこにも無ければ偽。
[[nodiscard]] bool SurfaceSlotHolding(const SurfaceInputState& state,
    const base::EntityId& id, modeling::ChainRole& slot) noexcept;

//! 「次のクリック → 断面(2本目)」。帯と一番下の一行に出す。
[[nodiscard]] std::string SurfaceActiveSlotHintJa(const SurfaceInputState& state);

//! 実際に生成へ渡す断面の並び(§13)。
//!
//! 手動固定なら画面の並びをそのまま。自動なら、並べ替えた結果を画面へ返せるように
//! 呼ぶ側が採用順を書き戻す。**どちらでも、最終の順が画面に出る。**
[[nodiscard]] std::vector<base::EntityId> SurfaceSectionOrder(
    const SurfaceInputState& state);

//! いまの入力で作れるか。作れないなら、何が足りないかを言う。
[[nodiscard]] bool SurfaceReadyToBuild(const SurfaceInputState& state);

//! 本数が合っていないときの理由。合っていれば空(§15)。
//!
//! 「まだ作れません」だけでは、**足りないのか多すぎるのかが分からない。**
//! ルールドに断面を3本入れたときは「2本にするか、ロフトにしてください」と言う。
[[nodiscard]] std::string SurfaceCountProblemJa(const SurfaceInputState& state);

//! 「4. 状態」に出す行(§15)。入力数・不足・生成可否・下見の様子。
//!
//! `deviationNoteJa` は、近づけて作る面が線からどれだけ外れているか
//! (`modeling/SurfaceDeviationLimit.h` が作る言葉)。空なら出さない。
//! **下見に出ている面のことなので、ここに出さないと人は知らずに確定する。**
[[nodiscard]] std::vector<std::string> SurfaceStatusLinesJa(const SurfaceInputState& state,
    bool previewShown, const std::string& deviationNoteJa = {});

//! 画面に並べる主要6方式(UIの正本「1. 作り方」)。並びを2か所に書かない。
[[nodiscard]] const std::vector<modeling::GuideSurfaceMethod>& MainSurfaceMethods();
//! 主要6方式に入らない、残りの作り方(「その他」へ置く)。既存機能は消さない。
[[nodiscard]] const std::vector<modeling::GuideSurfaceMethod>& OtherSurfaceMethods();

//! 役割の名前。画面の欄の見出しと同じ言葉を使う。
[[nodiscard]] std::string_view SurfaceSlotNameJa(modeling::ChainRole role) noexcept;

} // namespace kachakacha::v2::app
