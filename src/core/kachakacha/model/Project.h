#pragma once

#include "kachakacha/model/Surface.h"
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
};

struct NamedSurface {
    std::string name;
    Surface surface;
    std::vector<std::string> sourceWireNames;
};

class Project {
public:
    void AddWorkPlane(std::string name, WorkPlane plane);
    void AddWire(std::string name, Wire wire, WireMetadata metadata = {});
    void AddPlanarSurface(std::string name, std::string boundaryWireName);
    void AddRuledSurface(std::string name, std::string firstSectionName, std::string secondSectionName);
    void AddProjectedWire(
        std::string name,
        std::string sourceWireName,
        std::string targetSurfaceName,
        geometry::Vector3 direction);
    void UpdateWorkPlane(std::string_view name, WorkPlane plane);
    void UpdateWire(std::string_view name, Wire wire);
    void SetWireMetadata(std::string_view name, WireMetadata metadata);
    bool RemoveWorkPlane(std::string_view name);
    bool RemoveWire(std::string_view name);
    bool RemoveSurface(std::string_view name);

    [[nodiscard]] const std::vector<NamedWorkPlane>& WorkPlanes() const noexcept { return workPlanes_; }
    [[nodiscard]] const std::vector<NamedWire>& Wires() const noexcept { return wires_; }
    [[nodiscard]] const std::vector<NamedSurface>& Surfaces() const noexcept { return surfaces_; }

    [[nodiscard]] std::optional<WorkPlane> FindWorkPlane(std::string_view name) const;
    [[nodiscard]] std::optional<Surface> FindSurface(std::string_view name) const;

private:
    [[nodiscard]] const NamedWire& RequireWire(std::string_view name) const;
    void RebuildDependentGeometry();

    std::vector<NamedWorkPlane> workPlanes_;
    std::vector<NamedWire> wires_;
    std::vector<NamedSurface> surfaces_;
};

} // namespace kachakacha::model
