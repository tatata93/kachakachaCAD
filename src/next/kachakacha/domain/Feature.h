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

//! 作業平面。作り方と、それに要る入力を持つ。
//! 出来上がりの枠(原点と3軸)は再計算で出せるので、ここには持たない。
//! 持つと、入力を変えたのに枠が古いまま、という食い違いが起きる。
struct CreateWorkPlaneDefinition {
    //! modeling::WorkPlaneMethod と同じ並び。core への依存を増やさないため数で持つ。
    int method = 0;
    std::vector<EntityId> inputs;
    geometry::Vector3 origin{};
    geometry::Vector3 normal{0.0, 0.0, 1.0};
    geometry::Vector3 uDirection{1.0, 0.0, 0.0};
    geometry::EvaluatedValue offset;   //!< 平行移動の距離など
};

//! 線を面へ落とす。
struct ProjectWireDefinition {
    std::vector<EntityId> inputs;
    EntityId targetPlaneId;
    geometry::Vector3 direction{0.0, 0.0, -1.0};
};

//! 形状ガイドの面。役割ごとの線の並びを持つ。
struct CreateGuideSurfaceDefinition {
    //! modeling::GuideSurfaceMethod と同じ並び。
    int method = 0;
    //! 役割ごとの鎖。並びは modeling::ChainRole の順。
    std::vector<WireChainRef> chains;
    //! 各鎖の役割。chains と同じ長さ。
    std::vector<int> roles;
};

//! 押し出し。
struct ExtrudeDefinition {
    std::vector<EntityId> profiles;
    geometry::Vector3 direction{0.0, 0.0, 1.0};
    geometry::EvaluatedValue distance;
    //! modeling::ExtrudeExtentMode / ExtrudeBooleanMode と同じ並び。
    int extentMode = 0;
    int booleanMode = 0;
    std::vector<EntityId> targets;
};

//! 閉じたかごから部品を作る。
struct CreatePartFromWireCageDefinition {
    std::vector<EntityId> wires;
    geometry::EvaluatedValue thickness;
    //! 板厚をどちらへ付けるか。0=外側 1=中央 2=内側。
    int placement = 1;
};

//! 足す・引く。
struct BooleanDefinition {
    //! 0=足す 1=引く。
    int mode = 0;
    std::vector<EntityId> targets;
    std::vector<EntityId> tools;
};

//! 製作モデル。
struct CreateFabricationModelDefinition {
    std::vector<EntityId> parts;
    geometry::EvaluatedValue materialThickness;
    geometry::EvaluatedValue targetMaxDeviation;
    int fidelity = 6;
};

//! 型紙。
struct CreatePatternDefinition {
    std::vector<EntityId> fabricationModels;
    geometry::EvaluatedValue pageWidth;
    geometry::EvaluatedValue pageHeight;
    geometry::EvaluatedValue marginMm;
};

//! 種類ごとの定義。まだ実装していないFeatureは空の定義を持つ。
using FeatureDefinition = std::variant<std::monostate, CreatePointDefinition,
    CreateWireDefinition, TransformWireDefinition, FreezeDerivedDefinition,
    CreateWorkPlaneDefinition, ProjectWireDefinition, CreateGuideSurfaceDefinition,
    ExtrudeDefinition, CreatePartFromWireCageDefinition, BooleanDefinition,
    CreateFabricationModelDefinition, CreatePatternDefinition>;

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
