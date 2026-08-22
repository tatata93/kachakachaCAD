#include "kachakacha/model/Project.h"

#include "kachakacha/model/Measurement.h"
#include "kachakacha/model/WireOperations.h"

#include <algorithm>
#include <cmath>
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

std::optional<WorkPlane> ConstraintPlane(const Project& project, const WireMetadata& metadata)
{
    if (!metadata.sourcePlaneName.has_value()) {
        return std::nullopt;
    }
    return project.FindWorkPlane(*metadata.sourcePlaneName);
}

Wire ConstrainWire(const Project& project, const Wire& wire, const WireMetadata& metadata)
{
    return ApplyWireCurveConstraints(
        ApplyWireLineConstraints(wire, ConstraintPlane(project, metadata), metadata.lineConstraints),
        metadata.curveConstraints);
}

geometry::Vector3 EndpointPoint(const Wire& wire, WireEndpoint endpoint)
{
    return endpoint == WireEndpoint::Start ? wire.Start() : wire.End();
}

bool SameEndpointReference(
    const WireEndpointReference& first,
    const WireEndpointReference& second)
{
    return first.wireName == second.wireName && first.endpoint == second.endpoint;
}

geometry::Vector3 EndpointInteriorDirection(const Wire& wire, WireEndpoint endpoint)
{
    const geometry::Vector3 parameterTangent = MeasureWireTangent(
        wire, endpoint == WireEndpoint::Start ? 0.0 : 1.0);
    return endpoint == WireEndpoint::Start ? parameterTangent : parameterTangent * -1.0;
}

Wire AlignBezierEndpointTangent(
    const Wire& wire,
    WireEndpoint endpoint,
    geometry::Vector3 interiorDirection)
{
    if (wire.Kind() != WireKind::CubicBezier) {
        throw std::invalid_argument("Tangent followers must be cubic Bezier wires.");
    }
    std::vector<geometry::Vector3> points = wire.ControlPoints();
    const std::size_t endpointIndex = endpoint == WireEndpoint::Start ? 0 : 3;
    const std::size_t handleIndex = endpoint == WireEndpoint::Start ? 1 : 2;
    const double handleLength = (points[handleIndex] - points[endpointIndex]).Length();
    if (handleLength <= 1.0e-9) {
        throw std::invalid_argument("Tangent follower handle must have a non-zero length.");
    }
    points[handleIndex] = points[endpointIndex] + interiorDirection.Normalized() * handleLength;
    return Wire::CubicBezier(points[0], points[1], points[2], points[3]);
}

Wire AlignArcEndpointTangent(
    const Wire& wire,
    WireEndpoint endpoint,
    geometry::Vector3 interiorDirection,
    std::optional<geometry::Vector3> requiredPlaneNormal)
{
    if (wire.Kind() != WireKind::CircularArc) {
        throw std::invalid_argument("Arc tangent followers must be open circular arcs.");
    }
    const WireArcData arc = wire.ArcData();
    const geometry::Vector3 endpointPoint = EndpointPoint(wire, endpoint);
    const geometry::Vector3 parameterTangent =
        (endpoint == WireEndpoint::Start ? interiorDirection : -interiorDirection).Normalized();
    const geometry::Vector3 oldNormal = geometry::Cross(arc.uAxis, arc.vAxis).Normalized();

    geometry::Vector3 normal;
    if (requiredPlaneNormal.has_value()) {
        normal = requiredPlaneNormal->Normalized();
    } else {
        normal = oldNormal - parameterTangent * geometry::Dot(oldNormal, parameterTangent);
        if (normal.LengthSquared() <= 1.0e-18) {
            const geometry::Vector3 oldRadial = (endpointPoint - arc.center).Normalized();
            normal = oldRadial - parameterTangent * geometry::Dot(oldRadial, parameterTangent);
        }
        normal = normal.Normalized();
    }
    if (geometry::Dot(normal, oldNormal) < 0.0) {
        normal = -normal;
    }

    const double sweepOrientation = arc.sweepAngleRadians >= 0.0 ? 1.0 : -1.0;
    const geometry::Vector3 radial =
        geometry::Cross(parameterTangent, normal).Normalized() * sweepOrientation;
    const geometry::Vector3 vAxis = geometry::Cross(normal, radial).Normalized();
    const double startAngle = endpoint == WireEndpoint::Start ? 0.0 : -arc.sweepAngleRadians;
    return Wire::CircularArc(
        endpointPoint - radial * arc.radius,
        radial,
        vAxis,
        arc.radius,
        startAngle,
        arc.sweepAngleRadians);
}

