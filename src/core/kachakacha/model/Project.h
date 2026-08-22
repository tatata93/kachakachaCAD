#pragma once

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
};

class Project {
public:
    void AddWorkPlane(std::string name, WorkPlane plane);
    void AddWire(std::string name, Wire wire, WireMetadata metadata = {});
    void SetWireMetadata(std::string_view name, WireMetadata metadata);
    bool RemoveWorkPlane(std::string_view name);
    bool RemoveWire(std::string_view name);

    [[nodiscard]] const std::vector<NamedWorkPlane>& WorkPlanes() const noexcept { return workPlanes_; }
    [[nodiscard]] const std::vector<NamedWire>& Wires() const noexcept { return wires_; }

    [[nodiscard]] std::optional<WorkPlane> FindWorkPlane(std::string_view name) const;

private:
    std::vector<NamedWorkPlane> workPlanes_;
    std::vector<NamedWire> wires_;
};

} // namespace kachakacha::model
