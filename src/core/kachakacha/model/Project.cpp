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
    case WireKind::CubicBSpline:
        return Wire::CubicBSplineWithKnots(std::move(points), wire.BSplineKnots());
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

geometry::Vector3 EndpointCurvatureVector(const Wire& wire, WireEndpoint endpoint)
{
    switch (wire.Kind()) {
    case WireKind::Line:
    case WireKind::Polyline:
        return {};
    case WireKind::CubicBezier: {
        const auto& points = wire.ControlPoints();
        const geometry::Vector3 endpointPoint = endpoint == WireEndpoint::Start ? points[0] : points[3];
        const geometry::Vector3 firstInterior = endpoint == WireEndpoint::Start ? points[1] : points[2];
        const geometry::Vector3 secondInterior = endpoint == WireEndpoint::Start ? points[2] : points[1];
        const geometry::Vector3 firstDerivative = (firstInterior - endpointPoint) * 3.0;
        const double speedSquared = firstDerivative.LengthSquared();
        if (speedSquared <= 1.0e-18) {
            throw std::invalid_argument("Curvature anchor handle must have a non-zero length.");
        }
        const geometry::Vector3 tangent = firstDerivative / std::sqrt(speedSquared);
        const geometry::Vector3 secondDerivative =
            (secondInterior - firstInterior * 2.0 + endpointPoint) * 6.0;
        return (secondDerivative
            - tangent * geometry::Dot(secondDerivative, tangent)) / speedSquared;
    }
    case WireKind::CubicBSpline: {
        constexpr double step = 1.0e-4;
        const double start = endpoint == WireEndpoint::Start ? 0.0 : 1.0;
        const double direction = endpoint == WireEndpoint::Start ? 1.0 : -1.0;
        const geometry::Vector3 p0 = wire.Evaluate(start);
        const geometry::Vector3 p1 = wire.Evaluate(start + direction * step);
        const geometry::Vector3 p2 = wire.Evaluate(start + direction * step * 2.0);
        const geometry::Vector3 firstDerivative = (p1 - p0) / step;
        const double speedSquared = firstDerivative.LengthSquared();
        if (speedSquared <= 1.0e-18) {
            throw std::invalid_argument("Curvature anchor must have a non-zero endpoint tangent.");
        }
        const geometry::Vector3 tangent = firstDerivative / std::sqrt(speedSquared);
        const geometry::Vector3 secondDerivative = (p2 - p1 * 2.0 + p0) / (step * step);
        return (secondDerivative
            - tangent * geometry::Dot(secondDerivative, tangent)) / speedSquared;
    }
    case WireKind::Circle:
    case WireKind::CircularArc: {
        const WireArcData arc = wire.ArcData();
        const geometry::Vector3 endpointPoint = EndpointPoint(wire, endpoint);
        return (arc.center - endpointPoint) / (arc.radius * arc.radius);
    }
    }
    throw std::logic_error("Unknown wire kind.");
}

Wire AlignBezierEndpointTangent(
    const Wire& wire,
    WireEndpoint endpoint,
    geometry::Vector3 interiorDirection)
{
    if (wire.Kind() != WireKind::CubicBezier && wire.Kind() != WireKind::CubicBSpline) {
        throw std::invalid_argument("Tangent followers must be Bezier or B-spline wires.");
    }
    std::vector<geometry::Vector3> points = wire.ControlPoints();
    const std::size_t endpointIndex = endpoint == WireEndpoint::Start ? 0 : points.size() - 1;
    const std::size_t handleIndex = endpoint == WireEndpoint::Start ? 1 : points.size() - 2;
    const double handleLength = (points[handleIndex] - points[endpointIndex]).Length();
    if (handleLength <= 1.0e-9) {
        throw std::invalid_argument("Tangent follower handle must have a non-zero length.");
    }
    points[handleIndex] = points[endpointIndex] + interiorDirection.Normalized() * handleLength;
    return wire.Kind() == WireKind::CubicBezier
        ? Wire::CubicBezier(points[0], points[1], points[2], points[3])
        : Wire::CubicBSplineWithKnots(std::move(points), wire.BSplineKnots());
}

Wire AlignBezierEndpointCurvature(
    const Wire& wire,
    WireEndpoint endpoint,
    geometry::Vector3 interiorDirection,
    geometry::Vector3 curvature,
    std::optional<geometry::Vector3> requiredPlaneNormal)
{
    const Wire tangentAligned = AlignBezierEndpointTangent(wire, endpoint, interiorDirection);
    std::vector<geometry::Vector3> points = tangentAligned.ControlPoints();
    const std::size_t endpointIndex = endpoint == WireEndpoint::Start ? 0 : 3;
    const std::size_t firstHandleIndex = endpoint == WireEndpoint::Start ? 1 : 2;
    const std::size_t secondHandleIndex = endpoint == WireEndpoint::Start ? 2 : 1;
    const geometry::Vector3 firstHandle = points[firstHandleIndex] - points[endpointIndex];
    const double handleLength = firstHandle.Length();
    const geometry::Vector3 tangent = firstHandle / handleLength;
    curvature = curvature - tangent * geometry::Dot(curvature, tangent);
    if (requiredPlaneNormal.has_value()
        && std::abs(geometry::Dot(curvature, requiredPlaneNormal->Normalized())) > 1.0e-7) {
        throw std::invalid_argument("Curvature direction would leave the follower work plane.");
    }

    const geometry::Vector3 secondDerivativeOrigin =
        points[firstHandleIndex] * 2.0 - points[endpointIndex];
    const geometry::Vector3 previousOffset =
        points[secondHandleIndex] - secondDerivativeOrigin;
    const geometry::Vector3 tangentOffset =
        tangent * geometry::Dot(previousOffset, tangent);
    points[secondHandleIndex] = secondDerivativeOrigin + tangentOffset
        + curvature * (1.5 * handleLength * handleLength);
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
    if (wire.Kind() == WireKind::CubicBezier || wire.Kind() == WireKind::CubicBSpline) {
        return AlignBezierEndpointTangent(wire, endpoint, interiorDirection);
    }
    if (wire.Kind() == WireKind::CircularArc) {
        return AlignArcEndpointTangent(
            wire, endpoint, interiorDirection, std::move(requiredPlaneNormal));
    }
    throw std::invalid_argument("Tangent followers must be Bezier, B-spline, or circular arc wires.");
}

