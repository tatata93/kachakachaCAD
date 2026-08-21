#include "kachakacha/model/Project.h"

#include <stdexcept>

namespace kachakacha::model {

void Project::AddWorkPlane(std::string name, WorkPlane plane)
{
    if (name.empty()) {
        throw std::invalid_argument("Work plane name must not be empty.");
    }

    if (FindWorkPlane(name).has_value()) {
        throw std::invalid_argument("Work plane name already exists: " + name);
    }

    workPlanes_.push_back({std::move(name), plane});
}

void Project::AddWire(std::string name, Wire wire, WireMetadata metadata)
{
    if (name.empty()) {
        throw std::invalid_argument("Wire name must not be empty.");
    }

    for (const NamedWire& existingWire : wires_) {
        if (existingWire.name == name) {
            throw std::invalid_argument("Wire name already exists: " + name);
        }
    }

    if (metadata.sourcePlaneName.has_value() && !FindWorkPlane(*metadata.sourcePlaneName).has_value()) {
        throw std::invalid_argument("Wire source plane does not exist: " + *metadata.sourcePlaneName);
    }

    wires_.push_back({std::move(name), std::move(wire), std::move(metadata)});
}

void Project::SetWireMetadata(std::string_view name, WireMetadata metadata)
{
    if (metadata.sourcePlaneName.has_value() && !FindWorkPlane(*metadata.sourcePlaneName).has_value()) {
        throw std::invalid_argument("Wire source plane does not exist: " + *metadata.sourcePlaneName);
    }

    for (NamedWire& wire : wires_) {
        if (wire.name == name) {
            wire.metadata = std::move(metadata);
            return;
        }
    }

    throw std::invalid_argument("Wire name does not exist: " + std::string(name));
}

std::optional<WorkPlane> Project::FindWorkPlane(std::string_view name) const
{
    for (const NamedWorkPlane& workPlane : workPlanes_) {
        if (workPlane.name == name) {
            return workPlane.plane;
        }
    }

    return std::nullopt;
}

} // namespace kachakacha::model
