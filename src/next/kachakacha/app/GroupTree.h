#pragma once

//! 整理用のまとまり(フォルダ)を木として読む(オーナー指示 2026-09-14 §7〜13)。
//!
//! まとまりは **幾何ではない。** 入れても出しても、依存も参照も所有も変わらない。
//! ここは文書のまとまりを読むだけで、書き換えない。書き換えは Command が行う。
//!
//! いちばん大事なのは **見える/見えないの決め方** である。
//! まとまりを隠したときに中身の visibility を書き換えてしまうと、
//! 出し直したときに、利用者が1つずつ隠していたものまで全部出てしまう。
//! そこで中身は触らず、「自分が見える && 先祖のまとまりが全部見える」で決める。

#include "kachakacha/document/Document.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::app {

using base::GroupId;
using document::DocumentSnapshot;
using document::Group;

//! そのまとまりが、先祖まで含めて出ているか。無いまとまりは出ている扱い。
[[nodiscard]] bool GroupChainVisible(const DocumentSnapshot& snapshot,
    const std::optional<GroupId>& groupId);

//! その物が画面に出るか。自分の visibility と、入っているまとまりの両方で決まる。
[[nodiscard]] bool EntityEffectivelyVisible(const DocumentSnapshot& snapshot,
    const domain::Entity& entity);

//! まとまりの入れ子の深さ(最上位が 0)。輪があれば止まる。
[[nodiscard]] std::size_t GroupDepth(const DocumentSnapshot& snapshot,
    const GroupId& groupId);

//! 最上位から自分までの名前を「/」で繋いだもの。試験と画面の表示に使う。
[[nodiscard]] std::string GroupPathJa(const DocumentSnapshot& snapshot,
    const GroupId& groupId);

//! そのまとまりの直下にあるまとまり。並びは文書の並びのまま。
[[nodiscard]] std::vector<GroupId> ChildGroupsOf(const DocumentSnapshot& snapshot,
    const std::optional<GroupId>& parentId);

//! そのまとまり(と、その下のまとまり全部)に入っている物。
[[nodiscard]] std::vector<base::EntityId> EntitiesUnderGroup(
    const DocumentSnapshot& snapshot, const GroupId& groupId);

} // namespace kachakacha::v2::app
