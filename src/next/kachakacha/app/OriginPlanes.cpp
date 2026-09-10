#include "kachakacha/app/OriginPlanes.h"

#include "kachakacha/document/Commands.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/domain/Feature.h"

namespace kachakacha::v2::app {

using base::EntityId;
using modeling::StandardPlaneKind;

const std::array<OriginPlaneSpec, 3>& OriginPlaneSpecs()
{
    static const std::array<OriginPlaneSpec, 3> specs{
        OriginPlaneSpec{StandardPlaneKind::XY, "top_XY"},
        OriginPlaneSpec{StandardPlaneKind::ZX, "front_XZ"},
        OriginPlaneSpec{StandardPlaneKind::YZ, "side_YZ"},
    };
    return specs;
}

namespace {

[[nodiscard]] const domain::Feature* FeatureOf(const document::DocumentSnapshot& snapshot,
    const domain::Entity& entity)
{
    for (const auto& feature : snapshot.features) {
        if (feature.id == entity.createdBy) {
            return &feature;
        }
    }
    return nullptr;
}

[[nodiscard]] bool MatchesKind(const domain::CreateWorkPlaneDefinition& definition,
    StandardPlaneKind kind)
{
    const auto frame = modeling::StandardPlane(kind);
    return (definition.normal - frame.normal).Length() < 1.0e-9
        && (definition.uDirection - frame.uAxis).Length() < 1.0e-9;
}

} // namespace

bool IsOriginPlane(const document::DocumentSnapshot& snapshot, const EntityId& id)
{
    for (const auto& entity : snapshot.entities) {
        if (entity.id != id) {
            continue;
        }
        const auto* feature = FeatureOf(snapshot, entity);
        const auto* plane = feature == nullptr
            ? nullptr
            : std::get_if<domain::CreateWorkPlaneDefinition>(&feature->definition);
        return plane != nullptr && plane->isOriginPlane;
    }
    return false;
}

std::optional<EntityId> OriginPlaneId(const document::DocumentSnapshot& snapshot,
    StandardPlaneKind kind)
{
    for (const auto& entity : snapshot.entities) {
        if (entity.kind != domain::EntityKind::WorkPlane) {
            continue;
        }
        const auto* feature = FeatureOf(snapshot, entity);
        const auto* plane = feature == nullptr
            ? nullptr
            : std::get_if<domain::CreateWorkPlaneDefinition>(&feature->definition);
        if (plane != nullptr && plane->isOriginPlane && MatchesKind(*plane, kind)) {
            return entity.id;
        }
    }
    return std::nullopt;
}

bool EnsureOriginPlanes(document::Document& document, base::IdGenerator& ids,
    std::array<EntityId, 3>* idsOut)
{
    bool changed = false;
    std::size_t index = 0;
    for (const OriginPlaneSpec& spec : OriginPlaneSpecs()) {
        const auto existing = OriginPlaneId(document.Snapshot(), spec.kind);
        if (existing.has_value()) {
            if (idsOut != nullptr) {
                (*idsOut)[index] = *existing;
            }
            ++index;
            continue;
        }
        const auto frame = modeling::StandardPlane(spec.kind);
        domain::Feature feature;
        feature.id = ids.NextTyped<base::IdKind::Feature>();
        feature.type = domain::FeatureType::CreateWorkPlane;
        feature.displayName = std::string(spec.name);
        domain::CreateWorkPlaneDefinition definition;
        definition.method = static_cast<int>(modeling::WorkPlaneMethod::Standard);
        definition.isOriginPlane = true;
        definition.origin = frame.origin;
        definition.normal = frame.normal;
        definition.uDirection = frame.uAxis;
        feature.definition = std::move(definition);
        domain::Entity entity;
        entity.id = ids.NextTyped<base::IdKind::Entity>();
        entity.kind = domain::EntityKind::WorkPlane;
        entity.displayName = std::string(spec.name);
        entity.createdBy = feature.id;
        feature.outputs.push_back(
            domain::FeatureOutput{"plane", entity.id, domain::EntityKind::WorkPlane});
        const auto added = document.Run(
            document::AddFeatureCommand(feature, {entity}, "原点の基準平面"));
        if (added.committed) {
            changed = true;
            if (idsOut != nullptr) {
                (*idsOut)[index] = entity.id;
            }
        }
        ++index;
    }
    return changed;
}

} // namespace kachakacha::v2::app
