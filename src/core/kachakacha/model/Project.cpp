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
    if (first.plateOffset.has_value() || second.plateOffset.has_value()) {
        throw std::invalid_argument("Plate-offset wire cannot be used as a ruled surface section.");
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
    plates_.push_back({
        std::move(name),
        Plate(*surface, startThickness, endThickness, direction),
        std::move(sourceSurfaceName),
        std::move(material),
        {},
    });
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
            plate.plate = Plate(
                *sourceSurface, startThickness, endThickness, direction, plate.plate.Range());
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
        plate.plate = Plate(
            plate.plate.SourceSurface(),
            plate.plate.Thickness(),
            plate.plate.EndThickness(),
            plate.plate.Direction(),
            range);
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
    const auto position = std::find_if(plates_.begin(), plates_.end(), [&](const NamedPlate& plate) {
        return plate.name == name;
    });
    if (position == plates_.end()) {
        throw std::invalid_argument("Plate name does not exist: " + std::string(name));
    }
    if (position->plate.SourceSurface().Kind() == SurfaceKind::Planar) {
        throw std::invalid_argument("Planar plates can already be cut directly and do not need surface splitting.");
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
        std::move(firstReliefCuts),
    };
    NamedPlate secondNamedPlate{
        std::move(secondName),
        secondPlate,
        sourceSurfaceName,
        material,
        std::move(secondOpenings),
        visible,
        std::move(secondReliefCuts),
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
    if (std::find(plate->reliefCutWireNames.begin(), plate->reliefCutWireNames.end(), wireName)
        != plate->reliefCutWireNames.end()) {
        throw std::invalid_argument("Plate relief cut cannot also be an opening: " + wireName);
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
    for (const NamedBody& body : bodies_) {
        if (body.sourceSurfaceName == name) {
            throw std::invalid_argument("Surface is used by body: " + body.name);
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

    while (pendingSurfaces > 0 || pendingProjections > 0) {
        bool madeProgress = false;

        for (std::size_t index = 0; index < surfaces_.size(); ++index) {
            if (surfaceReady[index]) {
                continue;
            }
            NamedSurface& surface = surfaces_[index];
            const bool sourcesReady = std::all_of(
                surface.sourceWireNames.begin(), surface.sourceWireNames.end(),
                [&](const std::string& sourceName) { return wireReady[wireIndex(sourceName)]; });
            if (!sourcesReady) {
                continue;
            }

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
            plate.plate.Range());
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
    }
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

} // namespace kachakacha::model
