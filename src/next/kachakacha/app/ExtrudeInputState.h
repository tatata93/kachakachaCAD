#pragma once

//! 押し出しの入力スロット(オーナー指示 2026-09-15 §4・§5・§9、UIの正本 §1)。
//!
//! これまでの押し出しは、決めるたびに `viewport_->Selection()` を読み直していた。
//! 画面の「対象」「輪郭」は、その読み直しの結果を文字列にしただけだった。
//! そのため
//!   - 下見を出したあとで選択が変わると、確定は別のものを読む
//!   - 道具を構えている最中に素のクリックをすると、前の選択が消える
//!   - 対象と輪郭を1本の文字列にしていたので、どちらを選び直すのか分からない
//! という3つが同時に起きていた。
//!
//! ここでは **入力を明示のスロットとして持つ**。画面の欄と1対1である。
//!   対象 / 輪郭 / 演算 / 範囲 / 方向 / 距離 / 出力
//!
//! 決め方はここ(core)が持つ。画面は映して、押されたことを伝えるだけにする。

#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/ExtrudeInput.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

//! 押し出しの出力。**何を作るか**を4つ独立に持つ(オーナー指示 2026-09-15)。
//!
//! これまでは「部品」「押し出し先の輪郭」「側面」の3つしか無かった。
//! 開始側の輪郭は作れなかった。
struct ExtrudeOutputs {
    //! ソリッド / 面。
    bool body = true;
    //! 開始側の輪郭ワイヤー。**元の輪郭を作り変えるのではなく、新しく作る。**
    bool startWire = false;
    //! 押し出し先の輪郭ワイヤー。
    bool endWire = false;
    //! 側面ワイヤー。
    bool sideWires = false;

    [[nodiscard]] bool Any() const noexcept
    {
        return body || startWire || endWire || sideWires;
    }
    [[nodiscard]] bool AnyWire() const noexcept
    {
        return startWire || endWire || sideWires;
    }
    [[nodiscard]] bool operator==(const ExtrudeOutputs& other) const noexcept
    {
        return body == other.body && startWire == other.startWire
            && endWire == other.endWire && sideWires == other.sideWires;
    }
};

//! 画面の「出力プリセット」。並びはそのまま画面へ出す。
enum class ExtrudeOutputPreset {
    SolidOnly,
    WiresOnly,
    WiresAndSolid,
    EndWireOnly,
    Custom,
};

//! そのプリセットが表す4つの ON/OFF。
[[nodiscard]] ExtrudeOutputs OutputsForPreset(ExtrudeOutputPreset preset) noexcept;
//! その4つが、どのプリセットに当たるか。どれにも当たらなければ Custom。
[[nodiscard]] ExtrudeOutputPreset PresetForOutputs(const ExtrudeOutputs& outputs) noexcept;
//! 画面に出す名前。台帳ではなく、ここが唯一の出どころ。
[[nodiscard]] std::string_view ExtrudeOutputPresetNameJa(ExtrudeOutputPreset preset) noexcept;
//! 画面に並べる順。並びを2か所に書かない。
[[nodiscard]] const std::vector<ExtrudeOutputPreset>& ExtrudeOutputPresets();
//! 出力を一言で。状態欄の「出力: ソリッド」に使う。
[[nodiscard]] std::string ExtrudeOutputsTextJa(const ExtrudeOutputs& outputs);

//! いま画面が埋めてほしがっているスロット。
enum class ExtrudeSlot {
    //! 足りないものは無い。
    None,
    //! 加工する立体。
    Target,
    //! 押す輪郭、または押す面。
    Profile,
};

//! そのスロットの名前。画面の見出しと同じ言葉を使う。
[[nodiscard]] std::string_view ExtrudeSlotNameJa(ExtrudeSlot slot) noexcept;

//! 拾ったものの種類。画面が拾い方を知っていて、core は種類だけ受け取る。
enum class PickedKind {
    None,
    //! 閉じた輪郭。
    ClosedWire,
    //! 開いた輪郭。
    OpenWire,
    //! 立体。
    Solid,
    //! 立体の面。
    SolidFace,
    //! それ以外(曲面など)。
    Other,
};

//! 画面が拾った1つ。
struct PickedEntity {
    base::EntityId entityId;
    PickedKind kind = PickedKind::None;
    //! 面のときだけ。いまの網の中での番号。**保存してはならない。**
    std::optional<std::size_t> faceIndex;
};

