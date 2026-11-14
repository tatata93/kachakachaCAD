#include "V2PointTransform.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/view/ViewOrientation.h"
#include <optional>

namespace v2ui {
using namespace kachakacha::v2;

bool IsTransformTarget(domain::EntityKind kind)
{
    return kind == domain::EntityKind::Point || kind == domain::EntityKind::Wire
        || kind == domain::EntityKind::Part;
}

namespace {
std::optional<geometry::Vector3> TransformedPoint(
    geometry::Vector3 point, const domain::TransformWireDefinition& transform)
{
    const auto relative = point - transform.pointArgument;
    switch (transform.method) {
    case domain::WireTransformMethod::Move:
    case domain::WireTransformMethod::Copy:
        return point + transform.vectorArgument;
    case domain::WireTransformMethod::Mirror: {
        const auto normal = geometry::Normalized(transform.vectorArgument);
        return point - normal * (2.0 * geometry::Dot(relative, normal));
    }
    case domain::WireTransformMethod::Rotate:
        return transform.pointArgument + view::Rotate(
            view::FromAxisAngle(transform.vectorArgument, transform.scalarArgument.value), relative);
    case domain::WireTransformMethod::Scale:
        return transform.pointArgument + relative * transform.scalarArgument.value;
    default:
        return std::nullopt;
    }
}
}

document::CommandResult TransformDrawingPoint(document::Document& document, base::IdGenerator& ids,
    base::EntityId id, const domain::TransformWireDefinition& transform, bool keepSource, const std::string& label)
{
    const auto* source = document.FindEntity(id);
    const auto* owner = source ? document.FindFeature(source->createdBy) : nullptr;
    const auto* definition = owner && owner->enabled && source->kind == domain::EntityKind::Point
        ? std::get_if<domain::CreatePointDefinition>(&owner->definition) : nullptr;
    if (!definition || (!keepSource && (source->editPolicy == domain::EditPolicy::Derived || owner->outputs.size() != 1))) {
        document::CommandResult result;
        result.diagnostics.push_back(base::MakeError("DOC-C001", "この作図点は直接変更できません。"));
        return result;
    }
    auto point = *definition;
    const auto position = TransformedPoint(point.positionMm, transform);
    if (!position || !position->IsFinite()) {
        document::CommandResult result;
        result.diagnostics.push_back(base::MakeError("DOC-C001", "変換後の作図点の位置を決められません。"));
        return result;
    }
    point.positionMm = *position;
    const auto setCoordinate = [](geometry::EvaluatedValue& coordinate, double before, double after) {
        if (before != after) coordinate = {"", after, geometry::QuantityKind::Length};
    };
    setCoordinate(point.xExpression, definition->positionMm.x, point.positionMm.x);
    setCoordinate(point.yExpression, definition->positionMm.y, point.positionMm.y);
    setCoordinate(point.zExpression, definition->positionMm.z, point.positionMm.z);
    if (!keepSource)
        return document.Run(document::UpdateFeatureDefinitionCommand(owner->id, point, owner->inputEntityIds, label));

    domain::Feature feature;
    feature.id = ids.NextTyped<base::IdKind::Feature>();
    feature.type = domain::FeatureType::CreatePoint;
    feature.displayName = label;
    point.sourcePlaneId.reset();
    feature.definition = point;
    auto copy = *source;
    copy.id = ids.NextTyped<base::IdKind::Entity>();
    copy.createdBy = feature.id;
    copy.editPolicy = domain::EditPolicy::Source;
    copy.visibility = domain::Visibility::Visible;
    copy.revision = 0;
    copy.generatedFrom.reset();
    feature.outputs.push_back({"point", copy.id, copy.kind});
    return document.Run(document::AddFeatureCommand(feature, {copy}, label));
}
}
