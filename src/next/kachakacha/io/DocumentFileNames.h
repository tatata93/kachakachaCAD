#pragma once

//! kcd2 の書き出しと読み込みが共有する名前の表(内部用)。
//!
//! DocumentFile.cpp(書き出し)と DocumentFileRead.cpp(読み込み)で同じ表を使う。
//! 表を2つ持つと、片方だけ足したときに「書けるのに読めない」が起きる。
//! 名前は kcd2-format.md の lower snake_case。並びは enum 定義順。

#include "kachakacha/domain/Entity.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <cstddef>
#include <optional>
#include <string>

namespace kachakacha::v2::io::detail {

using domain::EditPolicy;
using domain::EntityKind;
using domain::FeatureType;
using domain::PartRole;
using domain::Visibility;
using domain::WireTransformMethod;
using geometry::CurveKind;
using geometry::QuantityKind;

inline constexpr const char* kFormatName = "kachakachaCAD";
inline constexpr int kSchemaVersion = 2;
inline constexpr const char* kDocumentEntry = "document.json";

inline constexpr const char* kNotOurFile = "KCD2-D001";  //!< format / schemaVersion が違う
inline constexpr const char* kBadValue   = "KCD2-D002";  //!< 型違い、範囲外、UUIDでない
inline constexpr const char* kBadEnum    = "KCD2-D003";  //!< 知らない enum
inline constexpr const char* kBadShape   = "KCD2-D004";  //!< 参照切れ、重複ID、構造の矛盾

// ---------------------------------------------------------------- enum 表
// 名前は kcd2-format.md の lower snake_case。並びは enum 定義順。

template<class Enumeration>
struct NamedEnum {
    Enumeration value;
    const char* name;
};

inline constexpr NamedEnum<EntityKind> kEntityKinds[]{
    {EntityKind::Point, "point"},
    {EntityKind::WorkPlane, "work_plane"},
    {EntityKind::Wire, "wire"},
    {EntityKind::GuideSurface, "guide_surface"},
    {EntityKind::Part, "part"},
    {EntityKind::FabricationModel, "fabrication_model"},
    {EntityKind::Pattern, "pattern"},
};

inline constexpr NamedEnum<Visibility> kVisibilities[]{
    {Visibility::Visible, "visible"},
    {Visibility::Reference, "reference"},
    {Visibility::Hidden, "hidden"},
};

inline constexpr NamedEnum<EditPolicy> kEditPolicies[]{
    {EditPolicy::Source, "source"},
    {EditPolicy::Derived, "derived"},
    {EditPolicy::Frozen, "frozen"},
};

inline constexpr NamedEnum<PartRole> kPartRoles[]{
    {PartRole::FinishedModel, "finished_model"},
    {PartRole::FabricationPart, "fabrication_part"},
    {PartRole::Fixture, "fixture"},
    {PartRole::Imported, "imported"},
};

inline constexpr NamedEnum<FeatureType> kFeatureTypes[]{
    {FeatureType::CreatePoint, "create_point"},
    {FeatureType::CreateWorkPlane, "create_work_plane"},
    {FeatureType::CreateWire, "create_wire"},
    {FeatureType::TransformWire, "transform_wire"},
    {FeatureType::ProjectWire, "project_wire"},
    {FeatureType::CreateGuideSurface, "create_guide_surface"},
    {FeatureType::Extrude, "extrude"},
    {FeatureType::CreatePartFromWireCage, "create_part_from_wire_cage"},
    {FeatureType::Boolean, "boolean"},
    {FeatureType::CreateFabricationModel, "create_fabrication_model"},
    {FeatureType::CreatePattern, "create_pattern"},
    {FeatureType::FreezeDerived, "freeze_derived"},
    {FeatureType::ThickenSurface, "thicken_surface"},
};

inline constexpr NamedEnum<WireTransformMethod> kTransformMethods[]{
    {WireTransformMethod::Move, "move"},
    {WireTransformMethod::Copy, "copy"},
    {WireTransformMethod::Rotate, "rotate"},
    {WireTransformMethod::Mirror, "mirror"},
    {WireTransformMethod::Trim, "trim"},
    {WireTransformMethod::Extend, "extend"},
    {WireTransformMethod::Split, "split"},
    {WireTransformMethod::Join, "join"},
    {WireTransformMethod::Fillet, "fillet"},
    {WireTransformMethod::Chamfer, "chamfer"},
    {WireTransformMethod::Offset, "offset"},
    {WireTransformMethod::MeetLines, "meet_lines"},
    {WireTransformMethod::Coincident, "coincident"},
    {WireTransformMethod::Tangent, "tangent"},
    {WireTransformMethod::Curvature, "curvature"},
};

inline constexpr NamedEnum<CurveKind> kCurveKinds[]{
    {CurveKind::Line, "line"},
    {CurveKind::CircularArc, "circular_arc"},
    {CurveKind::Circle, "circle"},
    {CurveKind::CubicBezier, "cubic_bezier"},
    {CurveKind::CubicBSpline, "cubic_b_spline"},
};

inline constexpr NamedEnum<QuantityKind> kQuantityKinds[]{
    {QuantityKind::Length, "length"},
    {QuantityKind::Angle, "angle"},
    {QuantityKind::Scalar, "scalar"},
};

template<class Enumeration, std::size_t Count>
[[nodiscard]] const char* NameOf(const NamedEnum<Enumeration> (&table)[Count],
    Enumeration value)
{
    for (const NamedEnum<Enumeration>& item : table) {
        if (item.value == value) {
            return item.name;
        }
    }
    return "";
}

template<class Enumeration, std::size_t Count>
[[nodiscard]] std::optional<Enumeration> ValueOf(const NamedEnum<Enumeration> (&table)[Count],
    const std::string& name)
{
    for (const NamedEnum<Enumeration>& item : table) {
        if (name == item.name) {
            return item.value;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- 書き出し

} // namespace kachakacha::v2::io::detail