bool TangentAlignmentMatches(
    const Wire& first,
    const Wire& second,
    WireEndpoint endpoint,
    WireContinuity continuity)
{
    if (first.Kind() != second.Kind()) {
        return false;
    }
    if (first.Kind() == WireKind::CubicBezier || first.Kind() == WireKind::CubicBSpline) {
        const std::size_t handleIndex = endpoint == WireEndpoint::Start
            ? 1 : first.ControlPoints().size() - 2;
        if (!geometry::AlmostEqual(
                first.ControlPoints()[handleIndex], second.ControlPoints()[handleIndex], 1.0e-9)) {
            return false;
        }
        if (continuity == WireContinuity::G2Curvature) {
            const std::size_t secondHandleIndex = endpoint == WireEndpoint::Start ? 2 : 1;
            return geometry::AlmostEqual(
                first.ControlPoints()[secondHandleIndex],
                second.ControlPoints()[secondHandleIndex],
                1.0e-9);
        }
        return true;
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

bool WireGeometryMatches(const Wire& first, const Wire& second, double tolerance = 1.0e-9)
{
    if (first.Kind() != second.Kind()
        || first.ControlPoints().size() != second.ControlPoints().size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.ControlPoints().size(); ++index) {
        if (!geometry::AlmostEqual(
                first.ControlPoints()[index], second.ControlPoints()[index], tolerance)) {
            return false;
        }
    }
    if (first.Kind() != WireKind::Circle && first.Kind() != WireKind::CircularArc) {
        return true;
    }
    const WireArcData firstArc = first.ArcData();
    const WireArcData secondArc = second.ArcData();
    return geometry::AlmostEqual(firstArc.center, secondArc.center, tolerance)
        && geometry::AlmostEqual(firstArc.uAxis, secondArc.uAxis, tolerance)
        && geometry::AlmostEqual(firstArc.vAxis, secondArc.vAxis, tolerance)
        && geometry::AlmostEqual(firstArc.radius, secondArc.radius, tolerance)
        && geometry::AlmostEqual(
            firstArc.startAngleRadians, secondArc.startAngleRadians, tolerance)
        && geometry::AlmostEqual(
            firstArc.sweepAngleRadians, secondArc.sweepAngleRadians, tolerance);
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
    case WireKind::CubicBSpline: {
        std::vector<geometry::Vector3> points = wire.ControlPoints();
        const std::size_t endpointIndex = endpoint == WireEndpoint::Start ? 0 : points.size() - 1;
        const std::size_t handleIndex = endpoint == WireEndpoint::Start ? 1 : points.size() - 2;
        const geometry::Vector3 delta = point - points[endpointIndex];
        points[endpointIndex] = point;
        points[handleIndex] = points[handleIndex] + delta;
        return Wire::CubicBSplineWithKnots(std::move(points), wire.BSplineKnots());
    }
    case WireKind::CircularArc:
        return wire.Translated(point - EndpointPoint(wire, endpoint));
    case WireKind::Circle:
        throw std::invalid_argument("Closed circles do not have editable endpoints.");
    }
    throw std::logic_error("Unknown wire kind.");
}

Wire AlignConstrainedLineEndpoint(
    const Project& project,
    const NamedWire& namedWire,
    WireEndpoint endpoint,
    geometry::Vector3 target)
{
    if (namedWire.wire.Kind() != WireKind::Line
        || namedWire.metadata.lineConstraints.Empty()) {
        return ReplaceWireEndpoint(namedWire.wire, endpoint, target);
    }

    const WireLineConstraints& constraints = namedWire.metadata.lineConstraints;
    geometry::Vector3 direction;
    if (constraints.angleDegrees.has_value()) {
        const std::optional<WorkPlane> plane = ConstraintPlane(project, namedWire.metadata);
        if (!plane.has_value()) {
            throw std::invalid_argument(
                "An angle-constrained line requires a source work plane: " + namedWire.name);
        }
        if (std::abs(plane->Project(target).w) > 1.0e-7) {
            throw std::invalid_argument(
                "Endpoint coincidence conflicts with the line work plane: " + namedWire.name);
        }
        constexpr double pi = 3.14159265358979323846;
        const double angleRadians = *constraints.angleDegrees * pi / 180.0;
        direction = plane->UAxis() * std::cos(angleRadians)
            + plane->VAxis() * std::sin(angleRadians);
    } else {
        direction = (namedWire.wire.End() - namedWire.wire.Start()).Normalized();
    }

    const double length = constraints.lengthMillimeters.value_or(
        (namedWire.wire.End() - namedWire.wire.Start()).Length());
    return endpoint == WireEndpoint::Start
        ? Wire::Line(target, target + direction * length)
        : Wire::Line(target - direction * length, target);
}

void RequireConstructionWireHasNoModelDependencies(const Project& project, std::string_view wireName)
{
    for (const NamedSurface& surface : project.Surfaces()) {
        if (std::find(surface.sourceWireNames.begin(), surface.sourceWireNames.end(), wireName)
            != surface.sourceWireNames.end()) {
            throw std::invalid_argument("A surface source wire cannot be changed to construction: "
                + std::string(wireName));
        }
        if (std::find(surface.guideWireNames.begin(), surface.guideWireNames.end(), wireName)
            != surface.guideWireNames.end()) {
            throw std::invalid_argument("A surface guide wire cannot be changed to construction: "
                + std::string(wireName));
        }
    }
    for (const NamedWire& wire : project.Wires()) {
        if (wire.projection.has_value() && wire.projection->sourceWireName == wireName) {
            throw std::invalid_argument("A projection source wire cannot be changed to construction: "
                + std::string(wireName));
        }
        if (wire.plateOffset.has_value() && wire.plateOffset->sourceWireName == wireName) {
            throw std::invalid_argument("A plate-offset source wire cannot be changed to construction: "
                + std::string(wireName));
        }
    }
    for (const NamedPlate& plate : project.Plates()) {
        if (std::find(plate.openingWireNames.begin(), plate.openingWireNames.end(), wireName)
            != plate.openingWireNames.end()) {
            throw std::invalid_argument("A plate opening wire cannot be changed to construction: "
                + std::string(wireName));
        }
        if (std::find(plate.reliefCutWireNames.begin(), plate.reliefCutWireNames.end(), wireName)
            != plate.reliefCutWireNames.end()) {
            throw std::invalid_argument("A plate relief-cut wire cannot be changed to construction: "
                + std::string(wireName));
        }
        if (std::find(plate.splitWireNames.begin(), plate.splitWireNames.end(), wireName)
            != plate.splitWireNames.end()) {
            throw std::invalid_argument("A plate split-line wire cannot be changed to construction: "
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

Wire BuildPlateOffsetWire(
    const NamedWire& source,
    const NamedPlate& plate,
    double throughThickness)
{
    if (!std::isfinite(throughThickness)
        || throughThickness < 0.0 || throughThickness > 1.0) {
        throw std::invalid_argument("Plate wire position must be between 0 and 1.");
    }
    if (!source.projection.has_value()
        || source.projection->targetSurfaceName != plate.sourceSurfaceName) {
        throw std::invalid_argument(
            "Plate offset source must be a wire projected to the plate source surface: "
            + source.name);
    }
    if (!OpeningLiesWithinRange(plate.plate.SourceSurface(), source, plate.plate.Range())) {
        throw std::invalid_argument("Plate offset source lies outside this plate piece: " + source.name);
    }

    const std::vector<geometry::Vector3>& sourcePoints = source.wire.ControlPoints();
    std::vector<geometry::Vector3> points;
    points.reserve(sourcePoints.size());
    for (const geometry::Vector3& point : sourcePoints) {
        const SurfaceProjection projection = plate.plate.SourceSurface().ProjectPointAlongDirection(
            point, source.projection->direction);
        const double localV = (projection.v - plate.plate.Range().minimumV)
            / (plate.plate.Range().maximumV - plate.plate.Range().minimumV);
        const double offset = plate.plate.MinimumOffset(localV)
            + (plate.plate.MaximumOffset(localV) - plate.plate.MinimumOffset(localV))
                * throughThickness;
        points.push_back(projection.point
            + plate.plate.SourceSurface().Normal(projection.u, projection.v) * offset);
    }
    if (points.size() < 2) {
        throw std::invalid_argument("Plate offset wire collapsed to fewer than two points.");
    }
    if (source.wire.IsClosed()) {
        points.back() = points.front();
    }
    return Wire::Polyline(std::move(points));
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

void Project::AddPoint(
    std::string name,
    geometry::Vector3 point,
    std::optional<std::string> sourcePlaneName)
{
    if (name.empty()) {
        throw std::invalid_argument("Point name must not be empty.");
    }
    if (!point.IsFinite()) {
        throw std::invalid_argument("Point coordinates must be finite.");
    }
    if (std::any_of(points_.begin(), points_.end(), [&](const NamedPoint& existingPoint) {
            return existingPoint.name == name;
        })) {
        throw std::invalid_argument("Point name already exists: " + name);
    }
    if (sourcePlaneName.has_value() && !FindWorkPlane(*sourcePlaneName).has_value()) {
        throw std::invalid_argument("Point source plane does not exist: " + *sourcePlaneName);
    }
    points_.push_back({std::move(name), point, std::move(sourcePlaneName)});
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
    wires_.push_back({std::move(name), std::move(wire), std::move(metadata), std::nullopt, true, {}});
}

void Project::AddPlanarSurface(std::string name, std::string boundaryWireName)
{
    AddPlanarSurface(
        std::move(name), std::vector<std::string>{std::move(boundaryWireName)});
}

void Project::AddPlanarSurface(
    std::string name,
    std::vector<std::string> boundaryWireNames)
{
    if (name.empty()) {
        throw std::invalid_argument("Surface name must not be empty.");
    }
    if (FindSurface(name).has_value()) {
        throw std::invalid_argument("Surface name already exists: " + name);
    }
    if (boundaryWireNames.empty()) {
        throw std::invalid_argument("Planar surface requires at least one boundary wire.");
    }
    Wire composite = BuildSurfaceWireGroup(
        boundaryWireNames, true, false, "planar surface boundary");
    surfaces_.push_back({
        std::move(name),
        Surface::Planar(std::move(composite)),
        boundaryWireNames,
    });
    surfaces_.back().sourceWireGroups.push_back(std::move(boundaryWireNames));
}

void Project::AddRuledSurface(std::string name, std::string firstSectionName, std::string secondSectionName)
{
    AddRuledSurface(
        std::move(name),
        std::vector<std::string>{std::move(firstSectionName)},
        std::vector<std::string>{std::move(secondSectionName)});
}

void Project::AddRuledSurface(
    std::string name,
    std::vector<std::string> firstSectionNames,
    std::vector<std::string> secondSectionNames)
{
    if (name.empty()) {
        throw std::invalid_argument("Surface name must not be empty.");
    }
    if (FindSurface(name).has_value()) {
        throw std::invalid_argument("Surface name already exists: " + name);
    }
    std::vector<std::string> sources = firstSectionNames;
    sources.insert(sources.end(), secondSectionNames.begin(), secondSectionNames.end());
    for (const std::string& source : sources) {
        if (std::count(sources.begin(), sources.end(), source) != 1) {
            throw std::invalid_argument(
                "Ruled-surface source wire is repeated: " + source);
        }
    }
    Wire first = BuildSurfaceWireGroup(
        firstSectionNames, false, true, "first ruled-surface section");
    Wire second = BuildSurfaceWireGroup(
        secondSectionNames, false, true, "second ruled-surface section");
    surfaces_.push_back({
        std::move(name),
        Surface::Ruled(std::move(first), std::move(second)),
        std::move(sources),
    });
    surfaces_.back().sourceWireGroups = {
        std::move(firstSectionNames), std::move(secondSectionNames)};
}

void Project::AddLoftSurface(std::string name, std::vector<std::string> sectionNames)
{
    std::vector<std::vector<std::string>> groups;
    groups.reserve(sectionNames.size());
    for (std::string& sectionName : sectionNames) {
        groups.push_back({std::move(sectionName)});
    }
    AddLoftSurface(std::move(name), std::move(groups));
}

void Project::AddLoftSurface(
    std::string name,
    std::vector<std::vector<std::string>> sectionWireGroups)
{
    if (name.empty()) {
        throw std::invalid_argument("Surface name must not be empty.");
    }
    if (FindSurface(name).has_value()) {
        throw std::invalid_argument("Surface name already exists: " + name);
    }
    if (sectionWireGroups.size() < 3) {
        throw std::invalid_argument("Loft surface requires at least three section wires.");
    }

    std::vector<std::string> sectionNames;
    for (const auto& group : sectionWireGroups) {
        sectionNames.insert(sectionNames.end(), group.begin(), group.end());
    }
    std::vector<Wire> sections;
    sections.reserve(sectionWireGroups.size());
    for (const std::string& sectionName : sectionNames) {
        if (std::count(sectionNames.begin(), sectionNames.end(), sectionName) != 1) {
            throw std::invalid_argument("Loft section wires must not be repeated: " + sectionName);
        }
    }
    for (const auto& group : sectionWireGroups) {
        sections.push_back(BuildSurfaceWireGroup(
            group, true, false, "loft section"));
    }
    surfaces_.push_back({
        std::move(name), Surface::Loft(std::move(sections)),
        std::move(sectionNames)});
    surfaces_.back().sourceWireGroups = std::move(sectionWireGroups);
}

void Project::AddGuidedLoftSurface(
    std::string name,
    std::string firstGuideName,
    std::string secondGuideName,
    std::vector<std::string> sectionNames)
{
    std::vector<std::vector<std::string>> groups;
    groups.reserve(sectionNames.size());
    for (std::string& sectionName : sectionNames) {
        groups.push_back({std::move(sectionName)});
    }
    AddGuidedLoftSurface(
        std::move(name),
        std::vector<std::string>{std::move(firstGuideName)},
        std::vector<std::string>{std::move(secondGuideName)},
        std::move(groups));
}

void Project::AddGuidedLoftSurface(
    std::string name,
    std::vector<std::string> firstGuideNames,
    std::vector<std::string> secondGuideNames,
    std::vector<std::vector<std::string>> sectionWireGroups)
{
    if (name.empty()) {
        throw std::invalid_argument("Surface name must not be empty.");
    }
    if (FindSurface(name).has_value()) {
        throw std::invalid_argument("Surface name already exists: " + name);
    }
    if (firstGuideNames.empty() || secondGuideNames.empty()
        || sectionWireGroups.empty()) {
        throw std::invalid_argument(
            "Guided loft requires two different guides and at least one cross section.");
    }
    std::vector<std::string> sources = firstGuideNames;
    sources.insert(sources.end(), secondGuideNames.begin(), secondGuideNames.end());
    for (const auto& group : sectionWireGroups) {
        sources.insert(sources.end(), group.begin(), group.end());
    }
    for (const std::string& sourceName : sources) {
        if (std::count(sources.begin(), sources.end(), sourceName) != 1) {
            throw std::invalid_argument(
                "Guided-loft wires must not be repeated: " + sourceName);
        }
    }
    std::vector<Wire> sections;
    sections.reserve(sectionWireGroups.size());
    for (const auto& group : sectionWireGroups) {
        sections.push_back(BuildSurfaceWireGroup(
            group, true, true, "guided-loft cross section"));
    }
    Wire firstGuide = BuildSurfaceWireGroup(
        firstGuideNames, true, true, "first guided-loft guide");
    Wire secondGuide = BuildSurfaceWireGroup(
        secondGuideNames, true, true, "second guided-loft guide");
    surfaces_.push_back({
        std::move(name),
        Surface::GuidedLoft(
            std::move(firstGuide),
            std::move(secondGuide),
            std::move(sections)),
        std::move(sources),
    });
    surfaces_.back().sourceWireGroups.reserve(sectionWireGroups.size() + 2);
    surfaces_.back().sourceWireGroups.push_back(std::move(firstGuideNames));
    surfaces_.back().sourceWireGroups.push_back(std::move(secondGuideNames));
    for (auto& group : sectionWireGroups) {
        surfaces_.back().sourceWireGroups.push_back(std::move(group));
    }
}

void Project::AddGordonSurface(
    std::string name,
    std::vector<std::string> sectionNames,
    std::vector<std::string> guideNames)
{
    if (name.empty()) {
        throw std::invalid_argument("Surface name must not be empty.");
    }
    if (FindSurface(name).has_value()) {
        throw std::invalid_argument("Surface name already exists: " + name);
    }
    if (sectionNames.size() < 2) {
        throw std::invalid_argument("Gordon surface requires at least two section wires.");
    }
    if (guideNames.empty()) {
        throw std::invalid_argument("Gordon surface requires at least one guide wire.");
    }

    std::vector<Wire> sections;
    sections.reserve(sectionNames.size());
    for (const std::string& sectionName : sectionNames) {
        if (std::count(sectionNames.begin(), sectionNames.end(), sectionName) != 1) {
            throw std::invalid_argument("Gordon section wires must not be repeated: " + sectionName);
        }
        if (std::count(guideNames.begin(), guideNames.end(), sectionName) != 0) {
            throw std::invalid_argument(
                "Gordon surface wire cannot be used as both a section and a guide: " + sectionName);
        }
        const NamedWire& section = RequireWire(sectionName);
        if (section.projection.has_value()) {
            throw std::invalid_argument("Projected wire cannot be used as a Gordon surface section.");
        }
        if (section.plateOffset.has_value()) {
            throw std::invalid_argument("Plate-offset wire cannot be used as a Gordon surface section.");
        }
        if (section.metadata.construction) {
            throw std::invalid_argument("Construction wire cannot be used as a Gordon surface section.");
        }
        sections.push_back(section.wire);
    }

    std::vector<Wire> guides;
    guides.reserve(guideNames.size());
    for (const std::string& guideName : guideNames) {
        if (std::count(guideNames.begin(), guideNames.end(), guideName) != 1) {
            throw std::invalid_argument("Gordon guide wires must not be repeated: " + guideName);
        }
        const NamedWire& guide = RequireWire(guideName);
        if (guide.projection.has_value()) {
            throw std::invalid_argument("Projected wire cannot be used as a Gordon surface guide.");
        }
        if (guide.plateOffset.has_value()) {
            throw std::invalid_argument("Plate-offset wire cannot be used as a Gordon surface guide.");
        }
        if (guide.metadata.construction) {
            throw std::invalid_argument("Construction wire cannot be used as a Gordon surface guide.");
        }
        guides.push_back(guide.wire);
    }

    surfaces_.push_back({
        std::move(name),
        Surface::Gordon(std::move(sections), std::move(guides)),
        std::move(sectionNames),
        true,
        std::move(guideNames),
    });
}

void Project::AddPlate(
    std::string name,
    std::string sourceSurfaceName,
    double thickness,
    PlateThicknessDirection direction,
    std::string material)
{
    AddPlate(
        std::move(name), std::move(sourceSurfaceName), thickness, thickness,
        direction, std::move(material));
}

void Project::AddPlate(
    std::string name,
    std::string sourceSurfaceName,
    double startThickness,
    double endThickness,
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
    // 面に登録済みの開口(窓・ライト等)は、この面から作る板材へ自動で引き継ぐ。
    std::vector<std::string> inheritedOpenings;
    for (const NamedSurface& named : surfaces_) {
        if (named.name == sourceSurfaceName) {
            inheritedOpenings = named.openingWireNames;
            break;
        }
    }
    plates_.push_back({
        std::move(name),
        Plate(*surface, startThickness, endThickness, direction),
        std::move(sourceSurfaceName),
        std::move(material),
        std::move(inheritedOpenings),
        true,
        {},
        {},
    });
}

void Project::SetPlateLaminate(std::string_view name, std::string_view basePlateName)
{
    const auto plate = std::find_if(plates_.begin(), plates_.end(),
        [&](const NamedPlate& candidate) { return candidate.name == name; });
    if (plate == plates_.end()) {
        throw std::invalid_argument("Plate name does not exist: " + std::string(name));
    }
    if (basePlateName.empty()) {
        plate->laminateBaseName.clear();
        if (std::abs(plate->plate.BaseOffset()) > 1.0e-12) {
            plate->plate = Plate(
                plate->plate.SourceSurface(),
                plate->plate.Thickness(),
                plate->plate.EndThickness(),
                plate->plate.Direction(),
                plate->plate.Range(),
                0.0);
        }
        return;
    }
    if (basePlateName == name) {
        throw std::invalid_argument("Plate cannot laminate onto itself: " + std::string(name));
    }
    const auto base = std::find_if(plates_.begin(), plates_.end(),
        [&](const NamedPlate& candidate) { return candidate.name == basePlateName; });
    if (base == plates_.end()) {
        throw std::invalid_argument(
            "Laminate base plate does not exist: " + std::string(basePlateName));
    }
    // 循環の拒否(base から親をたどって name に戻らないこと)。
    std::string_view cursor = basePlateName;
    for (std::size_t guard = 0; guard <= plates_.size(); ++guard) {
        const auto current = std::find_if(plates_.begin(), plates_.end(),
            [&](const NamedPlate& candidate) { return candidate.name == cursor; });
        if (current == plates_.end() || current->laminateBaseName.empty()) {
            break;
        }
        if (current->laminateBaseName == name) {
            throw std::invalid_argument(
                "Laminate relation would create a cycle: " + std::string(name));
        }
        cursor = current->laminateBaseName;
    }
    if (base->sourceSurfaceName == plate->sourceSurfaceName) {
        // 同じ元面の積層は幾何も追従する。v1では可変厚・中央合わせの base を許可しない。
        if (base->plate.HasVariableThickness()) {
            throw std::invalid_argument(
                "Laminating onto a variable-thickness plate is not supported yet: "
                + std::string(basePlateName));
        }
        if (base->plate.Direction() == PlateThicknessDirection::Centered) {
            throw std::invalid_argument(
                "Laminate base must use a one-sided thickness direction (not centered): "
                + std::string(basePlateName));
        }
        if (plate->plate.Direction() != base->plate.Direction()) {
            throw std::invalid_argument(
                "Laminated plate must use the same thickness direction as its base.");
        }
    }
    plate->laminateBaseName = std::string(basePlateName);
    RecomputeLaminateOffsets();
}

void Project::AddLaminatedPlate(
    std::string name,
    std::string_view basePlateName,
    double thickness,
    std::string material)
{
    const auto base = std::find_if(plates_.begin(), plates_.end(),
        [&](const NamedPlate& candidate) { return candidate.name == basePlateName; });
    if (base == plates_.end()) {
        throw std::invalid_argument(
            "Laminate base plate does not exist: " + std::string(basePlateName));
    }
    if (name.empty()) {
        throw std::invalid_argument("Plate name must not be empty.");
    }
    if (FindPlate(name).has_value()) {
        throw std::invalid_argument("Plate name already exists: " + name);
    }
    if (material.empty()) {
        material = base->material;
    }
    const std::string baseName(basePlateName);
    plates_.push_back({
        std::move(name),
        Plate(base->plate.SourceSurface(), thickness, thickness,
            base->plate.Direction(), base->plate.Range()),
        base->sourceSurfaceName,
        std::move(material),
        {},
        true,
        {},
        {},
    });
    SetPlateLaminate(plates_.back().name, baseName);
}

void Project::RecomputeLaminateOffsets()
{
    // 積層の連鎖(最長でも板の枚数)ぶんだけ反復し、下から順に下駄を確定させる。
    for (std::size_t pass = 0; pass < plates_.size(); ++pass) {
        bool changed = false;
        for (NamedPlate& plate : plates_) {
            if (plate.laminateBaseName.empty()) {
                continue;
            }
            const auto base = std::find_if(plates_.begin(), plates_.end(),
                [&](const NamedPlate& candidate) {
                    return candidate.name == plate.laminateBaseName;
                });
            if (base == plates_.end()) {
                throw std::logic_error(
                    "Laminate base plate is missing: " + plate.laminateBaseName);
            }
            if (base->sourceSurfaceName != plate.sourceSurfaceName) {
                continue; // 別の面に描いた積層は関係の記録のみ。
            }
            double desired = base->plate.BaseOffset();
            if (base->plate.Direction() == PlateThicknessDirection::Positive) {
                desired += base->plate.Thickness();
            } else if (base->plate.Direction() == PlateThicknessDirection::Negative) {
                desired -= base->plate.Thickness();
            }
            if (std::abs(plate.plate.BaseOffset() - desired) <= 1.0e-12) {
                continue;
            }
            plate.plate = Plate(
                plate.plate.SourceSurface(),
                plate.plate.Thickness(),
                plate.plate.EndThickness(),
                plate.plate.Direction(),
                plate.plate.Range(),
                desired);
            changed = true;
        }
        if (!changed) {
            break;
        }
    }
}

void Project::AddSurfaceJig(
    std::string name,
    std::string sourceSurfaceName,
    PlateSurfaceRange range,
    JigSide side,
    double clearanceMillimeters,
    double thicknessMillimeters)
{
    if (name.empty()) {
        throw std::invalid_argument("Body name must not be empty.");
    }
    if (FindBody(name).has_value()) {
        throw std::invalid_argument("Body name already exists: " + name);
    }
    const std::optional<Surface> surface = FindSurface(sourceSurfaceName);
    if (!surface.has_value()) {
        throw std::invalid_argument("Jig source surface does not exist: " + sourceSurfaceName);
    }
    bodies_.push_back({
        std::move(name),
        Body::SurfaceJig(
            *surface, range, side, clearanceMillimeters, thicknessMillimeters),
        std::move(sourceSurfaceName),
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
    if (source.projection.has_value() || source.plateOffset.has_value()) {
        throw std::invalid_argument("A derived wire cannot be used as another projection source.");
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
        true,
        {},
    });
}

void Project::AddPlateOffsetWire(
    std::string name,
    std::string sourceWireName,
    std::string plateName,
    double throughThickness)
{
    if (name.empty()) {
        throw std::invalid_argument("Plate offset wire name must not be empty.");
    }
    for (const NamedWire& existingWire : wires_) {
        if (existingWire.name == name) {
            throw std::invalid_argument("Wire name already exists: " + name);
        }
    }
    const NamedWire& source = RequireWire(sourceWireName);
    const auto plate = std::find_if(plates_.begin(), plates_.end(), [&](const NamedPlate& candidate) {
        return candidate.name == plateName;
    });
    if (plate == plates_.end()) {
        throw std::invalid_argument("Plate name does not exist: " + plateName);
    }
    Wire offsetWire = BuildPlateOffsetWire(source, *plate, throughThickness);
    wires_.push_back({
        std::move(name),
        std::move(offsetWire),
        {},
        std::nullopt,
        true,
        NamedWire::PlateOffset{
            std::move(sourceWireName), std::move(plateName), throughThickness},
    });
}

int Project::TranslateObjects(
    const std::vector<std::pair<ProjectObjectKind, std::string>>& targets,
    const geometry::Vector3& delta)
{
    if (!delta.IsFinite()) {
        throw std::invalid_argument("移動量に有限でない値が含まれています。");
    }

    Project candidate = *this;
    std::vector<std::string> workPlaneNames;
    std::vector<std::string> pointNames;
    std::vector<std::string> wireNames;

    const auto containsName = [](const std::vector<std::string>& names, std::string_view name) {
        return std::any_of(names.begin(), names.end(), [&](const std::string& current) {
            return current == name;
        });
    };
    const auto addUniqueName = [&](std::vector<std::string>& names, const std::string& name) {
        if (!containsName(names, name)) {
            names.push_back(name);
        }
    };
    const auto derivedMessage = [](std::string_view kind, const std::string& name) {
        return std::string(kind) + "は移動対象にできません: " + name
            + "。元(下書き・板材・近似モデル)側を動かしてください。";
    };

    const auto requireWorkPlane = [&](const std::string& name) -> const NamedWorkPlane& {
        const auto plane = std::find_if(
            candidate.workPlanes_.begin(), candidate.workPlanes_.end(),
            [&](const NamedWorkPlane& current) {
                return current.name == name;
            });
        if (plane == candidate.workPlanes_.end()) {
            throw std::invalid_argument("移動対象の作業平面が見つかりません: " + name);
        }
        return *plane;
    };
    const auto requirePoint = [&](const std::string& name) -> const NamedPoint& {
        const auto point = std::find_if(
            candidate.points_.begin(), candidate.points_.end(),
            [&](const NamedPoint& current) {
                return current.name == name;
            });
        if (point == candidate.points_.end()) {
            throw std::invalid_argument("移動対象の点が見つかりません: " + name);
        }
        return *point;
    };
    const auto requireWire = [&](const std::string& name) -> const NamedWire& {
        const auto wire = std::find_if(
            candidate.wires_.begin(), candidate.wires_.end(),
            [&](const NamedWire& current) {
                return current.name == name;
            });
        if (wire == candidate.wires_.end()) {
            throw std::invalid_argument("移動対象のワイヤが見つかりません: " + name);
        }
        return *wire;
    };
    const auto requireSurface = [&](const std::string& name) -> const NamedSurface& {
        const auto surface = std::find_if(
            candidate.surfaces_.begin(), candidate.surfaces_.end(),
            [&](const NamedSurface& current) {
                return current.name == name;
            });
        if (surface == candidate.surfaces_.end()) {
            throw std::invalid_argument("移動対象の面が見つかりません: " + name);
        }
        return *surface;
    };
    const auto requirePlate = [&](const std::string& name) -> const NamedPlate& {
        const auto plate = std::find_if(
            candidate.plates_.begin(), candidate.plates_.end(),
            [&](const NamedPlate& current) {
                return current.name == name;
            });
        if (plate == candidate.plates_.end()) {
            throw std::invalid_argument("移動対象の板材が見つかりません: " + name);
        }
        return *plate;
    };
    const auto requireBody = [&](const std::string& name) -> const NamedBody& {
        const auto body = std::find_if(
            candidate.bodies_.begin(), candidate.bodies_.end(),
            [&](const NamedBody& current) {
                return current.name == name;
            });
        if (body == candidate.bodies_.end()) {
            throw std::invalid_argument("移動対象の治具が見つかりません: " + name);
        }
        return *body;
    };
    const auto requirePartModel = [&](const std::string& name) {
        const auto model = std::find_if(
            candidate.partModels_.begin(), candidate.partModels_.end(),
            [&](const NamedPartModel& current) {
                return current.name == name;
            });
        if (model == candidate.partModels_.end()) {
            throw std::invalid_argument(
                "移動対象の部材近似モデルが見つかりません: " + name);
        }
    };

    const auto addBaseWire = [&](const std::string& name) {
        const NamedWire& wire = requireWire(name);
        if (wire.projection.has_value()
            || wire.plateOffset.has_value()
            || wire.partModelSourceName.has_value()) {
            throw std::invalid_argument(derivedMessage("派生ワイヤ", wire.name));
        }
        addUniqueName(wireNames, wire.name);
    };
    const auto expandSurface = [&](const std::string& name) {
        const NamedSurface& surface = requireSurface(name);
        if (surface.partModelSourceName.has_value()) {
            throw std::invalid_argument(
                derivedMessage("部材近似モデルの派生面", surface.name));
        }
        for (const std::string& wireName : surface.sourceWireNames) {
            addBaseWire(wireName);
        }
        for (const std::string& wireName : surface.guideWireNames) {
            addBaseWire(wireName);
        }
    };

    for (const auto& target : targets) {
        const std::string& targetName = target.second;
        switch (target.first) {
        case ProjectObjectKind::WorkPlane: {
            const NamedWorkPlane& plane = requireWorkPlane(targetName);
            addUniqueName(workPlaneNames, plane.name);
            for (const NamedWire& wire : candidate.wires_) {
                if (wire.metadata.sourcePlaneName.has_value()
                    && *wire.metadata.sourcePlaneName == plane.name) {
                    addBaseWire(wire.name);
                }
            }
            break;
        }
        case ProjectObjectKind::Point: {
            const NamedPoint& point = requirePoint(targetName);
            addUniqueName(pointNames, point.name);
            break;
        }
        case ProjectObjectKind::Wire:
            addBaseWire(targetName);
            break;
        case ProjectObjectKind::Surface:
            expandSurface(targetName);
            break;
        case ProjectObjectKind::Plate: {
            const NamedPlate& plate = requirePlate(targetName);
            expandSurface(plate.sourceSurfaceName);
            break;
        }
        case ProjectObjectKind::Body: {
            const NamedBody& body = requireBody(targetName);
            expandSurface(body.sourceSurfaceName);
            break;
        }
        case ProjectObjectKind::PartModel:
            requirePartModel(targetName);
            throw std::invalid_argument(
                derivedMessage("部材近似モデル", targetName));
        }
    }

    const int movedCount = static_cast<int>(
        workPlaneNames.size() + pointNames.size() + wireNames.size());
    if (movedCount == 0) {
        throw std::invalid_argument("移動できる対象がありません。");
    }

    for (NamedWorkPlane& plane : candidate.workPlanes_) {
        if (containsName(workPlaneNames, plane.name)) {
            plane.plane = plane.plane.Translated(delta);
        }
    }
    for (NamedPoint& point : candidate.points_) {
        if (containsName(pointNames, point.name)) {
            point.point = point.point + delta;
        }
    }
    for (NamedWire& wire : candidate.wires_) {
        if (containsName(wireNames, wire.name)) {
            wire.wire = ConstrainWire(candidate, wire.wire.Translated(delta), wire.metadata);
        }
    }

    candidate.RebuildDependentGeometry();
    *this = std::move(candidate);
    return movedCount;
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
            if (namedWire.projection.has_value() || namedWire.plateOffset.has_value()) {
                throw std::invalid_argument("Derived wire must be edited through its source geometry.");
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
        if (namedWire.projection.has_value() || namedWire.plateOffset.has_value()) {
            throw std::invalid_argument("Derived wire must be edited through its source geometry.");
        }
        if (metadata.construction) {
            RequireConstructionWireHasNoModelDependencies(candidate, name);
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
    UpdatePlate(
        name, std::move(sourceSurfaceName), thickness, thickness,
        direction, std::move(material));
}

void Project::UpdatePlate(
    std::string_view name,
    std::string sourceSurfaceName,
    double startThickness,
    double endThickness,
    PlateThicknessDirection direction,
    std::string material)
{
    if (material.empty()) {
        throw std::invalid_argument("Plate material must not be empty.");
    }
    Project candidate = *this;
    const std::optional<Surface> sourceSurface = candidate.FindSurface(sourceSurfaceName);
    if (!sourceSurface.has_value()) {
        throw std::invalid_argument("Plate source surface does not exist: " + sourceSurfaceName);
    }
    for (NamedPlate& plate : candidate.plates_) {
        if (plate.name == name) {
            for (const std::string& openingName : plate.openingWireNames) {
                const NamedWire& opening = candidate.RequireWire(openingName);
                if (!opening.projection.has_value()
                    || opening.projection->targetSurfaceName != sourceSurfaceName) {
                    throw std::invalid_argument("Remove plate openings before changing the source surface.");
                }
                if (!OpeningLiesWithinRange(*sourceSurface, opening, plate.plate.Range())) {
                    throw std::invalid_argument("Updated plate surface does not contain opening: " + openingName);
                }
            }
            for (const std::string& cutName : plate.reliefCutWireNames) {
                const NamedWire& cut = candidate.RequireWire(cutName);
                if (!cut.projection.has_value()
                    || cut.projection->targetSurfaceName != sourceSurfaceName) {
                    throw std::invalid_argument("Remove plate relief cuts before changing the source surface.");
                }
                if (!OpeningLiesWithinRange(*sourceSurface, cut, plate.plate.Range())) {
                    throw std::invalid_argument("Updated plate surface does not contain relief cut: " + cutName);
                }
            }
            for (const std::string& splitName : plate.splitWireNames) {
                const NamedWire& split = candidate.RequireWire(splitName);
                if (!split.projection.has_value()
                    || split.projection->targetSurfaceName != sourceSurfaceName) {
                    throw std::invalid_argument("Remove plate split lines before changing the source surface.");
                }
                if (!OpeningLiesWithinRange(*sourceSurface, split, plate.plate.Range())) {
                    throw std::invalid_argument("Updated plate surface does not contain split line: " + splitName);
                }
            }
            plate.plate = Plate(
                *sourceSurface, startThickness, endThickness, direction, plate.plate.Range(),
                plate.plate.BaseOffset());
            plate.sourceSurfaceName = std::move(sourceSurfaceName);
            plate.material = std::move(material);
            candidate.RebuildDependentGeometry();
            *this = std::move(candidate);
            return;
        }
    }
    throw std::invalid_argument("Plate name does not exist: " + std::string(name));
}

void Project::UpdateSurfaceJig(
    std::string_view name,
    std::string sourceSurfaceName,
    PlateSurfaceRange range,
    JigSide side,
    double clearanceMillimeters,
    double thicknessMillimeters)
{
    const std::optional<Surface> sourceSurface = FindSurface(sourceSurfaceName);
    if (!sourceSurface.has_value()) {
        throw std::invalid_argument("Jig source surface does not exist: " + sourceSurfaceName);
    }
    for (NamedBody& body : bodies_) {
        if (body.name == name) {
            body.body = Body::SurfaceJig(
                *sourceSurface,
                range,
                side,
                clearanceMillimeters,
                thicknessMillimeters);
            body.sourceSurfaceName = std::move(sourceSurfaceName);
            return;
        }
    }
    throw std::invalid_argument("Body name does not exist: " + std::string(name));
}

void Project::SetWireMetadata(std::string_view name, WireMetadata metadata)
{
    Project candidate = *this;
    if (metadata.sourcePlaneName.has_value() && !candidate.FindWorkPlane(*metadata.sourcePlaneName).has_value()) {
        throw std::invalid_argument("Wire source plane does not exist: " + *metadata.sourcePlaneName);
    }

    for (NamedWire& wire : candidate.wires_) {
        if (wire.name == name) {
            if (wire.projection.has_value() || wire.plateOffset.has_value()) {
                throw std::invalid_argument("Derived wire metadata is controlled by its source geometry.");
            }
            if (metadata.construction) {
                RequireConstructionWireHasNoModelDependencies(candidate, name);
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
    WireEndpointReference follower,
    WireContinuity continuity)
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
    if (continuity == WireContinuity::G2Curvature
        && followerWire.wire.Kind() != WireKind::CubicBezier) {
        throw std::invalid_argument("The curvature follower must be a cubic Bezier wire.");
    }
    if (continuity == WireContinuity::G1Tangent
        && followerWire.wire.Kind() != WireKind::CubicBezier
        && followerWire.wire.Kind() != WireKind::CubicBSpline
        && followerWire.wire.Kind() != WireKind::CircularArc) {
        throw std::invalid_argument("The tangent follower must be a Bezier, B-spline, or circular arc wire.");
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
    candidate.tangentConstraints_.push_back(
        {std::move(anchor), std::move(follower), continuity});
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

void Project::SetPointVisible(std::string_view name, bool visible)
{
    for (NamedPoint& point : points_) {
        if (point.name == name) {
            point.visible = visible;
            return;
        }
    }
    throw std::invalid_argument("Point name does not exist: " + std::string(name));
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

void Project::SetBodyVisible(std::string_view name, bool visible)
{
    for (NamedBody& body : bodies_) {
        if (body.name == name) {
            body.visible = visible;
            return;
        }
    }
    throw std::invalid_argument("Body name does not exist: " + std::string(name));
}

void Project::SetPlateRange(std::string_view name, PlateSurfaceRange range)
{
    Project candidate = *this;
    for (NamedPlate& plate : candidate.plates_) {
        if (plate.name != name) {
            continue;
        }
        if (plate.plate.SourceSurface().Kind() == SurfaceKind::Planar && !range.IsFull()) {
            throw std::invalid_argument("Planar plate ranges are not supported.");
        }
        for (const std::string& openingName : plate.openingWireNames) {
            if (!OpeningLiesWithinRange(plate.plate.SourceSurface(), candidate.RequireWire(openingName), range)) {
                throw std::invalid_argument("Plate range does not contain opening: " + openingName);
            }
        }
        for (const std::string& cutName : plate.reliefCutWireNames) {
            if (!OpeningLiesWithinRange(plate.plate.SourceSurface(), candidate.RequireWire(cutName), range)) {
                throw std::invalid_argument("Plate range does not contain relief cut: " + cutName);
            }
        }
        for (const std::string& splitName : plate.splitWireNames) {
            if (!OpeningLiesWithinRange(plate.plate.SourceSurface(), candidate.RequireWire(splitName), range)) {
                throw std::invalid_argument("Plate range does not contain split line: " + splitName);
            }
        }
        plate.plate = Plate(
            plate.plate.SourceSurface(),
            plate.plate.Thickness(),
            plate.plate.EndThickness(),
            plate.plate.Direction(),
            range,
            plate.plate.BaseOffset());
        candidate.RebuildDependentGeometry();
        *this = std::move(candidate);
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
    for (const NamedPartModel& model : partModels_) {
        if (model.sourcePlateName == name) {
            throw std::invalid_argument("Plate is used by part model: " + model.name);
        }
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
    if (!position->splitWireNames.empty()) {
        throw std::invalid_argument("Remove papercraft split lines before replacing this plate with two plate objects.");
    }
    for (const NamedWire& wire : wires_) {
        if (wire.plateOffset.has_value() && wire.plateOffset->plateName == name) {
            throw std::invalid_argument(
                "Delete plate-offset wire before splitting this plate: " + wire.name);
        }
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

    std::vector<std::string> firstReliefCuts;
    std::vector<std::string> secondReliefCuts;
    for (const std::string& cutName : position->reliefCutWireNames) {
        const NamedWire& cut = RequireWire(cutName);
        if (!cut.projection.has_value()) {
            throw std::logic_error("Plate relief-cut projection relation is missing.");
        }
        bool beforeSplit = false;
        bool afterSplit = false;
        for (int sample = 0; sample <= 24; ++sample) {
            const geometry::Vector3 point = cut.wire.Evaluate(static_cast<double>(sample) / 24.0);
            const SurfaceProjection projection = position->plate.SourceSurface().ProjectPointAlongDirection(
                point,
                cut.projection->direction);
            const double coordinate = axis == PlateSplitAxis::U ? projection.u : projection.v;
            beforeSplit = beforeSplit || coordinate < splitCoordinate - 1.0e-5;
            afterSplit = afterSplit || coordinate > splitCoordinate + 1.0e-5;
        }
        if (beforeSplit && afterSplit) {
            throw std::invalid_argument("Plate split line crosses relief cut: " + cutName);
        }
        if (!beforeSplit && !afterSplit) {
            throw std::invalid_argument("Plate split line coincides with relief cut: " + cutName);
        }
        (beforeSplit ? firstReliefCuts : secondReliefCuts).push_back(cutName);
    }

    for (const NamedPlate& other : plates_) {
        if (other.laminateBaseName == position->name) {
            throw std::invalid_argument(
                "Plate is used as laminate base by: " + other.name);
        }
    }
    const std::string sourceSurfaceName = position->sourceSurfaceName;
    const std::string material = position->material;
    const bool visible = position->visible;
    const std::string laminateBaseName = position->laminateBaseName;
    const std::size_t insertionIndex = static_cast<std::size_t>(std::distance(plates_.begin(), position));
    plates_.erase(position);
    NamedPlate firstNamedPlate{
        std::move(firstName),
        firstPlate,
        sourceSurfaceName,
        material,
        std::move(firstOpenings),
        visible,
        std::move(firstReliefCuts),
        {},
        laminateBaseName,
    };
    NamedPlate secondNamedPlate{
        std::move(secondName),
        secondPlate,
        sourceSurfaceName,
        material,
        std::move(secondOpenings),
        visible,
        std::move(secondReliefCuts),
        {},
        laminateBaseName,
    };
    plates_.insert(
        plates_.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
        std::move(firstNamedPlate));
    plates_.insert(
        plates_.begin() + static_cast<std::ptrdiff_t>(insertionIndex + 1),
        std::move(secondNamedPlate));
}

void Project::AddSurfaceOpening(std::string_view surfaceName, std::string wireName)
{
    const auto surface = std::find_if(surfaces_.begin(), surfaces_.end(),
        [&](const NamedSurface& candidate) { return candidate.name == surfaceName; });
    if (surface == surfaces_.end()) {
        throw std::invalid_argument("Surface name does not exist: " + std::string(surfaceName));
    }
    if (surface->partModelSourceName.has_value()) {
        throw std::invalid_argument(
            "Part-model derived surfaces manage openings automatically: "
            + std::string(surfaceName));
    }
    const NamedWire& wire = RequireWire(wireName);
    if (wire.metadata.construction) {
        throw std::invalid_argument(
            "Construction wire cannot be used as a surface opening: " + wireName);
    }
    if (!wire.wire.IsClosed()) {
        throw std::invalid_argument("Surface opening wire must be closed: " + wireName);
    }
    if (!wire.projection.has_value() || wire.projection->targetSurfaceName != surface->name) {
        throw std::invalid_argument(
            "Surface opening must be a wire projected to this surface: " + wireName);
    }
    if (std::find(surface->openingWireNames.begin(), surface->openingWireNames.end(), wireName)
        != surface->openingWireNames.end()) {
        throw std::invalid_argument("Wire is already a surface opening: " + wireName);
    }
    surface->openingWireNames.push_back(std::move(wireName));
}

void Project::RemoveSurfaceOpening(std::string_view surfaceName, std::string_view wireName)
{
    const auto surface = std::find_if(surfaces_.begin(), surfaces_.end(),
        [&](const NamedSurface& candidate) { return candidate.name == surfaceName; });
    if (surface == surfaces_.end()) {
        throw std::invalid_argument("Surface name does not exist: " + std::string(surfaceName));
    }
    const auto position = std::find(
        surface->openingWireNames.begin(), surface->openingWireNames.end(), wireName);
    if (position == surface->openingWireNames.end()) {
        throw std::invalid_argument(
            "Wire is not a surface opening: " + std::string(wireName));
    }
    surface->openingWireNames.erase(position);
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
    if (std::find(plate->reliefCutWireNames.begin(), plate->reliefCutWireNames.end(), wireName)
        != plate->reliefCutWireNames.end()) {
        throw std::invalid_argument("Plate relief cut cannot also be an opening: " + wireName);
    }
    if (std::find(plate->splitWireNames.begin(), plate->splitWireNames.end(), wireName)
        != plate->splitWireNames.end()) {
        throw std::invalid_argument("Plate split line cannot also be an opening: " + wireName);
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

void Project::AddPlateReliefCut(std::string_view plateName, std::string wireName)
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
        throw std::invalid_argument("Construction wire cannot be used as a plate relief cut: " + wireName);
    }
    if (!wire.projection.has_value() || wire.projection->targetSurfaceName != plate->sourceSurfaceName) {
        throw std::invalid_argument("Plate relief cut must be a wire projected to the plate source surface: " + wireName);
    }
    if (!OpeningLiesWithinRange(plate->plate.SourceSurface(), wire, plate->plate.Range())) {
        throw std::invalid_argument("Plate relief cut lies outside this plate piece: " + wireName);
    }
    if (std::find(plate->reliefCutWireNames.begin(), plate->reliefCutWireNames.end(), wireName)
        != plate->reliefCutWireNames.end()) {
        throw std::invalid_argument("Wire is already a plate relief cut: " + wireName);
    }
    if (std::find(plate->openingWireNames.begin(), plate->openingWireNames.end(), wireName)
        != plate->openingWireNames.end()) {
        throw std::invalid_argument("Plate opening cannot also be a relief cut: " + wireName);
    }
    if (std::find(plate->splitWireNames.begin(), plate->splitWireNames.end(), wireName)
        != plate->splitWireNames.end()) {
        throw std::invalid_argument("Plate split line cannot also be a relief cut: " + wireName);
    }
    plate->reliefCutWireNames.push_back(std::move(wireName));
}

void Project::RemovePlateReliefCut(std::string_view plateName, std::string_view wireName)
{
    for (NamedPlate& plate : plates_) {
        if (plate.name != plateName) {
            continue;
        }
        const auto position = std::find(
            plate.reliefCutWireNames.begin(), plate.reliefCutWireNames.end(), wireName);
        if (position == plate.reliefCutWireNames.end()) {
            throw std::invalid_argument("Wire is not a relief cut of this plate: " + std::string(wireName));
        }
        plate.reliefCutWireNames.erase(position);
        return;
    }
    throw std::invalid_argument("Plate name does not exist: " + std::string(plateName));
}

void Project::AddPlateSplitLine(std::string_view plateName, std::string wireName)
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
        throw std::invalid_argument("Construction wire cannot be used as a plate split line: " + wireName);
    }
    if (!wire.projection.has_value() || wire.projection->targetSurfaceName != plate->sourceSurfaceName) {
        throw std::invalid_argument("Plate split line must be a wire projected to the plate source surface: " + wireName);
    }
    if (!OpeningLiesWithinRange(plate->plate.SourceSurface(), wire, plate->plate.Range())) {
        throw std::invalid_argument("Plate split line lies outside this plate piece: " + wireName);
    }
    if (std::find(plate->splitWireNames.begin(), plate->splitWireNames.end(), wireName)
        != plate->splitWireNames.end()) {
        throw std::invalid_argument("Wire is already a plate split line: " + wireName);
    }
    if (std::find(plate->openingWireNames.begin(), plate->openingWireNames.end(), wireName)
        != plate->openingWireNames.end()
        || std::find(plate->reliefCutWireNames.begin(), plate->reliefCutWireNames.end(), wireName)
            != plate->reliefCutWireNames.end()) {
        throw std::invalid_argument("An opening or relief cut cannot also be a plate split line: " + wireName);
    }
    plate->splitWireNames.push_back(std::move(wireName));
}

void Project::RemovePlateSplitLine(std::string_view plateName, std::string_view wireName)
{
    for (NamedPlate& plate : plates_) {
        if (plate.name != plateName) {
            continue;
        }
        const auto position = std::find(plate.splitWireNames.begin(), plate.splitWireNames.end(), wireName);
        if (position == plate.splitWireNames.end()) {
            throw std::invalid_argument("Wire is not a split line of this plate: " + std::string(wireName));
        }
        plate.splitWireNames.erase(position);
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
    for (NamedPoint& point : points_) {
        if (point.sourcePlaneName.has_value() && *point.sourcePlaneName == name) {
            point.sourcePlaneName.reset();
        }
    }
    return true;
}

bool Project::RemovePoint(std::string_view name)
{
    const auto position = std::find_if(points_.begin(), points_.end(), [&](const NamedPoint& point) {
        return point.name == name;
    });
    if (position == points_.end()) {
        return false;
    }
    points_.erase(position);
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
    if (position->partModelSourceName.has_value()) {
        throw std::invalid_argument(
            "Part-model boundary wires cannot be removed individually: "
            + std::string(name));
    }

    for (const NamedSurface& surface : surfaces_) {
        if (std::find(surface.sourceWireNames.begin(), surface.sourceWireNames.end(), name)
            != surface.sourceWireNames.end()) {
            throw std::invalid_argument("Wire is used by surface: " + surface.name);
        }
        if (std::find(surface.guideWireNames.begin(), surface.guideWireNames.end(), name)
            != surface.guideWireNames.end()) {
            throw std::invalid_argument("Wire is used as a surface guide: " + surface.name);
        }
        if (std::find(surface.openingWireNames.begin(), surface.openingWireNames.end(), name)
            != surface.openingWireNames.end()) {
            throw std::invalid_argument("Wire is used as a surface opening: " + surface.name);
        }
    }
    for (const NamedWire& wire : wires_) {
        if (wire.projection.has_value() && wire.projection->sourceWireName == name) {
            throw std::invalid_argument("Wire is used as a projection drawing: " + wire.name);
        }
        if (wire.plateOffset.has_value() && wire.plateOffset->sourceWireName == name) {
            throw std::invalid_argument("Wire is used as a plate-offset source: " + wire.name);
        }
    }
    for (const NamedPlate& plate : plates_) {
        if (std::find(plate.openingWireNames.begin(), plate.openingWireNames.end(), name)
            != plate.openingWireNames.end()) {
            throw std::invalid_argument("Wire is used as a plate opening: " + plate.name);
        }
        if (std::find(plate.reliefCutWireNames.begin(), plate.reliefCutWireNames.end(), name)
            != plate.reliefCutWireNames.end()) {
            throw std::invalid_argument("Wire is used as a plate relief cut: " + plate.name);
        }
        if (std::find(plate.splitWireNames.begin(), plate.splitWireNames.end(), name)
            != plate.splitWireNames.end()) {
            throw std::invalid_argument("Wire is used as a plate split line: " + plate.name);
        }
    }
    for (const NamedPartModel& model : partModels_) {
        if (std::find(model.scopeWireNames.begin(), model.scopeWireNames.end(), name)
            != model.scopeWireNames.end()) {
            throw std::invalid_argument(
                "Wire is used by a part-model connection scope: " + model.name);
        }
        for (const NamedPartModel::PartOpening& record : model.partOpenings) {
            if (record.sourceWireName == name) {
                throw std::invalid_argument(
                    "Wire is used as a part-model opening source: " + model.name);
            }
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
    for (const NamedSurface& surface : surfaces_) {
        if (surface.name == name && surface.partModelSourceName.has_value()) {
            throw std::invalid_argument(
                "Part-model surfaces cannot be removed individually: "
                + std::string(name));
        }
    }
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
    for (const NamedBody& body : bodies_) {
        if (body.sourceSurfaceName == name) {
            throw std::invalid_argument("Surface is used by body: " + body.name);
        }
    }
    for (const NamedPartModel& model : partModels_) {
        if (model.sourceSurfaceName == name) {
            throw std::invalid_argument("Surface is used by part model: " + model.name);
        }
        if (std::find(model.scopeSurfaceNames.begin(), model.scopeSurfaceNames.end(), name)
            != model.scopeSurfaceNames.end()) {
            throw std::invalid_argument(
                "Surface is used by a part-model connection scope: " + model.name);
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
    for (const NamedWire& wire : wires_) {
        if (wire.plateOffset.has_value() && wire.plateOffset->plateName == name) {
            throw std::invalid_argument("Plate is used by offset wire: " + wire.name);
        }
    }
    for (const NamedPartModel& model : partModels_) {
        if (model.sourcePlateName == name) {
            throw std::invalid_argument("Plate is used by part model: " + model.name);
        }
    }
    for (const NamedPlate& other : plates_) {
        if (other.laminateBaseName == name) {
            throw std::invalid_argument(
                "Plate is used as laminate base by: " + other.name);
        }
    }
    plates_.erase(position);
    return true;
}

bool Project::RemoveBody(std::string_view name)
{
    const auto position = std::find_if(bodies_.begin(), bodies_.end(), [&](const NamedBody& body) {
        return body.name == name;
    });
    if (position == bodies_.end()) {
        return false;
    }
    bodies_.erase(position);
    return true;
}

void Project::RenameReferences(
    ProjectObjectKind kind, std::string_view oldName, const std::string& newName)
{
    const auto renameIn = [&](std::string& value) {
        if (value == oldName) {
            value = newName;
        }
    };
    const auto renameList = [&](std::vector<std::string>& values) {
        for (std::string& value : values) {
            renameIn(value);
        }
    };
    // セット所属は全種共通。
    for (ObjectSet& set : objectSets_) {
        for (ObjectSetMember& member : set.members) {
            if (member.kind == kind && member.name == oldName) {
                member.name = newName;
            }
        }
    }
    switch (kind) {
    case ProjectObjectKind::WorkPlane:
        for (NamedPoint& point : points_) {
            if (point.sourcePlaneName.has_value() && *point.sourcePlaneName == oldName) {
                point.sourcePlaneName = newName;
            }
        }
        for (NamedWire& wire : wires_) {
            if (wire.metadata.sourcePlaneName.has_value()
                && *wire.metadata.sourcePlaneName == oldName) {
                wire.metadata.sourcePlaneName = newName;
            }
        }
        for (ReferenceDimension& dimension : referenceDimensions_) {
            for (DimensionReference* reference : {&dimension.first, &dimension.second}) {
                if (reference->kind == DimensionReferenceKind::WorkPlane) {
                    renameIn(reference->objectName);
                }
            }
        }
        break;
    case ProjectObjectKind::Point:
        break;
    case ProjectObjectKind::Wire:
        for (NamedWire& wire : wires_) {
            if (wire.projection.has_value()) {
                renameIn(wire.projection->sourceWireName);
            }
            if (wire.plateOffset.has_value()) {
                renameIn(wire.plateOffset->sourceWireName);
            }
        }
        for (NamedSurface& surface : surfaces_) {
            renameList(surface.sourceWireNames);
            renameList(surface.guideWireNames);
            renameList(surface.openingWireNames);
            for (std::vector<std::string>& group : surface.sourceWireGroups) {
                renameList(group);
            }
        }
        for (NamedPlate& plate : plates_) {
            renameList(plate.openingWireNames);
            renameList(plate.reliefCutWireNames);
            renameList(plate.splitWireNames);
        }
        for (NamedPartModel& model : partModels_) {
            renameList(model.scopeWireNames);
            renameList(model.boundaryWireNames);
            renameList(model.openingWireNames);
            renameList(model.adaptedWireNames);
            for (NamedPartModel::PartOpening& record : model.partOpenings) {
                renameIn(record.sourceWireName);
            }
        }
        for (WireCoincidentConstraint& constraint : coincidentConstraints_) {
            renameIn(constraint.anchor.wireName);
            renameIn(constraint.follower.wireName);
        }
        for (WireTangentConstraint& constraint : tangentConstraints_) {
            renameIn(constraint.anchor.wireName);
            renameIn(constraint.follower.wireName);
        }
        for (ReferenceDimension& dimension : referenceDimensions_) {
            for (DimensionReference* reference : {&dimension.first, &dimension.second}) {
                if (reference->kind == DimensionReferenceKind::Wire) {
                    renameIn(reference->objectName);
                }
            }
        }
        break;
    case ProjectObjectKind::Surface:
        for (NamedWire& wire : wires_) {
            if (wire.projection.has_value()) {
                renameIn(wire.projection->targetSurfaceName);
            }
        }
        for (NamedPlate& plate : plates_) {
            renameIn(plate.sourceSurfaceName);
        }
        for (NamedBody& body : bodies_) {
            renameIn(body.sourceSurfaceName);
        }
        for (NamedPartModel& model : partModels_) {
            renameIn(model.sourceSurfaceName);
            renameList(model.scopeSurfaceNames);
            renameList(model.partSurfaceNames);
            renameList(model.adaptedSurfaceNames);
        }
        break;
    case ProjectObjectKind::Plate:
        for (NamedWire& wire : wires_) {
            if (wire.plateOffset.has_value()) {
                renameIn(wire.plateOffset->plateName);
            }
        }
        for (NamedPlate& plate : plates_) {
            renameIn(plate.laminateBaseName);
        }
        for (NamedPartModel& model : partModels_) {
            renameIn(model.sourcePlateName);
        }
        break;
    case ProjectObjectKind::Body:
        break;
    case ProjectObjectKind::PartModel:
        for (NamedWire& wire : wires_) {
            if (wire.partModelSourceName.has_value() && *wire.partModelSourceName == oldName) {
                wire.partModelSourceName = newName;
            }
        }
        for (NamedSurface& surface : surfaces_) {
            if (surface.partModelSourceName.has_value()
                && *surface.partModelSourceName == oldName) {
                surface.partModelSourceName = newName;
            }
        }
        break;
    }
}

void Project::RenameObject(ProjectObjectKind kind, std::string_view oldName, std::string newName)
{
    if (newName.empty()) {
        throw std::invalid_argument("New name must not be empty.");
    }
    if (newName == oldName) {
        return;
    }
    const auto ensureUnique = [&](const auto& container) {
        const auto clash = std::find_if(container.begin(), container.end(), [&](const auto& entry) {
            return entry.name == newName;
        });
        if (clash != container.end()) {
            throw std::invalid_argument("Name already exists: " + newName);
        }
    };
    switch (kind) {
    case ProjectObjectKind::WorkPlane: {
        const auto entry = std::find_if(workPlanes_.begin(), workPlanes_.end(),
            [&](const NamedWorkPlane& plane) { return plane.name == oldName; });
        if (entry == workPlanes_.end()) {
            throw std::invalid_argument("Work plane not found: " + std::string(oldName));
        }
        ensureUnique(workPlanes_);
        entry->name = newName;
        RenameReferences(kind, oldName, newName);
        break;
    }
    case ProjectObjectKind::Point: {
        const auto entry = std::find_if(points_.begin(), points_.end(),
            [&](const NamedPoint& point) { return point.name == oldName; });
        if (entry == points_.end()) {
            throw std::invalid_argument("Point not found: " + std::string(oldName));
        }
        ensureUnique(points_);
        entry->name = newName;
        RenameReferences(kind, oldName, newName);
        break;
    }
    case ProjectObjectKind::Wire: {
        const auto entry = std::find_if(wires_.begin(), wires_.end(),
            [&](const NamedWire& wire) { return wire.name == oldName; });
        if (entry == wires_.end()) {
            throw std::invalid_argument("Wire not found: " + std::string(oldName));
        }
        if (entry->partModelSourceName.has_value()) {
            throw std::invalid_argument(
                "Part-model derived wires cannot be renamed directly: " + std::string(oldName));
        }
        ensureUnique(wires_);
        entry->name = newName;
        RenameReferences(kind, oldName, newName);
        break;
    }
    case ProjectObjectKind::Surface: {
        const auto entry = std::find_if(surfaces_.begin(), surfaces_.end(),
            [&](const NamedSurface& surface) { return surface.name == oldName; });
        if (entry == surfaces_.end()) {
            throw std::invalid_argument("Surface not found: " + std::string(oldName));
        }
        if (entry->partModelSourceName.has_value()) {
            throw std::invalid_argument(
                "Part-model derived surfaces cannot be renamed directly: " + std::string(oldName));
        }
        ensureUnique(surfaces_);
        entry->name = newName;
        RenameReferences(kind, oldName, newName);
        break;
    }
    case ProjectObjectKind::Plate: {
        const auto entry = std::find_if(plates_.begin(), plates_.end(),
            [&](const NamedPlate& plate) { return plate.name == oldName; });
        if (entry == plates_.end()) {
            throw std::invalid_argument("Plate not found: " + std::string(oldName));
        }
        ensureUnique(plates_);
        entry->name = newName;
        RenameReferences(kind, oldName, newName);
        break;
    }
    case ProjectObjectKind::Body: {
        const auto entry = std::find_if(bodies_.begin(), bodies_.end(),
            [&](const NamedBody& body) { return body.name == oldName; });
        if (entry == bodies_.end()) {
            throw std::invalid_argument("Body not found: " + std::string(oldName));
        }
        ensureUnique(bodies_);
        entry->name = newName;
        RenameReferences(kind, oldName, newName);
        break;
    }
    case ProjectObjectKind::PartModel: {
        const auto entry = std::find_if(partModels_.begin(), partModels_.end(),
            [&](const NamedPartModel& model) { return model.name == oldName; });
        if (entry == partModels_.end()) {
            throw std::invalid_argument("Part model not found: " + std::string(oldName));
        }
        ensureUnique(partModels_);
        // 派生物(<モデル名>_境界N / _部材N / _部材N_穴M)を新しい接頭辞へ連動リネーム。
        const std::string oldPrefix = std::string(oldName) + "_";
        const std::string newPrefix = newName + "_";
        const auto renamedDerived = [&](const std::string& derivedName) {
            if (derivedName.rfind(oldPrefix, 0) == 0) {
                return newPrefix + derivedName.substr(oldPrefix.size());
            }
            return derivedName;
        };
        const auto renameDerivedList = [&](std::vector<std::string>& names,
                                           ProjectObjectKind derivedKind, bool isWire) {
            for (std::string& derivedName : names) {
                const std::string replacement = renamedDerived(derivedName);
                if (replacement == derivedName) {
                    continue;
                }
                const bool clash = isWire
                    ? std::any_of(wires_.begin(), wires_.end(),
                          [&](const NamedWire& wire) { return wire.name == replacement; })
                    : std::any_of(surfaces_.begin(), surfaces_.end(),
                          [&](const NamedSurface& surface) { return surface.name == replacement; });
                if (clash) {
                    throw std::invalid_argument("Name already exists: " + replacement);
                }
                if (isWire) {
                    for (NamedWire& wire : wires_) {
                        if (wire.name == derivedName) {
                            wire.name = replacement;
                        }
                    }
                } else {
                    for (NamedSurface& surface : surfaces_) {
                        if (surface.name == derivedName) {
                            surface.name = replacement;
                        }
                    }
                }
                RenameReferences(derivedKind, derivedName, replacement);
                derivedName = replacement;
            }
        };
        renameDerivedList(entry->boundaryWireNames, ProjectObjectKind::Wire, true);
        renameDerivedList(entry->openingWireNames, ProjectObjectKind::Wire, true);
        renameDerivedList(entry->adaptedWireNames, ProjectObjectKind::Wire, true);
        renameDerivedList(entry->partSurfaceNames, ProjectObjectKind::Surface, false);
        renameDerivedList(entry->adaptedSurfaceNames, ProjectObjectKind::Surface, false);
        entry->name = newName;
        RenameReferences(kind, oldName, newName);
        // 自動セット「近似:<名前>」も連動して付け替える。
        const std::string oldSetName = "近似:" + std::string(oldName);
        if (FindObjectSetMutable(oldSetName) != nullptr) {
            const std::string newSetName = "近似:" + newName;
            if (FindObjectSetMutable(newSetName) != nullptr) {
                throw std::invalid_argument("Set name already exists: " + newSetName);
            }
            for (ObjectSet& set : objectSets_) {
                if (set.name == oldSetName) {
                    set.name = newSetName;
                }
                if (set.parentName == oldSetName) {
                    set.parentName = newSetName;
                }
            }
        }
        break;
    }
    }
}

void Project::RenameObjectSet(std::string_view oldName, std::string newName)
{
    if (newName.empty()) {
        throw std::invalid_argument("New set name must not be empty.");
    }
    if (newName == oldName) {
        return;
    }
    ObjectSet* set = FindObjectSetMutable(oldName);
    if (set == nullptr) {
        throw std::invalid_argument("Set not found: " + std::string(oldName));
    }
    if (set->automatic) {
        throw std::invalid_argument(
            "Automatic sets cannot be renamed: " + std::string(oldName));
    }
    if (FindObjectSetMutable(newName) != nullptr) {
        throw std::invalid_argument("Set name already exists: " + newName);
    }
    set->name = newName;
    for (ObjectSet& child : objectSets_) {
        if (child.parentName == oldName) {
            child.parentName = newName;
        }
    }
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

std::optional<Body> Project::FindBody(std::string_view name) const
{
    for (const NamedBody& body : bodies_) {
        if (body.name == name) {
            return body.body;
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

Wire Project::BuildSurfaceWireGroup(
    const std::vector<std::string>& wireNames,
    bool rejectProjected,
    bool rejectPlateOffset,
    std::string_view role) const
{
    if (wireNames.empty()) {
        throw std::invalid_argument(
            std::string(role) + " requires at least one source wire.");
    }
    std::vector<Wire> wires;
    wires.reserve(wireNames.size());
    for (std::size_t index = 0; index < wireNames.size(); ++index) {
        if (std::find(wireNames.begin(), wireNames.begin() + index,
                wireNames[index]) != wireNames.begin() + index) {
            throw std::invalid_argument(
                std::string(role) + " repeats source wire: " + wireNames[index]);
        }
        const NamedWire& source = RequireWire(wireNames[index]);
        if (rejectProjected && source.projection.has_value()) {
            throw std::invalid_argument(
                "Projected wire cannot be used as " + std::string(role) + ".");
        }
        if (rejectPlateOffset && source.plateOffset.has_value()) {
            throw std::invalid_argument(
                "Plate-offset wire cannot be used as " + std::string(role) + ".");
        }
        if (source.metadata.construction) {
            throw std::invalid_argument(
                "Construction wire cannot be used as " + std::string(role) + ".");
        }
        wires.push_back(source.wire);
    }
    return JoinWireChain(wires);
}

void Project::ApplyCoincidentConstraints()
{
    const std::size_t maximumPasses = coincidentConstraints_.size() * 2 + 2;
    for (std::size_t pass = 0; pass < maximumPasses; ++pass) {
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
            if (follower->metadata.planePolicy == WirePlanePolicy::LockedToPlane
                && follower->metadata.sourcePlaneName.has_value()) {
                const std::optional<WorkPlane> plane = FindWorkPlane(*follower->metadata.sourcePlaneName);
                if (!plane.has_value() || std::abs(plane->Project(target).w) > 1.0e-7) {
                    throw std::invalid_argument("Coincident endpoint would leave its locked work plane.");
                }
            }
            const Wire aligned = AlignConstrainedLineEndpoint(
                *this, *follower, constraint.follower.endpoint, target);
            const bool matches = follower->wire.Kind() == WireKind::Line
                ? geometry::AlmostEqual(follower->wire.Start(), aligned.Start(), 1.0e-9)
                    && geometry::AlmostEqual(follower->wire.End(), aligned.End(), 1.0e-9)
                : geometry::AlmostEqual(
                    EndpointPoint(follower->wire, constraint.follower.endpoint), target, 1.0e-9);
            if (matches) {
                continue;
            }
            follower->wire = aligned;
            changed = true;
        }
        if (!changed) {
            return;
        }
    }
    for (const WireCoincidentConstraint& constraint : coincidentConstraints_) {
        const NamedWire& anchor = RequireWire(constraint.anchor.wireName);
        const NamedWire& follower = RequireWire(constraint.follower.wireName);
        if (!geometry::AlmostEqual(
                EndpointPoint(anchor.wire, constraint.anchor.endpoint),
                EndpointPoint(follower.wire, constraint.follower.endpoint),
                1.0e-8)) {
            throw std::invalid_argument(
                "Conflicting endpoint and dimensional constraints on wire: "
                + constraint.follower.wireName);
        }
    }
    throw std::logic_error("Endpoint coincidence constraints did not converge.");
}

void Project::ApplyTangentConstraints()
{
    std::string lastChangedFollower;
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

            try {
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

                const Wire aligned = constraint.continuity == WireContinuity::G2Curvature
                    ? AlignBezierEndpointCurvature(
                        follower->wire,
                        constraint.follower.endpoint,
                        desiredInterior,
                        EndpointCurvatureVector(anchor->wire, constraint.anchor.endpoint),
                        requiredPlaneNormal)
                    : AlignWireEndpointTangent(
                        follower->wire,
                        constraint.follower.endpoint,
                        desiredInterior,
                        requiredPlaneNormal);
                if (TangentAlignmentMatches(
                        follower->wire,
                        aligned,
                        constraint.follower.endpoint,
                        constraint.continuity)) {
                    continue;
                }
                follower->wire = aligned;
                lastChangedFollower = constraint.follower.wireName;
                changed = true;
            } catch (const std::invalid_argument& error) {
                throw std::invalid_argument(
                    std::string(error.what()) + " Follower wire: " + constraint.follower.wireName);
            }
        }
        if (!changed) {
            return;
        }
    }
    throw std::invalid_argument(
        "Conflicting G1/G2 continuity constraints on wire: " + lastChangedFollower);
}

void Project::ApplyWireConstraints()
{
    const std::size_t maximumPasses =
        (coincidentConstraints_.size() + tangentConstraints_.size()) * 2 + 2;
    for (std::size_t pass = 0; pass < maximumPasses; ++pass) {
        std::vector<Wire> before;
        before.reserve(wires_.size());
        for (const NamedWire& wire : wires_) {
            before.push_back(wire.wire);
        }

        ApplyCoincidentConstraints();
        ApplyTangentConstraints();

        bool changed = before.size() != wires_.size();
        for (std::size_t index = 0; !changed && index < wires_.size(); ++index) {
            changed = !WireGeometryMatches(before[index], wires_[index].wire);
        }
        if (!changed) {
            return;
        }
    }

    for (const WireCoincidentConstraint& constraint : coincidentConstraints_) {
        const NamedWire& anchor = RequireWire(constraint.anchor.wireName);
        const NamedWire& follower = RequireWire(constraint.follower.wireName);
        if (!geometry::AlmostEqual(
                EndpointPoint(anchor.wire, constraint.anchor.endpoint),
                EndpointPoint(follower.wire, constraint.follower.endpoint),
                1.0e-8)) {
            throw std::invalid_argument(
                "Conflicting endpoint and smooth continuity constraints near wire: "
                + constraint.follower.wireName);
        }
    }
    throw std::invalid_argument("Wire constraint system did not converge.");
}

void Project::RebuildDependentGeometry()
{
    ApplyWireConstraints();

    const auto wireIndex = [this](const std::string& name) {
        const auto found = std::find_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
            return wire.name == name;
        });
        if (found == wires_.end()) {
            throw std::logic_error("Dependent wire is missing: " + name);
        }
        return static_cast<std::size_t>(std::distance(wires_.begin(), found));
    };
    const auto surfaceIndex = [this](const std::string& name) {
        const auto found = std::find_if(surfaces_.begin(), surfaces_.end(), [&](const NamedSurface& surface) {
            return surface.name == name;
        });
        if (found == surfaces_.end()) {
            throw std::logic_error("Dependent surface is missing: " + name);
        }
        return static_cast<std::size_t>(std::distance(surfaces_.begin(), found));
    };

    std::vector<bool> wireReady(wires_.size(), false);
    std::size_t pendingProjections = 0;
    for (std::size_t index = 0; index < wires_.size(); ++index) {
        wireReady[index] = !wires_[index].projection.has_value();
        pendingProjections += wires_[index].projection.has_value() ? 1U : 0U;
    }
    std::vector<bool> surfaceReady(surfaces_.size(), false);
    std::size_t pendingSurfaces = surfaces_.size();
    for (std::size_t index = 0; index < surfaces_.size(); ++index) {
        if (surfaces_[index].partModelSourceName.has_value()) {
            surfaceReady[index] = true;
            --pendingSurfaces;
        }
    }

    while (pendingSurfaces > 0 || pendingProjections > 0) {
        bool madeProgress = false;

        for (std::size_t index = 0; index < surfaces_.size(); ++index) {
            if (surfaceReady[index]) {
                continue;
            }
            NamedSurface& surface = surfaces_[index];
            const bool sourcesReady = std::all_of(
                surface.sourceWireNames.begin(), surface.sourceWireNames.end(),
                [&](const std::string& sourceName) { return wireReady[wireIndex(sourceName)]; })
                && std::all_of(
                    surface.guideWireNames.begin(), surface.guideWireNames.end(),
                    [&](const std::string& guideName) { return wireReady[wireIndex(guideName)]; });
            if (!sourcesReady) {
                continue;
            }

            std::vector<std::vector<std::string>> groups
                = surface.sourceWireGroups;
            if (groups.empty()) {
                if (surface.surface.Kind() == SurfaceKind::Planar) {
                    groups.push_back(surface.sourceWireNames);
                } else {
                    groups.reserve(surface.sourceWireNames.size());
                    for (const std::string& sourceName : surface.sourceWireNames) {
                        groups.push_back({sourceName});
                    }
                }
            }

            if (surface.surface.Kind() == SurfaceKind::Planar) {
                if (groups.size() != 1) {
                    throw std::logic_error(
                        "Planar surface must contain one boundary group.");
                }
                surface.surface = Surface::Planar(BuildSurfaceWireGroup(
                    groups.front(), true, false, "planar surface boundary"));
            } else if (surface.surface.Kind() == SurfaceKind::Ruled) {
                if (groups.size() != 2) {
                    throw std::logic_error(
                        "Ruled surface must contain two section groups.");
                }
                surface.surface = Surface::Ruled(
                    BuildSurfaceWireGroup(
                        groups[0], false, true, "first ruled-surface section"),
                    BuildSurfaceWireGroup(
                        groups[1], false, true, "second ruled-surface section"));
            } else if (surface.surface.Kind() == SurfaceKind::Loft) {
                std::vector<Wire> sections;
                sections.reserve(groups.size());
                for (const auto& group : groups) {
                    sections.push_back(BuildSurfaceWireGroup(
                        group, true, false, "loft section"));
                }
                surface.surface = Surface::Loft(std::move(sections));
            } else if (surface.surface.Kind() == SurfaceKind::Gordon) {
                std::vector<Wire> sections;
                sections.reserve(surface.sourceWireNames.size());
                for (const std::string& sourceName : surface.sourceWireNames) {
                    sections.push_back(RequireWire(sourceName).wire);
                }
                std::vector<Wire> guides;
                guides.reserve(surface.guideWireNames.size());
                for (const std::string& guideName : surface.guideWireNames) {
                    guides.push_back(RequireWire(guideName).wire);
                }
                surface.surface = Surface::Gordon(std::move(sections), std::move(guides));
            } else {
                if (groups.size() < 3) {
                    throw std::logic_error(
                        "Guided loft source groups are incomplete.");
                }
                std::vector<Wire> sections;
                sections.reserve(groups.size() - 2);
                for (std::size_t groupIndex = 2;
                     groupIndex < groups.size(); ++groupIndex) {
                    sections.push_back(BuildSurfaceWireGroup(
                        groups[groupIndex], true, true,
                        "guided-loft cross section"));
                }
                surface.surface = Surface::GuidedLoft(
                    BuildSurfaceWireGroup(
                        groups[0], true, true, "first guided-loft guide"),
                    BuildSurfaceWireGroup(
                        groups[1], true, true, "second guided-loft guide"),
                    std::move(sections));
            }
            surfaceReady[index] = true;
            --pendingSurfaces;
            madeProgress = true;
        }

        for (std::size_t index = 0; index < wires_.size(); ++index) {
            NamedWire& wire = wires_[index];
            if (wireReady[index] || !wire.projection.has_value()) {
                continue;
            }
            const std::size_t sourceIndex = wireIndex(wire.projection->sourceWireName);
            const std::size_t targetIndex = surfaceIndex(wire.projection->targetSurfaceName);
            if (!wireReady[sourceIndex] || !surfaceReady[targetIndex]) {
                continue;
            }
            wire.wire = surfaces_[targetIndex].surface.ProjectWireAlongDirection(
                wires_[sourceIndex].wire, wire.projection->direction);
            wireReady[index] = true;
            --pendingProjections;
            madeProgress = true;
        }

        if (!madeProgress) {
            throw std::logic_error("Surface and projected-wire dependencies contain a cycle.");
        }
    }
    for (NamedPlate& plate : plates_) {
        const std::optional<Surface> sourceSurface = FindSurface(plate.sourceSurfaceName);
        if (!sourceSurface.has_value()) {
            throw std::logic_error("Plate source surface is missing.");
        }
        plate.plate = Plate(
            *sourceSurface,
            plate.plate.Thickness(),
            plate.plate.EndThickness(),
            plate.plate.Direction(),
            plate.plate.Range(),
            plate.plate.BaseOffset());
        for (const std::string& openingName : plate.openingWireNames) {
            if (!OpeningLiesWithinRange(*sourceSurface, RequireWire(openingName), plate.plate.Range())) {
                throw std::invalid_argument("Plate opening moved outside split piece: " + openingName);
            }
        }
        for (const std::string& cutName : plate.reliefCutWireNames) {
            if (!OpeningLiesWithinRange(*sourceSurface, RequireWire(cutName), plate.plate.Range())) {
                throw std::invalid_argument("Plate relief cut moved outside split piece: " + cutName);
            }
        }
        for (const std::string& splitName : plate.splitWireNames) {
            if (!OpeningLiesWithinRange(*sourceSurface, RequireWire(splitName), plate.plate.Range())) {
                throw std::invalid_argument("Plate split line moved outside split piece: " + splitName);
            }
        }
    }
    RebuildPartModels();
    // 部材面(部材近似モデルの派生面)を元にした板材は、部材面の更新後に再構築する。
    for (NamedPlate& plate : plates_) {
        const auto sourceSurface = std::find_if(surfaces_.begin(), surfaces_.end(),
            [&](const NamedSurface& candidate) {
                return candidate.name == plate.sourceSurfaceName
                    && candidate.partModelSourceName.has_value();
            });
        if (sourceSurface == surfaces_.end()) {
            continue;
        }
        plate.plate = Plate(
            sourceSurface->surface,
            plate.plate.Thickness(),
            plate.plate.EndThickness(),
            plate.plate.Direction(),
            plate.plate.Range(),
            plate.plate.BaseOffset());
    }
    RecomputeLaminateOffsets();

    for (NamedWire& wire : wires_) {
        if (!wire.plateOffset.has_value()) {
            continue;
        }
        const NamedWire& source = RequireWire(wire.plateOffset->sourceWireName);
        const auto plate = std::find_if(plates_.begin(), plates_.end(), [&](const NamedPlate& candidate) {
            return candidate.name == wire.plateOffset->plateName;
        });
        if (plate == plates_.end()) {
            throw std::logic_error("Plate-offset wire target plate is missing.");
        }
        wire.wire = BuildPlateOffsetWire(source, *plate, wire.plateOffset->throughThickness);
    }
    for (NamedBody& body : bodies_) {
        const std::optional<Surface> sourceSurface = FindSurface(body.sourceSurfaceName);
        if (!sourceSurface.has_value()) {
            throw std::logic_error("Body source surface is missing.");
        }
        body.body = Body::SurfaceJig(
            *sourceSurface,
            body.body.Range(),
            body.body.Side(),
            body.body.ClearanceMillimeters(),
            body.body.ThicknessMillimeters());
    }
}

// ---- 部材近似モデル(ADR 0019) ----------------------------------------

PartSource Project::RequirePartModelSource(const NamedPartModel& model) const
{
    if (!model.sourceSurfaceName.empty()) {
        const auto surface = std::find_if(surfaces_.begin(), surfaces_.end(),
            [&](const NamedSurface& candidate) {
                return candidate.name == model.sourceSurfaceName;
            });
        if (surface == surfaces_.end()) {
            throw std::logic_error(
                "Part-model source surface is missing: " + model.sourceSurfaceName);
        }
        return PartSource(surface->surface);
    }
    const auto plate = std::find_if(plates_.begin(), plates_.end(),
        [&](const NamedPlate& candidate) {
            return candidate.name == model.sourcePlateName;
        });
    if (plate == plates_.end()) {
        throw std::logic_error("Part-model source plate is missing: " + model.sourcePlateName);
    }
    return PartSource(plate->plate);
}

void Project::RebuildPartModels()
{
    for (NamedPartModel& model : partModels_) {
        model.result = ApproximatePlateParts(RequirePartModelSource(model), model.options);
        RegeneratePartModelDerivedObjects(model);
    }
}

void Project::RegeneratePartModelDerivedObjects(NamedPartModel& model)
{
    // 板材入力のときだけ元板材を引く(開口の投影に使う)。面入力では nullptr。
    const NamedPlate* sourcePlate = nullptr;
    if (model.sourceSurfaceName.empty()) {
        const auto plate = std::find_if(plates_.begin(), plates_.end(), [&](const NamedPlate& candidate) {
            return candidate.name == model.sourcePlateName;
        });
        if (plate == plates_.end()) {
            throw std::logic_error("Part-model source plate is missing: " + model.sourcePlateName);
        }
        sourcePlate = &*plate;
    }

    // レール(帯の縁+内部境界)のパラメータ列。部材数+1本。
    std::vector<double> parameters;
    parameters.push_back(0.0);
    for (std::size_t index = 1; index < model.result.parts.size(); ++index) {
        parameters.push_back(model.result.parts[index].minimumParameter);
    }
    parameters.push_back(1.0);

    // --- レールの派生ワイヤ ---
    std::vector<std::string> newWireNames;
    const std::string wirePrefix = model.name + "_境界";
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const std::string wireName = wirePrefix + std::to_string(index + 1);
        Wire boundary = BuildPartBoundaryWire(
            RequirePartModelSource(model), model.options.splitAxis, parameters[index]);
        const auto existing = std::find_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
            return wire.name == wireName;
        });
        if (existing != wires_.end()) {
            if (!existing->partModelSourceName.has_value()
                || *existing->partModelSourceName != model.name) {
                throw std::invalid_argument(
                    "Part-model boundary wire name is already used: " + wireName);
            }
            existing->wire = std::move(boundary);
        } else {
            NamedWire derived{wireName, std::move(boundary), {}, std::nullopt, model.visible, std::nullopt, model.name};
            wires_.push_back(std::move(derived));
        }
        newWireNames.push_back(wireName);
    }
    for (const std::string& oldName : model.boundaryWireNames) {
        if (std::find(newWireNames.begin(), newWireNames.end(), oldName) != newWireNames.end()) {
            continue;
        }
        wires_.erase(
            std::remove_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
                return wire.name == oldName
                    && wire.partModelSourceName.has_value()
                    && *wire.partModelSourceName == model.name;
            }),
            wires_.end());
        RemoveObjectFromSets(ProjectObjectKind::Wire, oldName);
    }
    model.boundaryWireNames = newWireNames;

    // --- 部材ごとの派生ルールド面(角ばった近似の実形状) ---
    std::vector<std::string> newSurfaceNames;
    const std::string surfacePrefix = model.name + "_部材";
    for (std::size_t index = 0; index < model.result.parts.size(); ++index) {
        const std::string surfaceName = surfacePrefix + std::to_string(index + 1);
        const auto bottomWire = std::find_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
            return wire.name == newWireNames[index];
        });
        const auto topWire = std::find_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
            return wire.name == newWireNames[index + 1];
        });
        if (bottomWire == wires_.end() || topWire == wires_.end()) {
            throw std::logic_error("Part-model rail wire is missing.");
        }
        Surface ruled = Surface::Ruled(bottomWire->wire, topWire->wire);
        const auto existing = std::find_if(surfaces_.begin(), surfaces_.end(), [&](const NamedSurface& surface) {
            return surface.name == surfaceName;
        });
        if (existing != surfaces_.end()) {
            if (!existing->partModelSourceName.has_value()
                || *existing->partModelSourceName != model.name) {
                throw std::invalid_argument(
                    "Part-model surface name is already used: " + surfaceName);
            }
            existing->surface = std::move(ruled);
            existing->sourceWireNames = {newWireNames[index], newWireNames[index + 1]};
            existing->sourceWireGroups = {{newWireNames[index]}, {newWireNames[index + 1]}};
        } else {
            surfaces_.push_back(NamedSurface{
                surfaceName,
                std::move(ruled),
                {newWireNames[index], newWireNames[index + 1]},
                model.visible,
                {},
                {{newWireNames[index]}, {newWireNames[index + 1]}},
                model.name,
            });
        }
        newSurfaceNames.push_back(surfaceName);
    }
    for (const std::string& oldName : model.partSurfaceNames) {
        if (std::find(newSurfaceNames.begin(), newSurfaceNames.end(), oldName) != newSurfaceNames.end()) {
            continue;
        }
        for (const NamedPlate& dependentPlate : plates_) {
            if (dependentPlate.sourceSurfaceName == oldName) {
                throw std::invalid_argument(
                    "Part surface is used by plate: " + dependentPlate.name);
            }
        }
        surfaces_.erase(
            std::remove_if(surfaces_.begin(), surfaces_.end(), [&](const NamedSurface& surface) {
                return surface.name == oldName
                    && surface.partModelSourceName.has_value()
                    && *surface.partModelSourceName == model.name;
            }),
            surfaces_.end());
        RemoveObjectFromSets(ProjectObjectKind::Surface, oldName);
    }
    model.partSurfaceNames = newSurfaceNames;

    // --- 元板材の開口(窓・ライト等)を、収まる部材の面へ投影した派生ワイヤ ---
    // 元開口は「下書きワイヤを方向投影した閉ワイヤ」なので、同じ下書き・同じ方向を
    // 部材のルールド面(角ばった近似形状)へ投影し直すと、近似モデル上の穴輪郭になる。
    // 部材境界をまたぐ開口はどの部材にも属せないため作らない(型紙側では表示される)。
    std::vector<std::string> newOpeningNames;
    // 開口の元: 板材入力なら板材の開口、面入力なら面の開口(窓・ライト等)。
    std::vector<std::string> sourceOpeningNames;
    const Surface* openingReferenceSurface = nullptr;
    double openingRangeMinimum = 0.0;
    double openingRangeMaximum = 1.0;
    const bool splitAlongVAxis = model.options.splitAxis == PartSplitAxis::V;
    if (sourcePlate != nullptr) {
        sourceOpeningNames = sourcePlate->openingWireNames;
        openingReferenceSurface = &sourcePlate->plate.SourceSurface();
        const PlateSurfaceRange& plateRange = sourcePlate->plate.Range();
        openingRangeMinimum = splitAlongVAxis ? plateRange.minimumV : plateRange.minimumU;
        openingRangeMaximum = splitAlongVAxis ? plateRange.maximumV : plateRange.maximumU;
    } else {
        const auto sourceSurfaceNamed = std::find_if(surfaces_.begin(), surfaces_.end(),
            [&](const NamedSurface& candidate) {
                return candidate.name == model.sourceSurfaceName;
            });
        if (sourceSurfaceNamed != surfaces_.end()) {
            sourceOpeningNames = sourceSurfaceNamed->openingWireNames;
            openingReferenceSurface = &sourceSurfaceNamed->surface;
        }
    }
    for (std::size_t openingIndex = 0;
         openingIndex < sourceOpeningNames.size(); ++openingIndex) {
        // 注意: このループは wires_ へ push_back するため、参照ではなくコピーで持つ。
        const NamedWire opening = RequireWire(sourceOpeningNames[openingIndex]);
        if (!opening.projection.has_value()) {
            continue;
        }
        // 開口の分割軸方向の範囲(入力ローカル0..1)を測り、収まる部材を探す。
        const bool splitAlongV = splitAlongVAxis;
        const double rangeMinimum = openingRangeMinimum;
        const double rangeMaximum = openingRangeMaximum;
        const double rangeSpan = std::max(1.0e-12, rangeMaximum - rangeMinimum);
        double minimumParameter = 1.0;
        double maximumParameter = 0.0;
        const int samples = 48;
        bool measured = true;
        for (int sample = 0; sample <= samples; ++sample) {
            const geometry::Vector3 point = opening.wire.Evaluate(
                static_cast<double>(sample) / samples);
            SurfaceProjection projected{};
            try {
                projected = openingReferenceSurface->ProjectPointAlongDirection(
                    point, opening.projection->direction);
            } catch (const std::exception&) {
                measured = false;
                break;
            }
            const double surfaceParameter = splitAlongV ? projected.v : projected.u;
            const double localParameter = (surfaceParameter - rangeMinimum) / rangeSpan;
            minimumParameter = std::min(minimumParameter, localParameter);
            maximumParameter = std::max(maximumParameter, localParameter);
        }
        if (!measured) {
            continue;
        }
        constexpr double parameterTolerance = 1.0e-6;
        int ownerIndex = -1;
        for (std::size_t partIndex = 0; partIndex < model.result.parts.size(); ++partIndex) {
            const ApproximatedPart& part = model.result.parts[partIndex];
            if (minimumParameter >= part.minimumParameter - parameterTolerance
                && maximumParameter <= part.maximumParameter + parameterTolerance) {
                ownerIndex = static_cast<int>(partIndex);
                break;
            }
        }
        if (ownerIndex < 0) {
            continue; // 部材境界をまたぐ開口。
        }
        const auto openingSource = std::find_if(wires_.begin(), wires_.end(),
            [&](const NamedWire& wire) {
                return wire.name == opening.projection->sourceWireName;
            });
        if (openingSource == wires_.end()) {
            continue;
        }
        const std::string& targetSurfaceName = newSurfaceNames[ownerIndex];
        const auto targetSurface = std::find_if(surfaces_.begin(), surfaces_.end(),
            [&](const NamedSurface& candidate) { return candidate.name == targetSurfaceName; });
        if (targetSurface == surfaces_.end()) {
            continue;
        }
        Wire projectedOutline = targetSurface->surface.ProjectWireAlongDirection(
            openingSource->wire, opening.projection->direction);
        const std::string derivedName = model.name + "_部材"
            + std::to_string(ownerIndex + 1) + "_穴"
            + std::to_string(openingIndex + 1);
        const auto existing = std::find_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
            return wire.name == derivedName;
        });
        if (existing != wires_.end()) {
            if (!existing->partModelSourceName.has_value()
                || *existing->partModelSourceName != model.name) {
                throw std::invalid_argument(
                    "Part-model opening wire name is already used: " + derivedName);
            }
            existing->wire = std::move(projectedOutline);
            existing->projection = NamedWire::Projection{
                opening.projection->sourceWireName,
                targetSurfaceName,
                opening.projection->direction,
            };
        } else {
            wires_.push_back(NamedWire{
                derivedName,
                std::move(projectedOutline),
                {},
                NamedWire::Projection{
                    opening.projection->sourceWireName,
                    targetSurfaceName,
                    opening.projection->direction,
                },
                model.visible,
                std::nullopt,
                model.name,
            });
        }
        newOpeningNames.push_back(derivedName);
    }
    // --- 部材面への後付け開口(#17b): 記録された部材番号の面へ投影し直す ---
    for (std::size_t extraIndex = 0; extraIndex < model.partOpenings.size(); ++extraIndex) {
        const NamedPartModel::PartOpening record = model.partOpenings[extraIndex];
        if (record.partNumber < 1
            || record.partNumber > static_cast<int>(newSurfaceNames.size())) {
            continue; // 部材数が減って載らない指定は保留(記録は残す)。
        }
        const auto sourceWire = std::find_if(wires_.begin(), wires_.end(),
            [&](const NamedWire& wire) { return wire.name == record.sourceWireName; });
        if (sourceWire == wires_.end()) {
            continue;
        }
        const std::string& targetSurfaceName = newSurfaceNames[record.partNumber - 1];
        const auto targetSurface = std::find_if(surfaces_.begin(), surfaces_.end(),
            [&](const NamedSurface& candidate) { return candidate.name == targetSurfaceName; });
        if (targetSurface == surfaces_.end()) {
            continue;
        }
        const Wire sourceGeometry = sourceWire->wire; // push_backで参照が無効になるため複製
        std::optional<Wire> projectedOutline;
        try {
            projectedOutline = targetSurface->surface.ProjectWireAlongDirection(
                sourceGeometry, record.direction);
        } catch (const std::exception&) {
            continue; // その部材面に載らない場合は保留。
        }
        const std::string derivedName = model.name + "_部材"
            + std::to_string(record.partNumber) + "_穴"
            + std::to_string(sourceOpeningNames.size() + extraIndex + 1);
        const auto existing = std::find_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
            return wire.name == derivedName;
        });
        if (existing != wires_.end()) {
            if (!existing->partModelSourceName.has_value()
                || *existing->partModelSourceName != model.name) {
                throw std::invalid_argument(
                    "Part-model opening wire name is already used: " + derivedName);
            }
            existing->wire = std::move(*projectedOutline);
            existing->projection = NamedWire::Projection{
                record.sourceWireName, targetSurfaceName, record.direction};
        } else {
            wires_.push_back(NamedWire{
                derivedName,
                std::move(*projectedOutline),
                {},
                NamedWire::Projection{
                    record.sourceWireName, targetSurfaceName, record.direction},
                model.visible,
                std::nullopt,
                model.name,
            });
        }
        newOpeningNames.push_back(derivedName);
    }
    for (const std::string& oldName : model.openingWireNames) {
        if (std::find(newOpeningNames.begin(), newOpeningNames.end(), oldName)
            != newOpeningNames.end()) {
            continue;
        }
        // 消える派生開口を穴として使っている板材からは外す(名前の残留参照を防ぐ)。
        for (NamedPlate& dependentPlate : plates_) {
            dependentPlate.openingWireNames.erase(
                std::remove(dependentPlate.openingWireNames.begin(),
                    dependentPlate.openingWireNames.end(), oldName),
                dependentPlate.openingWireNames.end());
        }
        wires_.erase(
            std::remove_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
                return wire.name == oldName
                    && wire.partModelSourceName.has_value()
                    && *wire.partModelSourceName == model.name;
            }),
            wires_.end());
        RemoveObjectFromSets(ProjectObjectKind::Wire, oldName);
    }
    model.openingWireNames = newOpeningNames;

    // --- 接続スコープ(合意13): 近似の実形状へスナップした派生「_接続」を作り直す ---
    std::vector<std::string> newAdaptedWireNames;
    std::vector<std::string> newAdaptedSurfaceNames;
    if (!model.scopeWireNames.empty() || !model.scopeSurfaceNames.empty()) {
        std::vector<double> railParameters;
        railParameters.push_back(0.0);
        for (std::size_t index = 1; index < model.result.parts.size(); ++index) {
            railParameters.push_back(model.result.parts[index].minimumParameter);
        }
        railParameters.push_back(1.0);
        const PartMeshDevelopment adaptMesh = DevelopPartMesh(
            RequirePartModelSource(model), model.options.splitAxis, railParameters, 96);
        const double snapTolerance
            = model.result.maximumDeviationMillimeters + 0.35;
        // ワイヤ(または輪郭)を折れ線としてサンプリングし、近似メッシュに
        // 載っている点だけをメッシュ上へスナップする。
        const auto adaptPolyline = [&](const Wire& wire, int samples) {
            std::vector<geometry::Vector3> points;
            points.reserve(static_cast<std::size_t>(samples) + 1);
            const bool closed = wire.IsClosed();
            const int last = closed ? samples - 1 : samples;
            for (int sample = 0; sample <= last; ++sample) {
                const geometry::Vector3 original
                    = wire.Evaluate(static_cast<double>(sample) / samples);
                const PartMeshMappedPoint mapped = MapPointToPartMeshState(
                    adaptMesh, adaptMesh.world, original);
                points.push_back(mapped.distanceMillimeters <= snapTolerance
                        ? mapped.point
                        : original);
            }
            if (closed) {
                points.push_back(points.front());
            }
            return points;
        };
        const auto placeAdapted = [&](const std::string& adaptedName, Wire adapted) {
            const auto existing = std::find_if(wires_.begin(), wires_.end(),
                [&](const NamedWire& wire) { return wire.name == adaptedName; });
            if (existing != wires_.end()) {
                if (!existing->partModelSourceName.has_value()
                    || *existing->partModelSourceName != model.name) {
                    throw std::invalid_argument(
                        "Connection wire name is already used: " + adaptedName);
                }
                existing->wire = std::move(adapted);
            } else {
                wires_.push_back(NamedWire{adaptedName, std::move(adapted), {},
                    std::nullopt, model.visible, std::nullopt, model.name});
            }
        };
        for (const std::string& scopeName : model.scopeWireNames) {
            const auto scopeWire = std::find_if(wires_.begin(), wires_.end(),
                [&](const NamedWire& wire) { return wire.name == scopeName; });
            if (scopeWire == wires_.end()) {
                throw std::logic_error("Scope wire is missing: " + scopeName);
            }
            const Wire original = scopeWire->wire; // push_backで参照が無効になるため複製
            const std::string adaptedName = scopeName + "_接続";
            placeAdapted(adaptedName, Wire::Polyline(adaptPolyline(original, 96)));
            newAdaptedWireNames.push_back(adaptedName);
        }
        for (const std::string& scopeName : model.scopeSurfaceNames) {
            const auto scopeSurface = std::find_if(surfaces_.begin(), surfaces_.end(),
                [&](const NamedSurface& candidate) { return candidate.name == scopeName; });
            if (scopeSurface == surfaces_.end()) {
                throw std::logic_error("Scope surface is missing: " + scopeName);
            }
            if (scopeSurface->surface.Kind() != SurfaceKind::Planar) {
                continue; // v1では平面のみ自動変形の対象にする。
            }
            const Wire boundary = scopeSurface->surface.FirstBoundary();
            const std::string adaptedWireName = scopeName + "_接続縁";
            const std::string adaptedSurfaceName = scopeName + "_接続";
            Wire adaptedBoundary = Wire::Polyline(adaptPolyline(boundary, 128));
            Surface adaptedSurface = Surface::Planar(
                adaptedBoundary,
                std::max(0.5, model.result.maximumDeviationMillimeters * 3.0));
            placeAdapted(adaptedWireName, std::move(adaptedBoundary));
            newAdaptedWireNames.push_back(adaptedWireName);
            const auto existing = std::find_if(surfaces_.begin(), surfaces_.end(),
                [&](const NamedSurface& candidate) {
                    return candidate.name == adaptedSurfaceName;
                });
            if (existing != surfaces_.end()) {
                if (!existing->partModelSourceName.has_value()
                    || *existing->partModelSourceName != model.name) {
                    throw std::invalid_argument(
                        "Connection surface name is already used: " + adaptedSurfaceName);
                }
                existing->surface = std::move(adaptedSurface);
                existing->sourceWireNames = {adaptedWireName};
                existing->sourceWireGroups = {{adaptedWireName}};
            } else {
                surfaces_.push_back(NamedSurface{
                    adaptedSurfaceName,
                    std::move(adaptedSurface),
                    {adaptedWireName},
                    model.visible,
                    {},
                    {{adaptedWireName}},
                    model.name,
                });
            }
            newAdaptedSurfaceNames.push_back(adaptedSurfaceName);
        }
    }
    // 前回の派生「_接続」のうち今回作られなかったものを片付ける。
    for (const std::string& oldName : model.adaptedSurfaceNames) {
        if (std::find(newAdaptedSurfaceNames.begin(), newAdaptedSurfaceNames.end(), oldName)
            != newAdaptedSurfaceNames.end()) {
            continue;
        }
        surfaces_.erase(
            std::remove_if(surfaces_.begin(), surfaces_.end(), [&](const NamedSurface& surface) {
                return surface.name == oldName
                    && surface.partModelSourceName.has_value()
                    && *surface.partModelSourceName == model.name;
            }),
            surfaces_.end());
        RemoveObjectFromSets(ProjectObjectKind::Surface, oldName);
    }
    for (const std::string& oldName : model.adaptedWireNames) {
        if (std::find(newAdaptedWireNames.begin(), newAdaptedWireNames.end(), oldName)
            != newAdaptedWireNames.end()) {
            continue;
        }
        wires_.erase(
            std::remove_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
                return wire.name == oldName
                    && wire.partModelSourceName.has_value()
                    && *wire.partModelSourceName == model.name;
            }),
            wires_.end());
        RemoveObjectFromSets(ProjectObjectKind::Wire, oldName);
    }
    model.adaptedWireNames = newAdaptedWireNames;
    model.adaptedSurfaceNames = newAdaptedSurfaceNames;

    // 可動折り線: 折り線の本数(部材数-1)が変わったらリセット(全て完成形=1)。
    const std::size_t creaseCount
        = model.result.parts.size() > 0 ? model.result.parts.size() - 1 : 0;
    if (!model.railFoldProgress.empty() && model.railFoldProgress.size() != creaseCount) {
        model.railFoldProgress.clear();
    }
    // 部材オフセット: 部材数が減って載らない記録は外す。
    std::erase_if(model.partOffsets, [&](const NamedPartModel::PartOffset& offset) {
        return offset.partNumber < 1
            || offset.partNumber > static_cast<int>(model.result.parts.size());
    });

    // --- 曲げ状態・部材オフセットの実体反映(オーナー指示) ---
    // 折り線進行度が完成形(全て1)以外、または部材オフセットがあるとき、
    // 部材面・境界レール・部材の穴を「剛体折りの現在姿勢+部材ごとの平行移動」へ写す。
    // 帯そのものは一切変形しない(等長)。この上に作った板材・厚み位置ワイヤは
    // RebuildDependentGeometry の部材面依存の再構築でこの姿勢へ追従する。
    const bool foldActive = std::any_of(
        model.railFoldProgress.begin(), model.railFoldProgress.end(),
        [](double value) { return std::abs(value - 1.0) > 1.0e-9; });
    const bool offsetActive = std::any_of(
        model.partOffsets.begin(), model.partOffsets.end(),
        [](const NamedPartModel::PartOffset& offset) {
            return offset.delta.LengthSquared() > 0.0;
        });
    if ((foldActive || offsetActive) && !model.result.parts.empty()) {
        const PartMeshDevelopment foldMesh = DevelopPartMesh(
            RequirePartModelSource(model), model.options.splitAxis, parameters, 96);
        std::vector<double> creaseProgress(
            foldMesh.rows >= 2 ? static_cast<std::size_t>(foldMesh.rows - 2) : 0, 1.0);
        for (std::size_t index = 0;
             index < creaseProgress.size() && index < model.railFoldProgress.size();
             ++index) {
            creaseProgress[index] = model.railFoldProgress[index];
        }
        const std::vector<PartBandTransform> bandTransforms
            = BuildRigidBandTransforms(foldMesh, creaseProgress);
        const int bandCount = static_cast<int>(bandTransforms.size());
        const auto offsetOf = [&model](int band) {
            for (const NamedPartModel::PartOffset& offset : model.partOffsets) {
                if (offset.partNumber == band + 1) {
                    return offset.delta;
                }
            }
            return geometry::Vector3{0.0, 0.0, 0.0};
        };
        const auto applyBand = [&](int band, const geometry::Vector3& point) {
            const int clamped = std::clamp(band, 0, bandCount - 1);
            return bandTransforms[static_cast<std::size_t>(clamped)].Apply(point)
                + offsetOf(clamped);
        };
        const auto transformedRow = [&](int row, int band) {
            std::vector<geometry::Vector3> points;
            points.reserve(foldMesh.world[static_cast<std::size_t>(row)].size());
            for (const geometry::Vector3& point
                : foldMesh.world[static_cast<std::size_t>(row)]) {
                points.push_back(applyBand(band, point));
            }
            return points;
        };
        // レール j は帯 min(j, 帯数-1) に同座標で追従する(帯どうしの隙間は許容)。
        for (std::size_t railIndex = 0; railIndex < model.boundaryWireNames.size()
             && railIndex < static_cast<std::size_t>(foldMesh.rows); ++railIndex) {
            const auto rail = std::find_if(wires_.begin(), wires_.end(),
                [&](const NamedWire& wire) {
                    return wire.name == model.boundaryWireNames[railIndex];
                });
            if (rail == wires_.end()) {
                continue;
            }
            rail->wire = Wire::Polyline(transformedRow(
                static_cast<int>(railIndex),
                std::min<int>(static_cast<int>(railIndex), bandCount - 1)));
        }
        // 部材面 i は帯 i の変換で両縁とも同じ剛体変換(帯は変形しない)。
        for (std::size_t partIndex = 0; partIndex < model.partSurfaceNames.size()
             && partIndex + 1 < static_cast<std::size_t>(foldMesh.rows); ++partIndex) {
            const auto named = std::find_if(surfaces_.begin(), surfaces_.end(),
                [&](const NamedSurface& candidate) {
                    return candidate.name == model.partSurfaceNames[partIndex];
                });
            if (named == surfaces_.end()) {
                continue;
            }
            const int band = static_cast<int>(partIndex);
            named->surface = Surface::Ruled(
                Wire::Polyline(transformedRow(static_cast<int>(partIndex), band)),
                Wire::Polyline(transformedRow(static_cast<int>(partIndex) + 1, band)));
        }
        // 部材の穴(投影済み)は、属する部材の変換で点単位に写す。
        for (const std::string& openingName : model.openingWireNames) {
            const std::string prefix = model.name + "_部材";
            if (openingName.size() <= prefix.size()
                || openingName.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            int band = 0;
            try {
                band = std::stoi(openingName.substr(prefix.size())) - 1;
            } catch (const std::exception&) {
                continue;
            }
            const auto opening = std::find_if(wires_.begin(), wires_.end(),
                [&](const NamedWire& wire) { return wire.name == openingName; });
            if (opening == wires_.end()) {
                continue;
            }
            constexpr int kOpeningSamples = 96;
            const bool closed = opening->wire.IsClosed();
            const int last = closed ? kOpeningSamples - 1 : kOpeningSamples;
            std::vector<geometry::Vector3> points;
            points.reserve(static_cast<std::size_t>(kOpeningSamples) + 1);
            for (int sample = 0; sample <= last; ++sample) {
                points.push_back(applyBand(band,
                    opening->wire.Evaluate(static_cast<double>(sample) / kOpeningSamples)));
            }
            if (closed) {
                points.push_back(points.front());
            }
            opening->wire = Wire::Polyline(std::move(points));
        }
    }

    // --- 自動セット「近似:<名前>」を最新のメンバーで作り直す ---
    const std::string setName = "近似:" + model.name;
    ObjectSet* set = FindObjectSetMutable(setName);
    if (set == nullptr) {
        objectSets_.push_back({setName, ObjectSetState::Visible, true, true, {}});
        set = &objectSets_.back();
    }
    set->automatic = true;
    set->members.clear();
    set->members.push_back({ProjectObjectKind::PartModel, model.name});
    for (const std::string& wireName : newWireNames) {
        set->members.push_back({ProjectObjectKind::Wire, wireName});
    }
    for (const std::string& wireName : newOpeningNames) {
        set->members.push_back({ProjectObjectKind::Wire, wireName});
    }
    for (const std::string& surfaceName : newSurfaceNames) {
        set->members.push_back({ProjectObjectKind::Surface, surfaceName});
    }
    for (const std::string& wireName : model.adaptedWireNames) {
        set->members.push_back({ProjectObjectKind::Wire, wireName});
    }
    for (const std::string& surfaceName : model.adaptedSurfaceNames) {
        set->members.push_back({ProjectObjectKind::Surface, surfaceName});
    }
}

void Project::AddPartModel(
    std::string name,
    std::string sourcePlateName,
    PartApproximationOptions options)
{
    if (name.empty()) {
        throw std::invalid_argument("Part-model name must not be empty.");
    }
    const auto duplicate = std::find_if(partModels_.begin(), partModels_.end(), [&](const NamedPartModel& model) {
        return model.name == name;
    });
    if (duplicate != partModels_.end()) {
        throw std::invalid_argument("Part-model name already exists: " + name);
    }
    const auto plate = std::find_if(plates_.begin(), plates_.end(), [&](const NamedPlate& candidate) {
        return candidate.name == sourcePlateName;
    });
    if (plate == plates_.end()) {
        throw std::invalid_argument("Part-model source plate is missing: " + sourcePlateName);
    }

    NamedPartModel model;
    model.name = std::move(name);
    model.sourcePlateName = std::move(sourcePlateName);
    model.options = std::move(options);
    model.result = ApproximatePlateParts(plate->plate, model.options);
    RegeneratePartModelDerivedObjects(model);
    partModels_.push_back(std::move(model));
}

void Project::AddPartModelFromSurface(
    std::string name,
    std::string sourceSurfaceName,
    PartApproximationOptions options)
{
    if (name.empty()) {
        throw std::invalid_argument("Part-model name must not be empty.");
    }
    const auto duplicate = std::find_if(partModels_.begin(), partModels_.end(), [&](const NamedPartModel& model) {
        return model.name == name;
    });
    if (duplicate != partModels_.end()) {
        throw std::invalid_argument("Part-model name already exists: " + name);
    }
    const auto surface = std::find_if(surfaces_.begin(), surfaces_.end(),
        [&](const NamedSurface& candidate) {
            return candidate.name == sourceSurfaceName;
        });
    if (surface == surfaces_.end()) {
        throw std::invalid_argument(
            "Part-model source surface is missing: " + sourceSurfaceName);
    }
    if (surface->partModelSourceName.has_value()) {
        throw std::invalid_argument(
            "Part-model derived surfaces cannot be approximated again: " + sourceSurfaceName);
    }

    NamedPartModel model;
    model.name = std::move(name);
    model.sourceSurfaceName = std::move(sourceSurfaceName);
    model.options = std::move(options);
    model.result = ApproximatePlateParts(PartSource(surface->surface), model.options);
    RegeneratePartModelDerivedObjects(model);
    partModels_.push_back(std::move(model));
}

void Project::UpdatePartModelOptions(std::string_view name, PartApproximationOptions options)
{
    const auto model = std::find_if(partModels_.begin(), partModels_.end(), [&](const NamedPartModel& candidate) {
        return candidate.name == name;
    });
    if (model == partModels_.end()) {
        throw std::invalid_argument("Part model is missing: " + std::string(name));
    }
    const PartApproximationResult result
        = ApproximatePlateParts(RequirePartModelSource(*model), options);
    model->options = std::move(options);
    model->result = result;
    RegeneratePartModelDerivedObjects(*model);
}

void Project::SetPartModelRailFoldProgress(
    std::string_view name, std::vector<double> progress)
{
    const auto model = std::find_if(partModels_.begin(), partModels_.end(),
        [&](const NamedPartModel& candidate) {
            return candidate.name == name;
        });
    if (model == partModels_.end()) {
        throw std::invalid_argument("Part model is missing: " + std::string(name));
    }
    const std::size_t creaseCount
        = model->result.parts.size() > 0 ? model->result.parts.size() - 1 : 0;
    if (!progress.empty() && progress.size() != creaseCount) {
        throw std::invalid_argument("折り線の進行度の数が折り線の本数と一致していません。");
    }
    for (const double value : progress) {
        if (!std::isfinite(value) || value < -4.0 || value > 4.0) {
            throw std::invalid_argument("折り線の進行度は -4〜4 の範囲で指定してください。");
        }
    }
    model->railFoldProgress = std::move(progress);
    // 実際の部材面・レール・穴と、その上の板材等をこの姿勢へ作り直す(オーナー指示)。
    RebuildDependentGeometry();
}

void Project::SetPartModelPartOffset(
    std::string_view name, int partNumber, const geometry::Vector3& delta)
{
    const auto model = std::find_if(partModels_.begin(), partModels_.end(),
        [&](const NamedPartModel& candidate) {
            return candidate.name == name;
        });
    if (model == partModels_.end()) {
        throw std::invalid_argument("Part model is missing: " + std::string(name));
    }
    if (partNumber < 1 || partNumber > static_cast<int>(model->result.parts.size())) {
        throw std::invalid_argument("部材番号が範囲外です。");
    }
    if (!delta.IsFinite()) {
        throw std::invalid_argument("部材オフセットの値が不正です。");
    }
    std::erase_if(model->partOffsets, [&](const NamedPartModel::PartOffset& offset) {
        return offset.partNumber == partNumber;
    });
    if (delta.LengthSquared() > 0.0) {
        model->partOffsets.push_back(NamedPartModel::PartOffset{partNumber, delta});
    }
    RebuildDependentGeometry();
}

void Project::SetPartModelConnectionScope(
    std::string_view name,
    std::vector<std::string> wireNames,
    std::vector<std::string> surfaceNames)
{
    const auto model = std::find_if(partModels_.begin(), partModels_.end(),
        [&](const NamedPartModel& candidate) {
            return candidate.name == name;
        });
    if (model == partModels_.end()) {
        throw std::invalid_argument("Part model is missing: " + std::string(name));
    }
    for (const std::string& wireName : wireNames) {
        const auto wire = std::find_if(wires_.begin(), wires_.end(),
            [&](const NamedWire& candidate) { return candidate.name == wireName; });
        if (wire == wires_.end()) {
            throw std::invalid_argument("Scope wire does not exist: " + wireName);
        }
        if (wire->partModelSourceName.has_value()) {
            throw std::invalid_argument(
                "Derived wires cannot join a connection scope: " + wireName);
        }
    }
    for (const std::string& surfaceName : surfaceNames) {
        const auto surface = std::find_if(surfaces_.begin(), surfaces_.end(),
            [&](const NamedSurface& candidate) { return candidate.name == surfaceName; });
        if (surface == surfaces_.end()) {
            throw std::invalid_argument("Scope surface does not exist: " + surfaceName);
        }
        if (surface->partModelSourceName.has_value()) {
            throw std::invalid_argument(
                "Derived surfaces cannot join a connection scope: " + surfaceName);
        }
        if (surfaceName == model->sourceSurfaceName) {
            throw std::invalid_argument(
                "Approximation source cannot join its own connection scope: " + surfaceName);
        }
    }
    model->scopeWireNames = std::move(wireNames);
    model->scopeSurfaceNames = std::move(surfaceNames);
    RegeneratePartModelDerivedObjects(*model);
}

void Project::AddPartModelOpening(
    std::string_view name,
    int partNumber,
    std::string sourceWireName,
    geometry::Vector3 direction)
{
    const auto model = std::find_if(partModels_.begin(), partModels_.end(),
        [&](const NamedPartModel& candidate) { return candidate.name == name; });
    if (model == partModels_.end()) {
        throw std::invalid_argument("Part-model name does not exist: " + std::string(name));
    }
    if (partNumber < 1 || partNumber > static_cast<int>(model->result.parts.size())) {
        throw std::invalid_argument("Part number is out of range.");
    }
    if (!direction.IsFinite() || direction.LengthSquared() <= 1.0e-18) {
        throw std::invalid_argument("Opening projection direction must be valid.");
    }
    const NamedWire& source = RequireWire(sourceWireName);
    if (source.partModelSourceName.has_value()) {
        throw std::invalid_argument(
            "Part-model derived wires cannot be an opening source: " + sourceWireName);
    }
    if (source.metadata.construction) {
        throw std::invalid_argument(
            "Construction wire cannot be an opening source: " + sourceWireName);
    }
    if (!source.wire.IsClosed()) {
        throw std::invalid_argument("Opening source wire must be closed: " + sourceWireName);
    }
    for (const NamedPartModel::PartOpening& record : model->partOpenings) {
        if (record.sourceWireName == sourceWireName && record.partNumber == partNumber) {
            throw std::invalid_argument(
                "Wire is already an opening of this part: " + sourceWireName);
        }
    }
    model->partOpenings.push_back({partNumber, std::move(sourceWireName), direction});
    RegeneratePartModelDerivedObjects(*model);
}

void Project::RemovePartModelOpening(std::string_view name, std::string_view sourceWireName)
{
    const auto model = std::find_if(partModels_.begin(), partModels_.end(),
        [&](const NamedPartModel& candidate) { return candidate.name == name; });
    if (model == partModels_.end()) {
        throw std::invalid_argument("Part-model name does not exist: " + std::string(name));
    }
    const std::size_t before = model->partOpenings.size();
    std::erase_if(model->partOpenings, [&](const NamedPartModel::PartOpening& record) {
        return record.sourceWireName == sourceWireName;
    });
    if (model->partOpenings.size() == before) {
        throw std::invalid_argument(
            "Wire is not a part opening: " + std::string(sourceWireName));
    }
    RegeneratePartModelDerivedObjects(*model);
}

bool Project::RemovePartModel(std::string_view name)
{
    const auto model = std::find_if(partModels_.begin(), partModels_.end(), [&](const NamedPartModel& candidate) {
        return candidate.name == name;
    });
    if (model == partModels_.end()) {
        return false;
    }
    for (const std::string& surfaceName : model->partSurfaceNames) {
        for (const NamedPlate& dependentPlate : plates_) {
            if (dependentPlate.sourceSurfaceName == surfaceName) {
                throw std::invalid_argument(
                    "Part surface is used by plate (remove the plate first): "
                    + dependentPlate.name);
            }
        }
        for (const NamedBody& dependentBody : bodies_) {
            if (dependentBody.sourceSurfaceName == surfaceName) {
                throw std::invalid_argument(
                    "Part surface is used by body (remove the body first): "
                    + dependentBody.name);
            }
        }
    }
    for (const std::string& surfaceName : model->partSurfaceNames) {
        surfaces_.erase(
            std::remove_if(surfaces_.begin(), surfaces_.end(), [&](const NamedSurface& surface) {
                return surface.name == surfaceName && surface.partModelSourceName.has_value();
            }),
            surfaces_.end());
    }
    for (const std::string& wireName : model->boundaryWireNames) {
        wires_.erase(
            std::remove_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
                return wire.name == wireName && wire.partModelSourceName.has_value();
            }),
            wires_.end());
    }
    for (const std::string& wireName : model->openingWireNames) {
        for (NamedPlate& dependentPlate : plates_) {
            dependentPlate.openingWireNames.erase(
                std::remove(dependentPlate.openingWireNames.begin(),
                    dependentPlate.openingWireNames.end(), wireName),
                dependentPlate.openingWireNames.end());
        }
        wires_.erase(
            std::remove_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
                return wire.name == wireName && wire.partModelSourceName.has_value();
            }),
            wires_.end());
        RemoveObjectFromSets(ProjectObjectKind::Wire, wireName);
    }
    for (const std::string& surfaceName : model->adaptedSurfaceNames) {
        surfaces_.erase(
            std::remove_if(surfaces_.begin(), surfaces_.end(), [&](const NamedSurface& surface) {
                return surface.name == surfaceName && surface.partModelSourceName.has_value();
            }),
            surfaces_.end());
        RemoveObjectFromSets(ProjectObjectKind::Surface, surfaceName);
    }
    for (const std::string& wireName : model->adaptedWireNames) {
        wires_.erase(
            std::remove_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
                return wire.name == wireName && wire.partModelSourceName.has_value();
            }),
            wires_.end());
        RemoveObjectFromSets(ProjectObjectKind::Wire, wireName);
    }
    const std::string setName = "近似:" + model->name;
    objectSets_.erase(
        std::remove_if(objectSets_.begin(), objectSets_.end(), [&](const ObjectSet& set) {
            return set.name == setName;
        }),
        objectSets_.end());
    partModels_.erase(model);
    return true;
}

