#pragma once

//! 手で付ける役割(fabrication-contract.md §6、AT-FAB-006)。
//!
//! 利用者は既存のワイヤーへ役割を付けられる。大事な決まりが4つある。
//!   1. 役割を付けても、元のワイヤーは変えない。参照として持つだけ。
//!   2. 同じ範囲へ矛盾する役割を付けたら、commit しない。
//!   3. 手で付けた線は、自動で引き直した線より優先する。
//!   4. 元の形が変わって投影できなくなった線を、近い場所へ黙って動かさない。
//!      赤く出して、付け直す・無効にする・消すを選ばせる。
//!
//! V1 は4番をやっていた。作り直すたびに切れ目の位置が少しずつ動いた。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::fabrication {

using base::EntityId;
using base::SegmentId;

enum class ManualRole {
    PanelBoundary,  //!< 完全に分ける
    FoldLine,       //!< つないだまま折る
    ReliefCut,      //!< 途中まで切る
    Opening,        //!< 貫通穴
    KeepTogether,   //!< この線をまたいで同じ部材にする
    NoCutZone,      //!< この範囲に切れ目を入れない
    BendDirection,  //!< 曲げ母線の向きを指定する
};

[[nodiscard]] std::string_view ManualRoleNameJa(ManualRole value) noexcept;
[[nodiscard]] std::string_view ManualRoleName(ManualRole value) noexcept;

//! 参照の状態。
enum class ManualReferenceState {
    Valid,       //!< そのまま使える
    Broken,      //!< 指している線が無くなった
    Unprojectable, //!< 線はあるが、いまの形へ投影できない
    Disabled,    //!< 利用者が一時的に外した
};

[[nodiscard]] std::string_view ManualReferenceStateNameJa(
    ManualReferenceState value) noexcept;

//! 1件の割り当て。
struct ManualRoleAssignment {
    std::string assignmentId;
    ManualRole role = ManualRole::PanelBoundary;
    EntityId wireEntityId;
    SegmentId segmentId;
    //! 線のどこからどこまでか。0〜1。線1本ぜんぶなら 0 と 1。
    double fromParameter = 0.0;
    double toParameter = 1.0;
    ManualReferenceState state = ManualReferenceState::Valid;
    //! BendDirection のとき、向き(ラジアン)。ほかの役割では使わない。
    std::optional<double> bendAngleRad;
};

//! 矛盾の中身。
struct ManualRoleConflict {
    std::string firstAssignmentId;
    std::string secondAssignmentId;
    ManualRole firstRole = ManualRole::PanelBoundary;
    ManualRole secondRole = ManualRole::PanelBoundary;
    //! 重なっている範囲。
    double overlapFrom = 0.0;
    double overlapTo = 0.0;
    std::string reasonJa;
};

//! 2つの役割が同じ場所に共存できるか。
[[nodiscard]] bool RolesCanCoexist(ManualRole first, ManualRole second) noexcept;

//! 割り当ての一覧を調べる。矛盾が1件でもあれば値を返さない(commit しない)。
struct ManualRoleSet {
    std::vector<ManualRoleAssignment> assignments;
    //! 使えない参照。赤く出して選ばせる対象。
    std::vector<std::string> brokenAssignmentIds;
};

[[nodiscard]] base::Result<ManualRoleSet> ValidateManualRoles(
    const std::vector<ManualRoleAssignment>& assignments);

//! 矛盾だけを取り出す。画面で赤く出すため。値は返さない版。
[[nodiscard]] std::vector<ManualRoleConflict> FindManualRoleConflicts(
    const std::vector<ManualRoleAssignment>& assignments);

//! 参照が切れているものを見つける。
//! `existing` はいま文書にあるワイヤーの並び。
[[nodiscard]] std::vector<std::string> FindBrokenReferences(
    const std::vector<ManualRoleAssignment>& assignments,
    const std::vector<EntityId>& existingWireIds);

//! 壊れた参照への対処。近い場所へ黙って動かす選択肢は無い。
enum class BrokenReferenceAction {
    Reassign,   //!< 付け直す
    Disable,    //!< 無効にする
    Remove,     //!< 消す
};

[[nodiscard]] std::string_view BrokenReferenceActionNameJa(
    BrokenReferenceAction value) noexcept;

//! 壊れた参照へ対処した結果を返す。元の並びは変えない。
[[nodiscard]] base::Result<ManualRoleSet> ResolveBrokenReference(
    const ManualRoleSet& current, const std::string& assignmentId,
    BrokenReferenceAction action, std::optional<EntityId> newWireId);

} // namespace kachakacha::v2::fabrication
