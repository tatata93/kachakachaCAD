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

    wires_.push_back({std::move(name), std::move(wire), std::move(metadata), std::nullopt});
}

void Project::AddPlanarSurface(std::string name, std::string boundaryWireName)
{
    if (name.empty()) {
        throw std::invalid_argument("Surface name must not be empty.");
    }
    if (FindSurface(name).has_value()) {
        throw std::invalid_argument("Surface name already exists: " + name);
    }
    const NamedWire& boundary = RequireWire(boundaryWireName);
    if (boundary.projection.has_value()) {
        throw std::invalid_argument("Projected wire cannot be used as a planar surface source.");
    }
    surfaces_.push_back({std::move(name), Surface::Planar(boundary.wire), {std::move(boundaryWireName)}});
}

void Project::AddRuledSurface(std::string name, std::string firstSectionName, std::string secondSectionName)
{
    if (name.empty()) {
        throw std::invalid_argument("Surface name must not be empty.");
    }
    if (FindSurface(name).has_value()) {
        throw std::invalid_argument("Surface name already exists: " + name);
    }
    if (firstSectionName == secondSectionName) {
        throw std::invalid_argument("Ruled surface requires two different section wires.");
    }
    const NamedWire& first = RequireWire(firstSectionName);
    const NamedWire& second = RequireWire(secondSectionName);
    if (first.projection.has_value() || second.projection.has_value()) {
        throw std::invalid_argument("Projected wire cannot be used as a ruled surface section.");
    }
    surfaces_.push_back({
        std::move(name),
        Surface::Ruled(first.wire, second.wire),
        {std::move(firstSectionName), std::move(secondSectionName)},
    });
}

void Project::AddLoftSurface(std::string name, std::vector<std::string> sectionNames)
{
    if (name.empty()) {
        throw std::invalid_argument("Surface name must not be empty.");
    }
    if (FindSurface(name).has_value()) {
        throw std::invalid_argument("Surface name already exists: " + name);
    }
    if (sectionNames.size() < 3) {
        throw std::invalid_argument("Loft surface requires at least three section wires.");
    }

    std::vector<Wire> sections;
    sections.reserve(sectionNames.size());
    for (const std::string& sectionName : sectionNames) {
        if (std::count(sectionNames.begin(), sectionNames.end(), sectionName) != 1) {
            throw std::invalid_argument("Loft section wires must not be repeated: " + sectionName);
        }
        const NamedWire& section = RequireWire(sectionName);
        if (section.projection.has_value()) {
            throw std::invalid_argument("Projected wire cannot be used as a loft section.");
        }
        sections.push_back(section.wire);
    }
    surfaces_.push_back({std::move(name), Surface::Loft(std::move(sections)), std::move(sectionNames)});
}

void Project::AddPlate(
    std::string name,
    std::string sourceSurfaceName,
    double thickness,
    PlateThicknessDirection direction,
    std::string material)
{
    if (name.empty()) {
        throw std::invalid_argument("Plate name must not be empty.");
    }
    if (FindPlate(name).has_value()) {
        throw std::invalid_argument("Plate name already exists: " + name);
    }
    if (material.empty()) {
        throw std::invalid_argument("Plate material must not be empty.");
    }
    const std::optional<Surface> surface = FindSurface(sourceSurfaceName);
    if (!surface.has_value()) {
        throw std::invalid_argument("Plate source surface does not exist: " + sourceSurfaceName);
    }
    plates_.push_back({
        std::move(name),
        Plate(*surface, thickness, direction),
        std::move(sourceSurfaceName),
        std::move(material),
        {},
    });
}

void Project::AddProjectedWire(
    std::string name,
    std::string sourceWireName,
    std::string targetSurfaceName,
    geometry::Vector3 direction)
{
    if (name.empty()) {
        throw std::invalid_argument("Projected wire name must not be empty.");
    }
    for (const NamedWire& existingWire : wires_) {
        if (existingWire.name == name) {
            throw std::invalid_argument("Wire name already exists: " + name);
        }
    }
    const NamedWire& source = RequireWire(sourceWireName);
    if (source.projection.has_value()) {
        throw std::invalid_argument("A projected wire cannot be used as another projection source.");
    }
    const std::optional<Surface> surface = FindSurface(targetSurfaceName);
    if (!surface.has_value()) {
        throw std::invalid_argument("Projection surface does not exist: " + targetSurfaceName);
    }
    Wire projected = surface->ProjectWireAlongDirection(source.wire, direction);
    wires_.push_back({
        std::move(name),
        std::move(projected),
        {},
        NamedWire::Projection{std::move(sourceWireName), std::move(targetSurfaceName), direction},
    });
}

