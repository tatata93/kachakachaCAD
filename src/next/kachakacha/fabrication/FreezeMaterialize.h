#pragma once

//! 固定した状態を、文書の中の実体にする(fabrication-contract.md §11、AT-FAB-011 / 014)。
//!
//! FreezeAssemblyState が作るのは「材料」である。それを Entity と Feature へ
//! 変えるのがここ。分けている理由は、材料を作るところは何度でも安全にやり直せる
//! のに対し、実体にするのは文書を変える一度きりの操作だからである。
//!
//! 大事な決まりが3つある。
//!   1. 固定する前の派生物は `Derived` で、直接編集できない。
//!   2. 固定して出来たものは `Frozen` で、元の製作モデルから独立する。
//!      元を変えても、固定したものは変わらない。
//!   3. `ワイヤーのみ / 部品のみ / 両方` で、出来る Entity の種類と数が変わる。
//!      両方のときは同じ評価の束から作られる。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/fabrication/FreezeState.h"

#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::fabrication {

using base::EntityId;
using base::GroupId;

//! 実体にした結果1つ。
struct MaterializedEntity {
    domain::EntityKind kind = domain::EntityKind::Wire;
    domain::EditPolicy editPolicy = domain::EditPolicy::Frozen;
    std::string displayName;
    //! ワイヤーのとき。曲線の種類を保つ。
    std::vector<CurveSegment> segments;
    //! 部品のとき。立体はカーネルが作るので、ここでは材料だけを持つ。
    std::optional<PanelSolidRequest> solidRequest;
    //! どの元から来たか。あとから辿れるようにする。
    std::string sourceId;
};

struct MaterializeResult {
    std::vector<MaterializedEntity> entities;
    //! 固定した割合。保存して読み直しても、この値で残る。
    double percent = 0.0;
    FreezeOutput output = FreezeOutput::Both;
};

//! 束を実体にする。
//!
//! `output` が WiresOnly ならワイヤーだけ、PartsOnly なら部品だけ、
//! Both なら両方が出る。無いものを数だけ合わせるために空の Entity を作らない。
[[nodiscard]] base::Result<MaterializeResult> MaterializeFrozenState(
    const FreezeBundle& bundle, std::string_view baseNameJa);

//! 固定する前の派生物が、直接編集できない印になっているか(§11)。
[[nodiscard]] bool IsDirectlyEditable(domain::EditPolicy policy) noexcept;

//! 固定したものが、元の製作モデルの変更に影響されないこと(AT-FAB-014)。
//!
//! 固定は値のコピーであって参照ではない、という決まりを機械で守る。
//! 参照で持つと、元を触ったときに固定したものまで動いてしまう。
[[nodiscard]] bool IsIndependentOfSource(const MaterializeResult& frozen,
    const FreezeBundle& changedSource);

} // namespace kachakacha::v2::fabrication