Wire AlignWireEndpointTangent(
    const Wire& wire,
    WireEndpoint endpoint,
    geometry::Vector3 interiorDirection,
    std::optional<geometry::Vector3> requiredPlaneNormal)
{
    if (wire.Kind() == WireKind::CubicBezier) {
        return AlignBezierEndpointTangent(wire, endpoint, interiorDirection);
    }
    if (wire.Kind() == WireKind::CircularArc) {
        return AlignArcEndpointTangent(
            wire, endpoint, interiorDirection, std::move(requiredPlaneNormal));
    }
    throw std::invalid_argument("Tangent followers must be cubic Bezier wires or circular arcs.");
}

bool TangentAlignmentMatches(const Wire& first, const Wire& second, WireEndpoint endpoint)
{
    if (first.Kind() != second.Kind()) {
        return false;
    }
    if (first.Kind() == WireKind::CubicBezier) {
        const std::size_t handleIndex = endpoint == WireEndpoint::Start ? 1 : 2;
        return geometry::AlmostEqual(
            first.ControlPoints()[handleIndex], second.ControlPoints()[handleIndex], 1.0e-9);
    }
    const WireArcData firstArc = first.ArcData();
    const WireArcData secondArc = second.ArcData();
    return geometry::AlmostEqual(firstArc.center, secondArc.center, 1.0e-9)
        && geometry::AlmostEqual(firstArc.uAxis, secondArc.uAxis, 1.0e-9)
        && geometry::AlmostEqual(firstArc.vAxis, secondArc.vAxis, 1.0e-9)
        && geometry::AlmostEqual(firstArc.radius, secondArc.radius, 1.0e-9)
        && geometry::AlmostEqual(
            firstArc.startAngleRadians, secondArc.startAngleRadians, 1.0e-9)
        && geometry::AlmostEqual(
            firstArc.sweepAngleRadians, secondArc.sweepAngleRadians, 1.0e-9);
}

Wire ReplaceWireEndpoint(const Wire& wire, WireEndpoint endpoint, geometry::Vector3 point)
{
    if (!point.IsFinite()) {
        throw std::invalid_argument("Coincident endpoint must be finite.");
    }
    switch (wire.Kind()) {
    case WireKind::Line:
        return endpoint == WireEndpoint::Start
            ? Wire::Line(point, wire.End())
            : Wire::Line(wire.Start(), point);
    case WireKind::Polyline: {
        std::vector<geometry::Vector3> points = wire.ControlPoints();
        (endpoint == WireEndpoint::Start ? points.front() : points.back()) = point;
        return Wire::Polyline(std::move(points));
    }
    case WireKind::CubicBezier: {
        std::vector<geometry::Vector3> points = wire.ControlPoints();
        if (endpoint == WireEndpoint::Start) {
            const geometry::Vector3 delta = point - points[0];
            points[0] = point;
            points[1] = points[1] + delta;
        } else {
            const geometry::Vector3 delta = point - points[3];
            points[3] = point;
            points[2] = points[2] + delta;
        }
        return Wire::CubicBezier(points[0], points[1], points[2], points[3]);
    }
    case WireKind::CircularArc:
        return wire.Translated(point - EndpointPoint(wire, endpoint));
    case WireKind::Circle:
        throw std::invalid_argument("Closed circles do not have editable endpoints.");
    }
    throw std::logic_error("Unknown wire kind.");
}

void RequireConstructionWireHasNoModelDependencies(const Project& project, std::string_view wireName)
{
    for (const NamedSurface& surface : project.Surfaces()) {
        if (std::find(surface.sourceWireNames.begin(), surface.sourceWireNames.end(), wireName)
            != surface.sourceWireNames.end()) {
            throw std::invalid_argument("A surface source wire cannot be changed to construction: "
                + std::string(wireName));
        }
    }
    for (const NamedWire& wire : project.Wires()) {
        if (wire.projection.has_value() && wire.projection->sourceWireName == wireName) {
            throw std::invalid_argument("A projection source wire cannot be changed to construction: "
                + std::string(wireName));
        }
    }
    for (const NamedPlate& plate : project.Plates()) {
        if (std::find(plate.openingWireNames.begin(), plate.openingWireNames.end(), wireName)
            != plate.openingWireNames.end()) {
            throw std::invalid_argument("A plate opening wire cannot be changed to construction: "
                + std::string(wireName));
        }
    }
}

