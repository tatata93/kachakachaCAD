#include "kachakacha/model/Project.h"

#include <algorithm>
#include <stdexcept>

namespace kachakacha::model {

namespace {

geometry::Vector3 ReframePoint(
    geometry::Vector3 point,
    const WorkPlane& oldPlane,
    const WorkPlane& newPlane)
{
    const PlaneCoordinates coordinates = oldPlane.Project(point);
    return newPlane.ToWorld(coordinates.u, coordinates.v, coordinates.w);
}

geometry::Vector3 ReframeDirection(
    geometry::Vector3 direction,
    const WorkPlane& oldPlane,
    const WorkPlane& newPlane)
{
    return newPlane.UAxis() * geometry::Dot(direction, oldPlane.UAxis())
        + newPlane.VAxis() * geometry::Dot(direction, oldPlane.VAxis())
        + newPlane.Normal() * geometry::Dot(direction, oldPlane.Normal());
}

Wire ReframeWire(const Wire& wire, const WorkPlane& oldPlane, const WorkPlane& newPlane)
{
    std::vector<geometry::Vector3> points;
    points.reserve(wire.ControlPoints().size());
    for (const geometry::Vector3& point : wire.ControlPoints()) {
        points.push_back(ReframePoint(point, oldPlane, newPlane));
    }

    switch (wire.Kind()) {
    case WireKind::Line:
        return Wire::Line(points[0], points[1]);
    case WireKind::Polyline:
        return Wire::Polyline(std::move(points));
    case WireKind::CubicBezier:
        return Wire::CubicBezier(points[0], points[1], points[2], points[3]);
    case WireKind::Circle:
    case WireKind::CircularArc: {
        const WireArcData arc = wire.ArcData();
        return Wire::CircularArc(
            ReframePoint(arc.center, oldPlane, newPlane),
            ReframeDirection(arc.uAxis, oldPlane, newPlane),
            ReframeDirection(arc.vAxis, oldPlane, newPlane),
            arc.radius,
            arc.startAngleRadians,
            arc.sweepAngleRadians);
    }
    }
    throw std::logic_error("Unknown wire kind.");
}

} // namespace

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

void Project::UpdateWorkPlane(std::string_view name, WorkPlane plane)
{
    for (NamedWorkPlane& namedPlane : workPlanes_) {
        if (namedPlane.name != name) {
            continue;
        }

        const WorkPlane oldPlane = namedPlane.plane;
        namedPlane.plane = plane;
        for (NamedWire& wire : wires_) {
            if (wire.metadata.planePolicy == WirePlanePolicy::LockedToPlane
                && wire.metadata.sourcePlaneName.has_value()
                && *wire.metadata.sourcePlaneName == name) {
                wire.wire = ReframeWire(wire.wire, oldPlane, plane);
            }
        }
        return;
    }
    throw std::invalid_argument("Work plane name does not exist: " + std::string(name));
}

void Project::UpdateWire(std::string_view name, Wire wire)
{
    for (NamedWire& namedWire : wires_) {
        if (namedWire.name == name) {
            namedWire.wire = std::move(wire);
            return;
        }
    }
    throw std::invalid_argument("Wire name does not exist: " + std::string(name));
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

bool Project::RemoveWorkPlane(std::string_view name)
{
    const auto position = std::find_if(workPlanes_.begin(), workPlanes_.end(), [&](const NamedWorkPlane& plane) {
        return plane.name == name;
    });
    if (position == workPlanes_.end()) {
        return false;
    }

    workPlanes_.erase(position);
    for (NamedWire& wire : wires_) {
        if (wire.metadata.sourcePlaneName.has_value() && *wire.metadata.sourcePlaneName == name) {
            wire.metadata.sourcePlaneName.reset();
        }
    }
    return true;
}

bool Project::RemoveWire(std::string_view name)
{
    const auto position = std::find_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
        return wire.name == name;
    });
    if (position == wires_.end()) {
        return false;
    }

    wires_.erase(position);
    return true;
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
