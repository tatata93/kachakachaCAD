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

void Project::AddWire(std::string name, Wire wire)
{
    if (name.empty()) {
        throw std::invalid_argument("Wire name must not be empty.");
    }

    wires_.push_back({std::move(name), std::move(wire)});
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