void Project::SetPartModelVisible(std::string_view name, bool visible)
{
    const auto model = std::find_if(partModels_.begin(), partModels_.end(), [&](NamedPartModel& candidate) {
        return candidate.name == name;
    });
    if (model == partModels_.end()) {
        throw std::invalid_argument("Part model is missing: " + std::string(name));
    }
    model->visible = visible;
    for (const std::string& wireName : model->adaptedWireNames) {
        for (NamedWire& wire : wires_) {
            if (wire.name == wireName) {
                wire.visible = visible;
            }
        }
    }
    for (const std::string& surfaceName : model->adaptedSurfaceNames) {
        for (NamedSurface& surface : surfaces_) {
            if (surface.name == surfaceName) {
                surface.visible = visible;
            }
        }
    }
    for (const std::string& wireName : model->boundaryWireNames) {
        for (NamedWire& wire : wires_) {
            if (wire.name == wireName) {
                wire.visible = visible;
            }
        }
    }
    for (const std::string& wireName : model->openingWireNames) {
        for (NamedWire& wire : wires_) {
            if (wire.name == wireName) {
                wire.visible = visible;
            }
        }
    }
    for (const std::string& surfaceName : model->partSurfaceNames) {
        for (NamedSurface& surface : surfaces_) {
            if (surface.name == surfaceName) {
                surface.visible = visible;
            }
        }
    }
}

