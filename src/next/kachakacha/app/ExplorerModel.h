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
//! 固定(FreezeDerived)で作ったものは Generated。
[[nodiscard]] ExplorerSection SectionForEntity(const document::DocumentSnapshot& snapshot,
    const domain::Entity& entity);

//! 種類の名前(一覧の2列目)。
[[nodiscard]] std::string_view ExplorerKindNameJa(domain::EntityKind kind) noexcept;

} // namespace kachakacha::v2::app