//! 押し出しの入力。**画面の欄と1対1。**
struct ExtrudeInputState {
    //! 加工する立体。無ければ「新しい部品」。
    std::optional<base::EntityId> target;
    //! 押す輪郭。面を押すときは、その面を持つ立体の番号が入る。
    std::vector<base::EntityId> profiles;
    //! 輪郭ではなく面を押しているか。
    bool profileIsFace = false;
    //! 面のときの番号。
    std::optional<std::size_t> faceIndex;

    modeling::ExtrudeBooleanMode operation = modeling::ExtrudeBooleanMode::NewPart;
    modeling::ExtrudeExtentMode extent = modeling::ExtrudeExtentMode::Distance;
    modeling::ExtrudeDirectionMode direction = modeling::ExtrudeDirectionMode::ProfileNormal;
    geometry::Vector3 customDirection{0.0, 0.0, 1.0};
    bool reversed = false;
    double distanceMm = 10.0;
    double secondDistanceMm = 0.0;
    std::optional<base::EntityId> extentTarget;
    ExtrudeOutputs outputs;
    //! 距離0でワイヤーだけ作ることを、利用者が承知しているか。
    bool zeroDistanceConfirmed = false;

    [[nodiscard]] bool HasProfile() const noexcept { return !profiles.empty(); }
    [[nodiscard]] bool HasTarget() const noexcept { return target.has_value(); }
};

//! 次に埋めてほしいスロット。**「あと何を選べばよいか」はここが決める。**
//!
//! 輪郭が無ければ輪郭。輪郭があって、足す/引くなのに相手がいなければ対象。
//! それ以外は None。
[[nodiscard]] ExtrudeSlot NextNeededSlot(const ExtrudeInputState& state) noexcept;

//! 拾ったものを、いま要求しているスロットへ入れる。
//!
//! **素のクリックで前の入力が消えない。**これまでは素のクリックが
//! 選択そのものを置き換えていたので、立体を選んでから輪郭をクリックすると
//! 立体が外れ、Ctrl を知らないと押し出しが始められなかった(§5)。
//!
//! `addToProfiles` は Ctrl のときだけ true。輪郭を足すのに使う。
//! 立体は常に1つなので、対象は必ず置き換える。
[[nodiscard]] ExtrudeInputState ApplyPick(const ExtrudeInputState& state,
    const PickedEntity& picked, bool addToProfiles);

//! そのスロットを埋めるのに使えるものか。拾う候補を絞るのに使う(§6)。
[[nodiscard]] bool PickFitsSlot(ExtrudeSlot slot, PickedKind kind) noexcept;

//! いまの入力で下見を出せるか。
[[nodiscard]] bool ReadyForPreview(const ExtrudeInputState& state) noexcept;

//! 拾った候補を、いま要求しているスロットに合う順へ並べ替える(§6)。
//!
//! ふだんの優先順は **点 → 線 → 形** で固定だった。面の上に線が載っていると
//! 線が先に取れるので、面を押したいのに元の輪郭が選ばれていた。
//!
//! ここでは **並びを変えるだけで、候補を捨てない。** 捨てると
//! 「見えているのに掴めない」が起きる。Tab と右クリックの送りもそのまま効く。
//! 合うものが1つも無ければ、渡された並びをそのまま返す。
[[nodiscard]] std::vector<PickedKind> SortKindsForSlot(ExtrudeSlot slot,
    const std::vector<PickedKind>& kinds);

//! 素のクリックで、前の選択に **足す** べきか(§5)。
//!
//! 道具が入力を集めている間、役割の違うものは足す。立体と輪郭は役割が違うので、
//! 輪郭を選んだあとに相手の立体を素でクリックしても輪郭は残る。
//! 同じ役割のものは置き換える(輪郭を選び直せる)。
//!
//! **「いま足りないスロット」で決めてはいけない。**
//! 輪郭が入った時点でスロットは None になるので、それで判断すると
//! 次に相手の立体を押した瞬間に輪郭が消える。判断に使うのは
//! 「道具が動いているか」である。
[[nodiscard]] bool PlainClickShouldAdd(bool toolActive, PickedKind picked,
    const std::vector<PickedKind>& already) noexcept;

//! 「状態」欄に出す行(§15)。診断コードに頼らず、通った道も言う。
//!
//! `targetNameJa` / `profileNamesJa` は画面が渡す表示名。
[[nodiscard]] std::vector<std::string> ExtrudeStatusLinesJa(const ExtrudeInputState& state,
    const std::string& targetNameJa, const std::vector<std::string>& profileNamesJa,
    bool previewShown);

//! 演算の欄を触れるか。**body を作らないなら、足す/引くは起きない**(オーナー指示)。
[[nodiscard]] bool OperationApplies(const ExtrudeInputState& state) noexcept;

} // namespace kachakacha::v2::app