std::vector<std::string> Project::ExtractPartModelBoundaries(std::string_view name)
{
    const auto model = std::find_if(partModels_.begin(), partModels_.end(), [&](const NamedPartModel& candidate) {
        return candidate.name == name;
    });
    if (model == partModels_.end()) {
        throw std::invalid_argument("Part model is missing: " + std::string(name));
    }

    const std::string setName = "抽出:" + model->name;
    if (FindObjectSetMutable(setName) == nullptr) {
        objectSets_.push_back({setName, ObjectSetState::Visible, false, true, {}});
    }

    std::vector<std::string> created;
    for (const std::string& wireName : model->boundaryWireNames) {
        const auto source = std::find_if(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
            return wire.name == wireName;
        });
        if (source == wires_.end()) {
            continue;
        }
        std::string copyName;
        for (int suffix = 1;; ++suffix) {
            copyName = model->name + "_抽出" + std::to_string(created.size() + 1)
                + (suffix == 1 ? std::string() : "_" + std::to_string(suffix));
            const bool taken = std::any_of(wires_.begin(), wires_.end(), [&](const NamedWire& wire) {
                return wire.name == copyName;
            });
            if (!taken) {
                break;
            }
        }
        NamedWire copy{copyName, source->wire, {}, std::nullopt, true, std::nullopt, std::nullopt};
        wires_.push_back(std::move(copy));
        AssignObjectToSet(ProjectObjectKind::Wire, copyName, setName);
        created.push_back(copyName);
    }
    return created;
}