bool OpeningLiesWithinRange(
    const Surface& surface,
    const NamedWire& opening,
    const PlateSurfaceRange& range)
{
    if (!opening.projection.has_value()) {
        return false;
    }
    constexpr double tolerance = 1.0e-5;
    for (int sample = 0; sample < 24; ++sample) {
        const geometry::Vector3 point = opening.wire.Evaluate(static_cast<double>(sample) / 24.0);
        const SurfaceProjection projection = surface.ProjectPointAlongDirection(
            point,
            opening.projection->direction);
        if (projection.u < range.minimumU - tolerance || projection.u > range.maximumU + tolerance
            || projection.v < range.minimumV - tolerance || projection.v > range.maximumV + tolerance) {
            return false;
        }
    }
    return true;
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

    wire = ConstrainWire(*this, wire, metadata);
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
    if (boundary.metadata.construction) {
        throw std::invalid_argument("Construction wire cannot be used as a planar surface source.");
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
    if (first.metadata.construction || second.metadata.construction) {
        throw std::invalid_argument("Construction wire cannot be used as a ruled surface section.");
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
        if (section.metadata.construction) {
            throw std::invalid_argument("Construction wire cannot be used as a loft section.");
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
    if (source.metadata.construction) {
        throw std::invalid_argument("Construction wire cannot be used as a projection source.");
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
            if ((wire.metadata.planePolicy == WirePlanePolicy::LockedToPlane
                    || wire.metadata.lineConstraints.angleDegrees.has_value())
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
            namedWire.wire = ConstrainWire(candidate, wire, namedWire.metadata);
            candidate.RebuildDependentGeometry();
            *this = std::move(candidate);
            return;
        }
    }
    throw std::invalid_argument("Wire name does not exist: " + std::string(name));
}

void Project::UpdateWireAndMetadata(std::string_view name, Wire wire, WireMetadata metadata)
{
    Project candidate = *this;
    if (metadata.sourcePlaneName.has_value()
        && !candidate.FindWorkPlane(*metadata.sourcePlaneName).has_value()) {
        throw std::invalid_argument("Wire source plane does not exist: " + *metadata.sourcePlaneName);
    }

    for (NamedWire& namedWire : candidate.wires_) {
        if (namedWire.name != name) {
            continue;
        }
        if (namedWire.projection.has_value()) {
            throw std::invalid_argument("Projected wire must be edited through its source drawing.");
        }
        if (metadata.construction) {
            RequireConstructionWireHasNoModelDependencies(candidate, name);
        }
        if (!metadata.lineConstraints.Empty()
            && std::any_of(
                candidate.coincidentConstraints_.begin(), candidate.coincidentConstraints_.end(),
                [&](const WireCoincidentConstraint& constraint) {
                    return constraint.follower.wireName == name;
                })) {
            throw std::invalid_argument("Remove endpoint coincidence before adding follower line dimensions.");
        }
        namedWire.wire = ConstrainWire(candidate, wire, metadata);
        namedWire.metadata = std::move(metadata);
        candidate.RebuildDependentGeometry();
        *this = std::move(candidate);
        return;
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
    for (NamedPlate& plate : plates_) {
        if (plate.name == name) {
            for (const std::string& openingName : plate.openingWireNames) {
                const NamedWire& opening = RequireWire(openingName);
                if (!opening.projection.has_value()
                    || opening.projection->targetSurfaceName != sourceSurfaceName) {
                    throw std::invalid_argument("Remove plate openings before changing the source surface.");
                }
                if (!OpeningLiesWithinRange(*sourceSurface, opening, plate.plate.Range())) {
                    throw std::invalid_argument("Updated plate surface does not contain opening: " + openingName);
                }
            }
            plate.plate = Plate(*sourceSurface, thickness, direction, plate.plate.Range());
            plate.sourceSurfaceName = std::move(sourceSurfaceName);
            plate.material = std::move(material);
            return;
        }
    }
    throw std::invalid_argument("Plate name does not exist: " + std::string(name));
}

void Project::SetWireMetadata(std::string_view name, WireMetadata metadata)
{
    Project candidate = *this;
    if (metadata.sourcePlaneName.has_value() && !candidate.FindWorkPlane(*metadata.sourcePlaneName).has_value()) {
        throw std::invalid_argument("Wire source plane does not exist: " + *metadata.sourcePlaneName);
    }

    for (NamedWire& wire : candidate.wires_) {
        if (wire.name == name) {
            if (metadata.construction) {
                RequireConstructionWireHasNoModelDependencies(candidate, name);
            }
            if (!metadata.lineConstraints.Empty()
                && std::any_of(
                    candidate.coincidentConstraints_.begin(), candidate.coincidentConstraints_.end(),
                    [&](const WireCoincidentConstraint& constraint) {
                        return constraint.follower.wireName == name;
                    })) {
                throw std::invalid_argument("Remove endpoint coincidence before adding follower line dimensions.");
            }
            wire.wire = ConstrainWire(candidate, wire.wire, metadata);
            wire.metadata = std::move(metadata);
            candidate.RebuildDependentGeometry();
            *this = std::move(candidate);
            return;
        }
    }

    throw std::invalid_argument("Wire name does not exist: " + std::string(name));
}

void Project::AddWireCoincidentConstraint(
    WireEndpointReference anchor,
    WireEndpointReference follower)
{
    if (anchor.wireName == follower.wireName) {
        throw std::invalid_argument("Coincident endpoints must belong to different wires.");
    }
    const NamedWire& anchorWire = RequireWire(anchor.wireName);
    const NamedWire& followerWire = RequireWire(follower.wireName);
    if (anchorWire.projection.has_value() || followerWire.projection.has_value()) {
        throw std::invalid_argument("Projected wires cannot own endpoint coincidence constraints.");
    }
    if (anchorWire.wire.IsClosed() || followerWire.wire.IsClosed()) {
        throw std::invalid_argument("Coincident endpoint constraints require open wires.");
    }
    if (!followerWire.metadata.lineConstraints.Empty()) {
        throw std::invalid_argument("Remove the follower line dimensions before adding endpoint coincidence.");
    }
    for (const WireCoincidentConstraint& existing : coincidentConstraints_) {
        if (SameEndpointReference(existing.follower, follower)) {
            throw std::invalid_argument("The follower endpoint already has a coincidence constraint.");
        }
    }

    std::vector<std::string> reachable{follower.wireName};
    for (std::size_t cursor = 0; cursor < reachable.size(); ++cursor) {
        if (reachable[cursor] == anchor.wireName) {
            throw std::invalid_argument("Endpoint coincidence constraints cannot form a cycle.");
        }
        for (const WireCoincidentConstraint& existing : coincidentConstraints_) {
            if (existing.anchor.wireName == reachable[cursor]
                && std::find(reachable.begin(), reachable.end(), existing.follower.wireName) == reachable.end()) {
                reachable.push_back(existing.follower.wireName);
            }
        }
    }

    Project candidate = *this;
    candidate.coincidentConstraints_.push_back({std::move(anchor), std::move(follower)});
    candidate.RebuildDependentGeometry();
    *this = std::move(candidate);
}

std::size_t Project::RemoveWireCoincidentConstraints(std::string_view wireName)
{
    const std::size_t before = coincidentConstraints_.size();
    std::erase_if(coincidentConstraints_, [&](const WireCoincidentConstraint& constraint) {
        return constraint.anchor.wireName == wireName || constraint.follower.wireName == wireName;
    });
    std::erase_if(tangentConstraints_, [&](const WireTangentConstraint& constraint) {
        return constraint.anchor.wireName == wireName || constraint.follower.wireName == wireName;
    });
    return before - coincidentConstraints_.size();
}

void Project::AddWireTangentConstraint(
    WireEndpointReference anchor,
    WireEndpointReference follower)
{
    const NamedWire& anchorWire = RequireWire(anchor.wireName);
    const NamedWire& followerWire = RequireWire(follower.wireName);
    if (anchor.wireName == follower.wireName) {
        throw std::invalid_argument("Tangent endpoints must belong to different wires.");
    }
    if (anchorWire.projection.has_value() || followerWire.projection.has_value()
        || anchorWire.wire.IsClosed() || followerWire.wire.IsClosed()) {
        throw std::invalid_argument("Tangent endpoint constraints require open, editable wires.");
    }
    if (followerWire.wire.Kind() != WireKind::CubicBezier
        && followerWire.wire.Kind() != WireKind::CircularArc) {
        throw std::invalid_argument("The tangent follower must be a cubic Bezier wire or circular arc.");
    }
    const bool hasCoincidence = std::any_of(
        coincidentConstraints_.begin(), coincidentConstraints_.end(),
        [&](const WireCoincidentConstraint& constraint) {
            return SameEndpointReference(constraint.anchor, anchor)
                && SameEndpointReference(constraint.follower, follower);
        });
    if (!hasCoincidence) {
        throw std::invalid_argument("Tangent endpoints must have a matching coincidence constraint.");
    }
    for (const WireTangentConstraint& existing : tangentConstraints_) {
        if (SameEndpointReference(existing.follower, follower)) {
            throw std::invalid_argument("The follower endpoint already has a tangent constraint.");
        }
    }

    Project candidate = *this;
    candidate.tangentConstraints_.push_back({std::move(anchor), std::move(follower)});
    candidate.RebuildDependentGeometry();
    *this = std::move(candidate);
}

std::size_t Project::RemoveWireTangentConstraints(std::string_view wireName)
{
    const std::size_t before = tangentConstraints_.size();
    std::erase_if(tangentConstraints_, [&](const WireTangentConstraint& constraint) {
        return constraint.anchor.wireName == wireName || constraint.follower.wireName == wireName;
    });
    return before - tangentConstraints_.size();
}

void Project::AddReferenceDimension(ReferenceDimension dimension)
{
    if (dimension.name.empty()) {
        throw std::invalid_argument("Reference dimension name must not be empty.");
    }
    if (std::any_of(referenceDimensions_.begin(), referenceDimensions_.end(),
            [&](const ReferenceDimension& existing) { return existing.name == dimension.name; })) {
        throw std::invalid_argument("Reference dimension name already exists: " + dimension.name);
    }

    Project candidate = *this;
    const std::string name = dimension.name;
    candidate.referenceDimensions_.push_back(std::move(dimension));
    (void)candidate.EvaluateReferenceDimension(name);
    *this = std::move(candidate);
}

bool Project::RemoveReferenceDimension(std::string_view name)
{
    const auto position = std::find_if(
        referenceDimensions_.begin(), referenceDimensions_.end(),
        [&](const ReferenceDimension& dimension) { return dimension.name == name; });
    if (position == referenceDimensions_.end()) {
        return false;
    }
    referenceDimensions_.erase(position);
    return true;
}

void Project::SetReferenceDimensionVisible(std::string_view name, bool visible)
{
    for (ReferenceDimension& dimension : referenceDimensions_) {
        if (dimension.name == name) {
            dimension.visible = visible;
            return;
        }
    }
    throw std::invalid_argument("Reference dimension name does not exist: " + std::string(name));
}

ReferenceDimensionResult Project::EvaluateReferenceDimension(std::string_view name) const
{
    const auto position = std::find_if(
        referenceDimensions_.begin(), referenceDimensions_.end(),
        [&](const ReferenceDimension& dimension) { return dimension.name == name; });
    if (position == referenceDimensions_.end()) {
        throw std::invalid_argument("Reference dimension name does not exist: " + std::string(name));
    }
    const ReferenceDimension& dimension = *position;
    const auto wire = [&](const DimensionReference& reference) -> const Wire& {
        if (reference.kind != DimensionReferenceKind::Wire
            || !std::isfinite(reference.wireParameter)
            || reference.wireParameter < 0.0 || reference.wireParameter > 1.0) {
            throw std::invalid_argument("Reference dimension requires a valid wire reference.");
        }
        return RequireWire(reference.objectName).wire;
    };
    const auto plane = [&](const DimensionReference& reference) -> WorkPlane {
        if (reference.kind != DimensionReferenceKind::WorkPlane) {
            throw std::invalid_argument("Reference dimension requires a work plane reference.");
        }
        const std::optional<WorkPlane> found = FindWorkPlane(reference.objectName);
        if (!found.has_value()) {
            throw std::invalid_argument("Reference dimension work plane does not exist: "
                + reference.objectName);
        }
        return *found;
    };
    const auto point = [&](const DimensionReference& reference) -> geometry::Vector3 {
        if (reference.kind == DimensionReferenceKind::FixedPoint) {
            if (!reference.point.IsFinite()) {
                throw std::invalid_argument("Reference dimension point must be finite.");
            }
            return reference.point;
        }
        if (reference.kind == DimensionReferenceKind::Wire) {
            return wire(reference).Evaluate(reference.wireParameter);
        }
        throw std::invalid_argument("Reference dimension requires a point or wire position.");
    };

    switch (dimension.kind) {
    case ReferenceDimensionKind::PointDistance: {
        const geometry::Vector3 first = point(dimension.first);
        const geometry::Vector3 second = point(dimension.second);
        return {first, second, (second - first).Length()};
    }
    case ReferenceDimensionKind::WireLength: {
        const Wire& measured = wire(dimension.first);
        return {measured.Start(), measured.End(), MeasureWireLength(measured)};
    }
    case ReferenceDimensionKind::WireRadius: {
        const Wire& measured = wire(dimension.first);
        const std::optional<double> radius = MeasureWireRadius(measured);
        if (!radius.has_value()) {
            throw std::invalid_argument("Radius dimensions require a circle or circular arc.");
        }
        return {measured.ArcData().center,
            measured.Evaluate(dimension.first.wireParameter), *radius};
    }
    case ReferenceDimensionKind::WireDistance: {
        const DistanceMeasurement measured = MeasureWireToWireDistance(
            wire(dimension.first), wire(dimension.second));
        return {measured.firstPoint, measured.secondPoint, measured.distanceMillimeters};
    }
    case ReferenceDimensionKind::WireAngle: {
        const Wire& firstWire = wire(dimension.first);
        const Wire& secondWire = wire(dimension.second);
        return {
            firstWire.Evaluate(dimension.first.wireParameter),
            secondWire.Evaluate(dimension.second.wireParameter),
            MeasureDirectionsAngle(
                MeasureWireTangent(firstWire, dimension.first.wireParameter),
                MeasureWireTangent(secondWire, dimension.second.wireParameter)).directedDegrees,
        };
    }
    case ReferenceDimensionKind::PointWireDistance: {
        const DistanceMeasurement measured = MeasurePointToWireDistance(
            point(dimension.first), wire(dimension.second));
        return {measured.firstPoint, measured.secondPoint, measured.distanceMillimeters};
    }
    case ReferenceDimensionKind::PointPlaneDistance: {
        const geometry::Vector3 measuredPoint = point(dimension.first);
        const WorkPlane measuredPlane = plane(dimension.second);
        const double signedDistance = MeasureSignedPointToPlaneDistance(measuredPoint, measuredPlane);
        return {measuredPoint, measuredPoint - measuredPlane.Normal() * signedDistance,
            std::abs(signedDistance)};
    }
    case ReferenceDimensionKind::WirePlaneAngle: {
        const Wire& measuredWire = wire(dimension.first);
        const WorkPlane measuredPlane = plane(dimension.second);
        const geometry::Vector3 measuredPoint = measuredWire.Evaluate(dimension.first.wireParameter);
        const double signedDistance = MeasureSignedPointToPlaneDistance(measuredPoint, measuredPlane);
        return {measuredPoint, measuredPoint - measuredPlane.Normal() * signedDistance,
            MeasureDirectionToPlaneAngleDegrees(
                MeasureWireTangent(measuredWire, dimension.first.wireParameter), measuredPlane)};
    }
    case ReferenceDimensionKind::PlaneAngle: {
        const WorkPlane firstPlane = plane(dimension.first);
        const WorkPlane secondPlane = plane(dimension.second);
        return {firstPlane.Origin(), secondPlane.Origin(),
            MeasurePlaneToPlaneAngleDegrees(firstPlane, secondPlane)};
    }
    case ReferenceDimensionKind::PlaneDistance: {
        const WorkPlane firstPlane = plane(dimension.first);
        const WorkPlane secondPlane = plane(dimension.second);
        if (MeasurePlaneToPlaneAngleDegrees(firstPlane, secondPlane) > 1.0e-7) {
            throw std::invalid_argument("Plane distance dimensions require parallel work planes.");
        }
        const double signedDistance = MeasureSignedPointToPlaneDistance(
            firstPlane.Origin(), secondPlane);
        return {firstPlane.Origin(), firstPlane.Origin() - secondPlane.Normal() * signedDistance,
            std::abs(signedDistance)};
    }
    }
    throw std::logic_error("Unknown reference dimension kind.");
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

void Project::SetPlateRange(std::string_view name, PlateSurfaceRange range)
{
    for (NamedPlate& plate : plates_) {
        if (plate.name != name) {
            continue;
        }
        if (plate.plate.SourceSurface().Kind() == SurfaceKind::Planar && !range.IsFull()) {
            throw std::invalid_argument("Planar plate ranges are not supported.");
        }
        for (const std::string& openingName : plate.openingWireNames) {
            if (!OpeningLiesWithinRange(plate.plate.SourceSurface(), RequireWire(openingName), range)) {
                throw std::invalid_argument("Plate range does not contain opening: " + openingName);
            }
        }
        plate.plate = Plate(
            plate.plate.SourceSurface(),
            plate.plate.Thickness(),
            plate.plate.Direction(),
            range);
        return;
    }
    throw std::invalid_argument("Plate name does not exist: " + std::string(name));
}

void Project::SplitPlate(
    std::string_view name,
    PlateSplitAxis axis,
    double parameter,
    std::string firstName,
    std::string secondName)
{
    if (firstName.empty() || secondName.empty() || firstName == secondName) {
        throw std::invalid_argument("Split plate names must be different and non-empty.");
    }
    if (FindPlate(firstName).has_value() || FindPlate(secondName).has_value()) {
        throw std::invalid_argument("Split plate name already exists.");
    }
    const auto position = std::find_if(plates_.begin(), plates_.end(), [&](const NamedPlate& plate) {
        return plate.name == name;
    });
    if (position == plates_.end()) {
        throw std::invalid_argument("Plate name does not exist: " + std::string(name));
    }
    if (position->plate.SourceSurface().Kind() == SurfaceKind::Planar) {
        throw std::invalid_argument("Planar plates can already be cut directly and do not need surface splitting.");
    }

    const auto [firstPlate, secondPlate] = position->plate.Split(axis, parameter);
    const double splitCoordinate = axis == PlateSplitAxis::U
        ? firstPlate.Range().maximumU
        : firstPlate.Range().maximumV;
    std::vector<std::string> firstOpenings;
    std::vector<std::string> secondOpenings;
    for (const std::string& openingName : position->openingWireNames) {
        const NamedWire& opening = RequireWire(openingName);
        if (!opening.projection.has_value()) {
            throw std::logic_error("Plate opening projection relation is missing.");
        }
        bool beforeSplit = false;
        bool afterSplit = false;
        for (int sample = 0; sample < 24; ++sample) {
            const geometry::Vector3 point = opening.wire.Evaluate(static_cast<double>(sample) / 24.0);
            const SurfaceProjection projection = position->plate.SourceSurface().ProjectPointAlongDirection(
                point,
                opening.projection->direction);
            const double coordinate = axis == PlateSplitAxis::U ? projection.u : projection.v;
            beforeSplit = beforeSplit || coordinate < splitCoordinate - 1.0e-5;
            afterSplit = afterSplit || coordinate > splitCoordinate + 1.0e-5;
        }
        if (beforeSplit && afterSplit) {
            throw std::invalid_argument("Plate split line crosses opening: " + openingName);
        }
        if (!beforeSplit && !afterSplit) {
            throw std::invalid_argument("Plate split line coincides with opening: " + openingName);
        }
        (beforeSplit ? firstOpenings : secondOpenings).push_back(openingName);
    }

    const std::string sourceSurfaceName = position->sourceSurfaceName;
    const std::string material = position->material;
    const bool visible = position->visible;
    const std::size_t insertionIndex = static_cast<std::size_t>(std::distance(plates_.begin(), position));
    plates_.erase(position);
    NamedPlate firstNamedPlate{
        std::move(firstName),
        firstPlate,
        sourceSurfaceName,
        material,
        std::move(firstOpenings),
        visible,
    };
    NamedPlate secondNamedPlate{
        std::move(secondName),
        secondPlate,
        sourceSurfaceName,
        material,
        std::move(secondOpenings),
        visible,
    };
    plates_.insert(
        plates_.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
        std::move(firstNamedPlate));
    plates_.insert(
        plates_.begin() + static_cast<std::ptrdiff_t>(insertionIndex + 1),
        std::move(secondNamedPlate));
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
    if (wire.metadata.construction) {
        throw std::invalid_argument("Construction wire cannot be used as a plate opening: " + wireName);
    }
    if (!wire.wire.IsClosed()) {
        throw std::invalid_argument("Plate opening wire must be closed: " + wireName);
    }
    if (!wire.projection.has_value() || wire.projection->targetSurfaceName != plate->sourceSurfaceName) {
        throw std::invalid_argument("Plate opening must be a wire projected to the plate source surface: " + wireName);
    }
    if (!OpeningLiesWithinRange(plate->plate.SourceSurface(), wire, plate->plate.Range())) {
        throw std::invalid_argument("Plate opening lies outside this plate piece: " + wireName);
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

    for (const NamedWire& wire : wires_) {
        if (wire.metadata.sourcePlaneName.has_value()
            && *wire.metadata.sourcePlaneName == name
            && wire.metadata.lineConstraints.angleDegrees.has_value()) {
            throw std::invalid_argument("Work plane is used by an angle-constrained wire: " + wire.name);
        }
    }

    workPlanes_.erase(position);
    std::erase_if(referenceDimensions_, [&](const ReferenceDimension& dimension) {
        return (dimension.first.kind == DimensionReferenceKind::WorkPlane
                   && dimension.first.objectName == name)
            || (dimension.second.kind == DimensionReferenceKind::WorkPlane
                && dimension.second.objectName == name);
    });
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
    for (const WireCoincidentConstraint& constraint : coincidentConstraints_) {
        if (constraint.anchor.wireName == name || constraint.follower.wireName == name) {
            throw std::invalid_argument("Wire is used by an endpoint coincidence constraint.");
        }
    }
    for (const WireTangentConstraint& constraint : tangentConstraints_) {
        if (constraint.anchor.wireName == name || constraint.follower.wireName == name) {
            throw std::invalid_argument("Wire is used by an endpoint tangent constraint.");
        }
    }

    wires_.erase(position);
    std::erase_if(referenceDimensions_, [&](const ReferenceDimension& dimension) {
        return (dimension.first.kind == DimensionReferenceKind::Wire
                   && dimension.first.objectName == name)
            || (dimension.second.kind == DimensionReferenceKind::Wire
                && dimension.second.objectName == name);
    });
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

void Project::ApplyCoincidentConstraints()
{
    for (std::size_t pass = 0; pass <= coincidentConstraints_.size(); ++pass) {
        bool changed = false;
        for (const WireCoincidentConstraint& constraint : coincidentConstraints_) {
            const auto anchor = std::find_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
                return wire.name == constraint.anchor.wireName;
            });
            const auto follower = std::find_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
                return wire.name == constraint.follower.wireName;
            });
            if (anchor == wires_.end() || follower == wires_.end()) {
                throw std::logic_error("Endpoint coincidence wire is missing.");
            }
            const geometry::Vector3 target = EndpointPoint(anchor->wire, constraint.anchor.endpoint);
            if (geometry::AlmostEqual(
                    EndpointPoint(follower->wire, constraint.follower.endpoint), target, 1.0e-9)) {
                continue;
            }
            if (follower->metadata.planePolicy == WirePlanePolicy::LockedToPlane
                && follower->metadata.sourcePlaneName.has_value()) {
                const std::optional<WorkPlane> plane = FindWorkPlane(*follower->metadata.sourcePlaneName);
                if (!plane.has_value() || std::abs(plane->Project(target).w) > 1.0e-7) {
                    throw std::invalid_argument("Coincident endpoint would leave its locked work plane.");
                }
            }
            follower->wire = ReplaceWireEndpoint(follower->wire, constraint.follower.endpoint, target);
            changed = true;
        }
        if (!changed) {
            return;
        }
    }
    throw std::logic_error("Endpoint coincidence constraints did not converge.");
}

void Project::ApplyTangentConstraints()
{
    for (std::size_t pass = 0; pass <= tangentConstraints_.size(); ++pass) {
        bool changed = false;
        for (const WireTangentConstraint& constraint : tangentConstraints_) {
            const auto anchor = std::find_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
                return wire.name == constraint.anchor.wireName;
            });
            const auto follower = std::find_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
                return wire.name == constraint.follower.wireName;
            });
            if (anchor == wires_.end() || follower == wires_.end()) {
                throw std::logic_error("Endpoint tangent wire is missing.");
            }

            const geometry::Vector3 desiredInterior =
                EndpointInteriorDirection(anchor->wire, constraint.anchor.endpoint) * -1.0;
            std::optional<geometry::Vector3> requiredPlaneNormal;
            if (follower->metadata.planePolicy == WirePlanePolicy::LockedToPlane
                && follower->metadata.sourcePlaneName.has_value()) {
                const std::optional<WorkPlane> plane = FindWorkPlane(*follower->metadata.sourcePlaneName);
                if (!plane.has_value()
                    || std::abs(geometry::Dot(desiredInterior, plane->Normal())) > 1.0e-7) {
                    throw std::invalid_argument("Tangent direction would leave the follower work plane.");
                }
                requiredPlaneNormal = plane->Normal();
            }

            const Wire aligned = AlignWireEndpointTangent(
                follower->wire,
                constraint.follower.endpoint,
                desiredInterior,
                requiredPlaneNormal);
            if (TangentAlignmentMatches(
                    follower->wire, aligned, constraint.follower.endpoint)) {
                continue;
            }
            follower->wire = aligned;
            changed = true;
        }
        if (!changed) {
            return;
        }
    }
    throw std::logic_error("Endpoint tangent constraints did not converge.");
}

void Project::RebuildDependentGeometry()
{
    ApplyCoincidentConstraints();
    ApplyTangentConstraints();
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
        plate.plate = Plate(
            *sourceSurface,
            plate.plate.Thickness(),
            plate.plate.Direction(),
            plate.plate.Range());
        for (const std::string& openingName : plate.openingWireNames) {
            if (!OpeningLiesWithinRange(*sourceSurface, RequireWire(openingName), plate.plate.Range())) {
                throw std::invalid_argument("Plate opening moved outside split piece: " + openingName);
            }
        }
    }
}

} // namespace kachakacha::model
