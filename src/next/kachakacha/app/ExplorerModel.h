#pragma once

//! Model Explorer(左の一覧)の節(正本 3 HTML 2026-09-18、指示書 model_explorer)。
//!
//!   Project
//!   ├ 原点(Origin Point / X / Y / Z / XY / YZ / XZ)… 常に最上段
//!   ├ 作業面
//!   ├ グループ(入れ子。中に入れたものはここに出る)
//!   ├ ワイヤー
//!   ├ 面
//!   ├ 立体
//!   ├ 近似(近似モデルごとに 候補 / 部材 / 生成物)
//!   └ 生成物(固定で作ったもの)
//!
//! どのものがどの節に入るかは **ここが決める**。画面は並べるだけ。
//! グループに入っているものは「グループ」の節に(種類の節には出さない)。

#include "kachakacha/document/Document.h"
#include "kachakacha/domain/Entity.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

enum class ExplorerSection {
    Origin,
    WorkPlanes,
    Groups,
    Wires,
    Surfaces,
    Solids,
    Approximation,
    Generated,
};

[[nodiscard]] std::string_view ExplorerSectionNameJa(ExplorerSection section) noexcept;

//! 節の並び。正本のとおり、原点が先頭。
[[nodiscard]] const std::vector<ExplorerSection>& ExplorerSections();

//! グループに入っていないものが入る節。原点の平面は Origin。
//! 製作の「生成」で作り、生成元の近似モデルがあるものは Approximation(その近似モデルの下)。
//! 固定(FreezeDerived)で作ったものは Generated。
[[nodiscard]] ExplorerSection SectionForEntity(const document::DocumentSnapshot& snapshot,
    const domain::Entity& entity);

//! 製作の「生成」で作ったもので、生成元の近似モデルがまだ文書にあるなら、その近似モデル。
//! 一覧はこれを近似モデルの下の「生成物」に並べる(F-15)。グループに入れたものは
//! グループの節が先(ほかの物と同じ決まり)なので、ここでは見ない。
[[nodiscard]] std::optional<base::EntityId> GeneratingModelOf(
    const document::DocumentSnapshot& snapshot, const domain::Entity& entity);

//! 種類の名前(一覧の2列目)。
[[nodiscard]] std::string_view ExplorerKindNameJa(domain::EntityKind kind) noexcept;

//! 同じ名前が並ばないように番号を送る(「押し出し」「押し出し 2」「押し出し 3」…)。
//! 同じ種類の中で見る。名前が違えば見分けられるが、同じ名前が2つ並ぶと、
//! 足す・引くの土台と相手の欄に同じ字が出て、どちらがどちらか読めない
//! (PC 自己試験 HP-BO-01 2026-09-18)。
[[nodiscard]] std::string UniqueDisplayName(const document::DocumentSnapshot& snapshot,
    domain::EntityKind kind, const std::string& base);

} // namespace kachakacha::v2::app