// ---- セット(グループ) ------------------------------------------------

ObjectSet* Project::FindObjectSetMutable(std::string_view name)
{
    for (ObjectSet& set : objectSets_) {
        if (set.name == name) {
            return &set;
        }
    }
    return nullptr;
}

void Project::CreateObjectSet(std::string name, ObjectSetState state)
{
    if (name.empty()) {
        throw std::invalid_argument("Set name must not be empty.");
    }
    if (FindObjectSetMutable(name) != nullptr) {
        throw std::invalid_argument("Set name already exists: " + name);
    }
    objectSets_.push_back({std::move(name), state, false, true, {}});
}

bool Project::RemoveObjectSet(std::string_view name)
{
    const auto set = std::find_if(objectSets_.begin(), objectSets_.end(), [&](const ObjectSet& candidate) {
        return candidate.name == name;
    });
    if (set == objectSets_.end()) {
        return false;
    }
    if (set->automatic) {
        throw std::invalid_argument(
            "Automatic sets are removed with their part model: " + std::string(name));
    }
    const std::string removedParent = set->parentName;
    objectSets_.erase(set);
    // 子グループは削除したグループの親へ付け替える(孤児にしない)。
    for (ObjectSet& candidate : objectSets_) {
        if (candidate.parentName == name) {
            candidate.parentName = removedParent;
        }
    }
    return true;
}