void Project::UpdateWorkPlane(std::string_view name, WorkPlane plane)
{
    Project candidate = *this;
    for (NamedWorkPlane& namedPlane : candidate.workPlanes_) {
        if (namedPlane.name != name) {
            continue;
        }

        const WorkPlane oldPlane = namedPlane.plane;
        namedPlane.plane = plane;
        for (NamedWire& wire : candidate.wires_) {
            if (wire.metadata.planePolicy == WirePlanePolicy::LockedToPlane
                && wire.metadata.sourcePlaneName.has_value()
                && *wire.metadata.sourcePlaneName == name) {
                wire.wire = ReframeWire(wire.wire, oldPlane, plane);
            }
        }
        candidate.RebuildDependentGeometry();
        *this = std::move(candidate);
        return;
    }
    throw std::invalid_argument("Work plane name does not exist: " + std::string(name));
}

void Project::UpdateWire(std::string_view name, Wire wire)
{
    Project candidate = *this;
    for (NamedWire& namedWire : candidate.wires_) {
        if (namedWire.name == name) {
            if (namedWire.projection.has_value()) {
                throw std::invalid_argument("Projected wire must be edited through its source drawing.");
            }
            namedWire.wire = std::move(wire);
            candidate.RebuildDependentGeometry();
            *this = std::move(candidate);
            return;
        }
    }
    throw std::invalid_argument("Wire name does not exist: " + std::string(name));
}

