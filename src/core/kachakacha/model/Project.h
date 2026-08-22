#pragma once

#include "kachakacha/model/Plate.h"
#include "kachakacha/model/Wire.h"
#include "kachakacha/model/WireConstraints.h"
#include "kachakacha/model/WorkPlane.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::model {

enum class WirePlanePolicy {
    Free3D,
    ReferenceOnly,
    LockedToPlane,
};

enum class WireEndpoint {
    Start,
    End,
};

struct WireEndpointReference {
    std::string wireName;
    WireEndpoint endpoint = WireEndpoint::Start;
};

struct WireCoincidentConstraint {
    WireEndpointReference anchor;
    WireEndpointReference follower;
};

enum class WireContinuity {
    G1Tangent,
    G2Curvature,
};

struct WireTangentConstraint {
    WireEndpointReference anchor;
    WireEndpointReference follower;
    WireContinuity continuity = WireContinuity::G1Tangent;
};

enum class ReferenceDimensionKind {
    PointDistance,
    WireLength,
    WireRadius,
    WireDistance,
    WireAngle,
    PointWireDistance,
    PointPlaneDistance,
    WirePlaneAngle,
    PlaneAngle,
    PlaneDistance,
};

enum class DimensionReferenceKind {
    None,
    FixedPoint,
    Wire,
    WorkPlane,
};

struct DimensionReference {
    DimensionReferenceKind kind = DimensionReferenceKind::None;
    std::string objectName;
    geometry::Vector3 point;
    double wireParameter = 0.0;
};

struct ReferenceDimension {
    std::string name;
    ReferenceDimensionKind kind = ReferenceDimensionKind::PointDistance;
    DimensionReference first;
    DimensionReference second;
    bool visible = true;
};

struct ReferenceDimensionResult {
    geometry::Vector3 firstPoint;
    geometry::Vector3 secondPoint;
    double value = 0.0;
};

struct WireMetadata {
    std::optional<std::string> sourcePlaneName;
    WirePlanePolicy planePolicy = WirePlanePolicy::Free3D;
    WireLineConstraints lineConstraints;
    WireCurveConstraints curveConstraints;
    bool construction = false;
};

struct NamedWorkPlane {
    std::string name;
    WorkPlane plane;
    bool visible = true;
};

struct NamedWire {
    std::string name;
    Wire wire;
    WireMetadata metadata;
    struct Projection {
        std::string sourceWireName;
        std::string targetSurfaceName;
        geometry::Vector3 direction;
    };
    std::optional<Projection> projection;
    bool visible = true;
};

struct NamedSurface {
    std::string name;
    Surface surface;
    std::vector<std::string> sourceWireNames;
    bool visible = true;
};

struct NamedPlate {
    std::string name;
    Plate plate;
    std::string sourceSurfaceName;
    std::string material;
    std::vector<std::string> openingWireNames;
    bool visible = true;
};

class Project {
public:
    void AddWorkPlane(std::string name, WorkPlane plane);
    void AddWire(std::string name, Wire wire, WireMetadata metadata = {});
    void AddPlanarSurface(std::string name, std::string boundaryWireName);
    void AddRuledSurface(std::string name, std::string firstSectionName, std::string secondSectionName);
    void AddLoftSurface(std::string name, std::vector<std::string> sectionNames);
    void AddPlate(
        std::string name,
        std::string sourceSurfaceName,
        double thickness,
        PlateThicknessDirection direction,
        std::string material);
    void AddProjectedWire(
        std::string name,
        std::string sourceWireName,
        std::string targetSurfaceName,
        geometry::Vector3 direction);
    void UpdateWorkPlane(std::string_view name, WorkPlane plane);
    void UpdateWire(std::string_view name, Wire wire);
    void UpdateWireAndMetadata(std::string_view name, Wire wire, WireMetadata metadata);
    void UpdatePlate(
        std::string_view name,
        std::string sourceSurfaceName,
        double thickness,
        PlateThicknessDirection direction,
        std::string material);
    void SetWireMetadata(std::string_view name, WireMetadata metadata);
    void AddWireCoincidentConstraint(
        WireEndpointReference anchor,
        WireEndpointReference follower);
    [[nodiscard]] std::size_t RemoveWireCoincidentConstraints(std::string_view wireName);
    void AddWireTangentConstraint(
        WireEndpointReference anchor,
        WireEndpointReference follower,
        WireContinuity continuity = WireContinuity::G1Tangent);
    [[nodiscard]] std::size_t RemoveWireTangentConstraints(std::string_view wireName);
    void AddReferenceDimension(ReferenceDimension dimension);
    [[nodiscard]] bool RemoveReferenceDimension(std::string_view name);
    void SetReferenceDimensionVisible(std::string_view name, bool visible);
    [[nodiscard]] ReferenceDimensionResult EvaluateReferenceDimension(
        std::string_view name) const;
    void SetWorkPlaneVisible(std::string_view name, bool visible);
    void SetWireVisible(std::string_view name, bool visible);
    void SetSurfaceVisible(std::string_view name, bool visible);
    void SetPlateVisible(std::string_view name, bool visible);
    void SetPlateRange(std::string_view name, PlateSurfaceRange range);
    void SplitPlate(
        std::string_view name,
        PlateSplitAxis axis,
        double parameter,
        std::string firstName,
        std::string secondName);
    void AddPlateOpening(std::string_view plateName, std::string wireName);
    void RemovePlateOpening(std::string_view plateName, std::string_view wireName);
    bool RemoveWorkPlane(std::string_view name);
    bool RemoveWire(std::string_view name);
    bool RemoveSurface(std::string_view name);
    bool RemovePlate(std::string_view name);

    [[nodiscard]] const std::vector<NamedWorkPlane>& WorkPlanes() const noexcept { return workPlanes_; }
    [[nodiscard]] const std::vector<NamedWire>& Wires() const noexcept { return wires_; }
    [[nodiscard]] const std::vector<NamedSurface>& Surfaces() const noexcept { return surfaces_; }
    [[nodiscard]] const std::vector<NamedPlate>& Plates() const noexcept { return plates_; }
    [[nodiscard]] const std::vector<WireCoincidentConstraint>& CoincidentConstraints() const noexcept
    {
        return coincidentConstraints_;
    }
    [[nodiscard]] const std::vector<WireTangentConstraint>& TangentConstraints() const noexcept
    {
        return tangentConstraints_;
    }
    [[nodiscard]] const std::vector<ReferenceDimension>& ReferenceDimensions() const noexcept
    {
        return referenceDimensions_;
    }

    [[nodiscard]] std::optional<WorkPlane> FindWorkPlane(std::string_view name) const;
    [[nodiscard]] std::optional<Surface> FindSurface(std::string_view name) const;
    [[nodiscard]] std::optional<Plate> FindPlate(std::string_view name) const;

private:
    [[nodiscard]] const NamedWire& RequireWire(std::string_view name) const;
    void ApplyCoincidentConstraints();
    void ApplyTangentConstraints();
    void RebuildDependentGeometry();

    std::vector<NamedWorkPlane> workPlanes_;
    std::vector<NamedWire> wires_;
    std::vector<NamedSurface> surfaces_;
    std::vector<NamedPlate> plates_;
    std::vector<WireCoincidentConstraint> coincidentConstraints_;
    std::vector<WireTangentConstraint> tangentConstraints_;
    std::vector<ReferenceDimension> referenceDimensions_;
};

} // namespace kachakacha::model