void Project::SetObjectSetState(std::string_view name, ObjectSetState state)
{
    ObjectSet* set = FindObjectSetMutable(name);
    if (set == nullptr) {
        throw std::invalid_argument("Set is missing: " + std::string(name));
    }
    set->state = state;
}

void Project::SetObjectSetParent(std::string_view child, std::string_view parent)
{
    ObjectSet* childSet = FindObjectSetMutable(child);
    if (childSet == nullptr) {
        throw std::invalid_argument("Set is missing: " + std::string(child));
    }
    if (parent.empty()) {
        childSet->parentName.clear();
        return;
    }
    if (child == parent) {
        throw std::invalid_argument("グループを自分自身の中へは移動できません。");
    }
    if (FindObjectSetMutable(parent) == nullptr) {
        throw std::invalid_argument("Set is missing: " + std::string(parent));
    }
    std::string current(parent);
    int guard = 0;
    while (!current.empty() && guard++ < 1024) {
        if (current == child) {
            throw std::invalid_argument("グループを自分の子グループの中へは移動できません。");
        }
        const ObjectSet* ancestor = FindObjectSetMutable(current);
        if (ancestor == nullptr) {
            break;
        }
        current = ancestor->parentName;
    }
    childSet->parentName = parent;
}

void Project::SetObjectSetExport(std::string_view name, bool enabled)
{
    ObjectSet* set = FindObjectSetMutable(name);
    if (set == nullptr) {
        throw std::invalid_argument("Set is missing: " + std::string(name));
    }
    set->exportEnabled = enabled;
}