void Project::UpdatePlate(
    std::string_view name,
    std::string sourceSurfaceName,
    double thickness,
    PlateThicknessDirection direction,
    std::string material)
{
    if (material.empty()) {
        throw std::invalid_argument("Plate material must not be empty.");
    }
    const std::optional<Surface> sourceSurface = FindSurface(sourceSurfaceName);
    if (!sourceSurface.has_value()) {
        throw std::invalid_argument("Plate source surface does not exist: " + sourceSurfaceName);
    }
    Plate replacement(*sourceSurface, thickness, direction);
    for (NamedPlate& plate : plates_) {
        if (plate.name == name) {
            for (const std::string& openingName : plate.openingWireNames) {
                const NamedWire& opening = RequireWire(openingName);
                if (!opening.projection.has_value()
                    || opening.projection->targetSurfaceName != sourceSurfaceName) {
                    throw std::invalid_argument("Remove plate openings before changing the source surface.");
                }
            }
            plate.plate = std::move(replacement);
            plate.sourceSurfaceName = std::move(sourceSurfaceName);
            plate.material = std::move(material);
            return;
        }
    }
    throw std::invalid_argument("Plate name does not exist: " + std::string(name));
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

void Project::SetWorkPlaneVisible(std::string_view name, bool visible)
{
    for (NamedWorkPlane& plane : workPlanes_) {
        if (plane.name == name) {
            plane.visible = visible;
            return;
        }
    }
    throw std::invalid_argument("Work plane name does not exist: " + std::string(name));
}

void Project::SetWireVisible(std::string_view name, bool visible)
{
    for (NamedWire& wire : wires_) {
        if (wire.name == name) {
            wire.visible = visible;
            return;
        }
    }
    throw std::invalid_argument("Wire name does not exist: " + std::string(name));
}

void Project::SetSurfaceVisible(std::string_view name, bool visible)
{
    for (NamedSurface& surface : surfaces_) {
        if (surface.name == name) {
            surface.visible = visible;
            return;
        }
    }
    throw std::invalid_argument("Surface name does not exist: " + std::string(name));
}

void Project::SetPlateVisible(std::string_view name, bool visible)
{
    for (NamedPlate& plate : plates_) {
        if (plate.name == name) {
            plate.visible = visible;
            return;
        }
    }
    throw std::invalid_argument("Plate name does not exist: " + std::string(name));
}

void Project::AddPlateOpening(std::string_view plateName, std::string wireName)
{
    NamedPlate* plate = nullptr;
    for (NamedPlate& candidate : plates_) {
        if (candidate.name == plateName) {
            plate = &candidate;
            break;
        }
    }
    if (plate == nullptr) {
        throw std::invalid_argument("Plate name does not exist: " + std::string(plateName));
    }
    const NamedWire& wire = RequireWire(wireName);
    if (!wire.wire.IsClosed()) {
        throw std::invalid_argument("Plate opening wire must be closed: " + wireName);
    }
    if (!wire.projection.has_value() || wire.projection->targetSurfaceName != plate->sourceSurfaceName) {
        throw std::invalid_argument("Plate opening must be a wire projected to the plate source surface: " + wireName);
    }
    if (std::find(plate->openingWireNames.begin(), plate->openingWireNames.end(), wireName)
        != plate->openingWireNames.end()) {
        throw std::invalid_argument("Wire is already a plate opening: " + wireName);
    }
    plate->openingWireNames.push_back(std::move(wireName));
}

void Project::RemovePlateOpening(std::string_view plateName, std::string_view wireName)
{
    for (NamedPlate& plate : plates_) {
        if (plate.name != plateName) {
            continue;
        }
        const auto position = std::find(plate.openingWireNames.begin(), plate.openingWireNames.end(), wireName);
        if (position == plate.openingWireNames.end()) {
            throw std::invalid_argument("Wire is not an opening of this plate: " + std::string(wireName));
        }
        plate.openingWireNames.erase(position);
        return;
    }
    throw std::invalid_argument("Plate name does not exist: " + std::string(plateName));
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

    for (const NamedSurface& surface : surfaces_) {
        if (std::find(surface.sourceWireNames.begin(), surface.sourceWireNames.end(), name)
            != surface.sourceWireNames.end()) {
            throw std::invalid_argument("Wire is used by surface: " + surface.name);
        }
    }
    for (const NamedWire& wire : wires_) {
        if (wire.projection.has_value() && wire.projection->sourceWireName == name) {
            throw std::invalid_argument("Wire is used as a projection drawing: " + wire.name);
        }
    }
    for (const NamedPlate& plate : plates_) {
        if (std::find(plate.openingWireNames.begin(), plate.openingWireNames.end(), name)
            != plate.openingWireNames.end()) {
            throw std::invalid_argument("Wire is used as a plate opening: " + plate.name);
        }
    }

    wires_.erase(position);
    return true;
}

bool Project::RemoveSurface(std::string_view name)
{
    const auto position = std::find_if(surfaces_.begin(), surfaces_.end(), [&](const NamedSurface& surface) {
        return surface.name == name;
    });
    if (position == surfaces_.end()) {
        return false;
    }
    for (const NamedWire& wire : wires_) {
        if (wire.projection.has_value() && wire.projection->targetSurfaceName == name) {
            throw std::invalid_argument("Surface is used by projected wire: " + wire.name);
        }
    }
    for (const NamedPlate& plate : plates_) {
        if (plate.sourceSurfaceName == name) {
            throw std::invalid_argument("Surface is used by plate: " + plate.name);
        }
    }
    surfaces_.erase(position);
    return true;
}

bool Project::RemovePlate(std::string_view name)
{
    const auto position = std::find_if(plates_.begin(), plates_.end(), [&](const NamedPlate& plate) {
        return plate.name == name;
    });
    if (position == plates_.end()) {
        return false;
    }
    plates_.erase(position);
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

std::optional<Surface> Project::FindSurface(std::string_view name) const
{
    for (const NamedSurface& surface : surfaces_) {
        if (surface.name == name) {
            return surface.surface;
        }
    }
    return std::nullopt;
}

std::optional<Plate> Project::FindPlate(std::string_view name) const
{
    for (const NamedPlate& plate : plates_) {
        if (plate.name == name) {
            return plate.plate;
        }
    }
    return std::nullopt;
}

const NamedWire& Project::RequireWire(std::string_view name) const
{
    const auto wire = std::find_if(wires_.begin(), wires_.end(), [&](const NamedWire& candidate) {
        return candidate.name == name;
    });
    if (wire == wires_.end()) {
        throw std::invalid_argument("Wire name does not exist: " + std::string(name));
    }
    return *wire;
}

void Project::RebuildDependentGeometry()
{
    for (NamedSurface& surface : surfaces_) {
        if (surface.surface.Kind() == SurfaceKind::Planar) {
            surface.surface = Surface::Planar(RequireWire(surface.sourceWireNames.at(0)).wire);
        } else if (surface.surface.Kind() == SurfaceKind::Ruled) {
            surface.surface = Surface::Ruled(
                RequireWire(surface.sourceWireNames.at(0)).wire,
                RequireWire(surface.sourceWireNames.at(1)).wire);
        } else {
            std::vector<Wire> sections;
            sections.reserve(surface.sourceWireNames.size());
            for (const std::string& sourceName : surface.sourceWireNames) {
                sections.push_back(RequireWire(sourceName).wire);
            }
            surface.surface = Surface::Loft(std::move(sections));
        }
    }
    for (NamedWire& wire : wires_) {
        if (!wire.projection.has_value()) {
            continue;
        }
        const NamedWire& source = RequireWire(wire.projection->sourceWireName);
        const std::optional<Surface> surface = FindSurface(wire.projection->targetSurfaceName);
        if (!surface.has_value()) {
            throw std::logic_error("Projected wire target surface is missing.");
        }
        wire.wire = surface->ProjectWireAlongDirection(source.wire, wire.projection->direction);
    }
    for (NamedPlate& plate : plates_) {
        const std::optional<Surface> sourceSurface = FindSurface(plate.sourceSurfaceName);
        if (!sourceSurface.has_value()) {
            throw std::logic_error("Plate source surface is missing.");
        }
        plate.plate = Plate(*sourceSurface, plate.plate.Thickness(), plate.plate.Direction());
    }
}

} // namespace kachakacha::model
