#pragma once

#include "kachakacha/model/Plate.h"
#include "kachakacha/model/Wire.h"
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

struct WireMetadata {
    std::optional<std::string> sourcePlaneName;
    WirePlanePolicy planePolicy = WirePlanePolicy::Free3D;
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
    void UpdatePlate(
        std::string_view name,
        std::string sourceSurfaceName,
        double thickness,
        PlateThicknessDirection direction,
        std::string material);
    void SetWireMetadata(std::string_view name, WireMetadata metadata);
    void SetWorkPlaneVisible(std::string_view name, bool visible);
    void SetWireVisible(std::string_view name, bool visible);
    void SetSurfaceVisible(std::string_view name, bool visible);
    void SetPlateVisible(std::string_view name, bool visible);
    bool RemoveWorkPlane(std::string_view name);
    bool RemoveWire(std::string_view name);
    bool RemoveSurface(std::string_view name);
    bool RemovePlate(std::string_view name);

    [[nodiscard]] const std::vector<NamedWorkPlane>& WorkPlanes() const noexcept { return workPlanes_; }
    [[nodiscard]] const std::vector<NamedWire>& Wires() const noexcept { return wires_; }
    [[nodiscard]] const std::vector<NamedSurface>& Surfaces() const noexcept { return surfaces_; }
    [[nodiscard]] const std::vector<NamedPlate>& Plates() const noexcept { return plates_; }

    [[nodiscard]] std::optional<WorkPlane> FindWorkPlane(std::string_view name) const;
    [[nodiscard]] std::optional<Surface> FindSurface(std::string_view name) const;
    [[nodiscard]] std::optional<Plate> FindPlate(std::string_view name) const;

private:
    [[nodiscard]] const NamedWire& RequireWire(std::string_view name) const;
    void RebuildDependentGeometry();

    std::vector<NamedWorkPlane> workPlanes_;
    std::vector<NamedWire> wires_;
    std::vector<NamedSurface> surfaces_;
    std::vector<NamedPlate> plates_;
};

} // namespace kachakacha::model