void Project::AssignObjectToSet(
    ProjectObjectKind kind, std::string objectName, std::string_view setName)
{
    ObjectSet* set = FindObjectSetMutable(setName);
    if (set == nullptr) {
        throw std::invalid_argument("Set is missing: " + std::string(setName));
    }
    RemoveObjectFromSets(kind, objectName);
    set->members.push_back({kind, std::move(objectName)});
}

void Project::RemoveObjectFromSets(ProjectObjectKind kind, std::string_view objectName)
{
    for (ObjectSet& set : objectSets_) {
        set.members.erase(
            std::remove_if(set.members.begin(), set.members.end(), [&](const ObjectSetMember& member) {
                return member.kind == kind && member.name == objectName;
            }),
            set.members.end());
    }
}

ObjectSetState Project::ObjectStateInSets(
    ProjectObjectKind kind, std::string_view objectName) const
{
    for (const ObjectSet& set : objectSets_) {
        for (const ObjectSetMember& member : set.members) {
            if (member.kind != kind || member.name != objectName) {
                continue;
            }
            // 祖先グループの状態を合成する(非表示 > 参照のみ > 表示)。
            ObjectSetState combined = set.state;
            std::string current = set.parentName;
            int guard = 0;
            while (!current.empty() && guard++ < 1024) {
                const auto ancestor = std::find_if(
                    objectSets_.begin(), objectSets_.end(),
                    [&](const ObjectSet& candidate) { return candidate.name == current; });
                if (ancestor == objectSets_.end()) {
                    break;
                }
                if (ancestor->state == ObjectSetState::Hidden) {
                    return ObjectSetState::Hidden;
                }
                if (ancestor->state == ObjectSetState::ReferenceOnly
                    && combined == ObjectSetState::Visible) {
                    combined = ObjectSetState::ReferenceOnly;
                }
                current = ancestor->parentName;
            }
            return combined;
        }
    }
    return ObjectSetState::Visible;
}

} // namespace kachakacha::model
