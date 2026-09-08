#pragma once

//! Feature DAG。文書の正本はこちらで、Entityは識別と表示だけを持つ。
//! methodごとにFeature typeを増やさない(architecture-and-data.md §7)。

#include "kachakacha/base/Ids.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/Expression.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kachakacha::v2::domain {

enum class FeatureType {
    CreatePoint,
    CreateWorkPlane,
    CreateWire,
    TransformWire,
    ProjectWire,
    CreateGuideSurface,
    Extrude,
    CreatePartFromWireCage,
    Boolean,
    CreateFabricationModel,
    CreatePattern,
    FreezeDerived,
};

[[nodiscard]] constexpr std::string_view FeatureTypeName(FeatureType type) noexcept
{
    switch (type) {
    case FeatureType::CreatePoint:            return "CreatePoint";
    case FeatureType::CreateWorkPlane:        return "CreateWorkPlane";
    case FeatureType::CreateWire:             return "CreateWire";
    case FeatureType::TransformWire:          return "TransformWire";
    case FeatureType::ProjectWire:            return "ProjectWire";
    case FeatureType::CreateGuideSurface:     return "CreateGuideSurface";
    case FeatureType::Extrude:                return "Extrude";
    case FeatureType::CreatePartFromWireCage: return "CreatePartFromWireCage";
    case FeatureType::Boolean:                return "Boolean";
    case FeatureType::CreateFabricationModel: return "CreateFabricationModel";
    case FeatureType::CreatePattern:          return "CreatePattern";
    case FeatureType::FreezeDerived:          return "FreezeDerived";
    }
    return "Unknown";
}

//! ワイヤー編集の方法。V1の編集アクションをここへ集める。
enum class WireTransformMethod {
    Move,
    Copy,
    Rotate,
    Mirror,
    Trim,
    Extend,
    Split,
    Join,
    Fillet,
    Chamfer,
    Offset,
    MeetLines,
    Coincident,
    Tangent,
    Curvature,
};

enum class BooleanOperation {
    New,
    Add,
    Cut,
};

// ---- 参照(architecture-and-data.md §6) ----

struct EntityRef {
    EntityId entityId;
};

struct SegmentRef {
    EntityId entityId;
    SegmentId segmentId;
    double startParameter = 0.0;
    double endParameter = 1.0;
};

struct SubshapeRef {
    EntityId partId;
    //! Feature由来の意味的キー。OCCTの一時Face番号を保存してはならない。
    std::string subshapeKey;
};

struct WireChainRef {
    std::vector<SegmentRef> segments;
    std::vector<bool> reversed;
};

// ---- Feature定義 ----

struct CreatePointDefinition {
    geometry::Vector3 positionMm;
    std::optional<EntityId> sourcePlaneId;
    geometry::EvaluatedValue xExpression;
    geometry::EvaluatedValue yExpression;
    geometry::EvaluatedValue zExpression;
};

struct CreateWireDefinition {
    std::vector<geometry::CurveSegment> segments;
    std::vector<SegmentId> segmentIds;
    std::optional<EntityId> sourcePlaneId;
    bool construction = false;
};

struct TransformWireDefinition {
    WireTransformMethod method = WireTransformMethod::Move;
    std::vector<SegmentRef> inputs;
    geometry::Vector3 vectorArgument;   //!< 移動量、軸方向、面法線など
    geometry::Vector3 pointArgument;    //!< 軸上の点、面上の点など
    geometry::EvaluatedValue scalarArgument; //!< 距離、半径、角度など
};

struct FreezeDerivedDefinition {
    std::vector<EntityId> sources;
};

//! 種類ごとの定義。まだ実装していないFeatureは空の定義を持つ。
using FeatureDefinition = std::variant<std::monostate, CreatePointDefinition,
    CreateWireDefinition, TransformWireDefinition, FreezeDerivedDefinition>;

struct FeatureOutput {
    std::string key;   //!< 再計算で同じ出力を指し続けるための安定キー
    EntityId entityId;
    EntityKind kind = EntityKind::Point;
};

struct Feature {
    FeatureId id;
    FeatureType type = FeatureType::CreatePoint;
    std::string displayName;
    bool enabled = true;
    FeatureDefinition definition;
    std::vector<FeatureOutput> outputs;
    std::uint64_t revision = 0;

    //! このFeatureが入力として参照しているEntity。DAGの辺はここから作る。
    std::vector<EntityId> inputEntityIds;

    //! 派生物を置くグループ(architecture-and-data.md §11)。
    //! 部品・形状ガイド・近似ワイヤー・型紙は派生物であり、
    //! 作業中グループではなく、ここが指すグループへ入る。
    //! 指定が無ければ作業中グループへ入る(利用者が自分で作ったもの)。
    std::optional<GroupId> derivedGroupId;
};

} // namespace kachakacha::v2::domain
