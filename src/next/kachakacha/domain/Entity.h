#pragma once

//! Entity は「誰であるか」と表示用の付随情報だけを正本として持つ。
//! 幾何そのものは Feature を評価した結果であり、Entity と Feature へ二重に持たない
//! (architecture-and-data.md §5)。

#include "kachakacha/base/Ids.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::domain {

using base::EntityId;
using base::FeatureId;
using base::GroupId;
using base::SegmentId;

enum class EntityKind {
    Point,
    WorkPlane,
    Wire,
    GuideSurface,
    Part,
    FabricationModel,
    Pattern,
};

[[nodiscard]] constexpr std::string_view EntityKindName(EntityKind kind) noexcept
{
    switch (kind) {
    case EntityKind::Point:            return "Point";
    case EntityKind::WorkPlane:        return "WorkPlane";
    case EntityKind::Wire:             return "Wire";
    case EntityKind::GuideSurface:     return "GuideSurface";
    case EntityKind::Part:             return "Part";
    case EntityKind::FabricationModel: return "FabricationModel";
    case EntityKind::Pattern:          return "Pattern";
    }
    return "Unknown";
}

//! 利用者向けの種類名。Surface/Plate/Body を通常UIへ出さない(PRD §3)。
[[nodiscard]] constexpr std::string_view EntityKindNameJa(EntityKind kind) noexcept
{
    switch (kind) {
    case EntityKind::Point:            return "作図点";
    case EntityKind::WorkPlane:        return "作業平面";
    case EntityKind::Wire:             return "ワイヤー";
    case EntityKind::GuideSurface:     return "形状ガイド";
    case EntityKind::Part:             return "部品";
    case EntityKind::FabricationModel: return "製作モデル";
    case EntityKind::Pattern:          return "型紙";
    }
    return "不明";
}

enum class Visibility {
    Visible,
    Reference,  //!< 薄く出るが、選択・編集の対象にしない
    Hidden,
};

enum class EditPolicy {
    Source,   //!< 利用者が描いた正本
    Derived,  //!< Featureが作った派生。直接編集しない
    Frozen,   //!< 派生を固定して独立させたもの
};

//! 部品の用途(PRD-014)。幾何の種類を増やさず、属性で分ける。
enum class PartRole {
    FinishedModel,
    FabricationPart,
    Fixture,
    Imported,
};

struct ManufacturingProperties {
    std::string materialName;
    std::string colorName;
    std::string processName;
    //! 基準板厚。0は「厚み0」を意味する。未指定は値を持たないこと。
    std::optional<double> nominalThicknessMm;
    std::optional<double> scaleDenominator;
    std::string note;
    //! 積層の重ね枚数(D-3)。専用オブジェクトを作らず、ここで表す。
    int layerCount = 1;
};

struct Entity {
    EntityId id;
    EntityKind kind = EntityKind::Point;
    std::string displayName;
    std::optional<GroupId> groupId;
    Visibility visibility = Visibility::Visible;
    EditPolicy editPolicy = EditPolicy::Source;
    FeatureId createdBy;
    std::uint64_t revision = 0;

    //! 種類別の付随情報。幾何ではなく、Featureが決められない属性だけを置く。
    PartRole partRole = PartRole::FinishedModel;
    std::optional<ManufacturingProperties> manufacturing;
    bool construction = false; //!< 補助線(V1同等性の要件)
    //! 基準線。測定や位置合わせの基準として印を付けたもの(V1の「基準線に設定」)。
    //! 幾何は変わらない。表示と、測定のときの既定の相手が変わるだけ。
    bool datum = false;
};

} // namespace kachakacha::v2::domain
