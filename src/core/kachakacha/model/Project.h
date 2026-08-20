#pragma once

#include "kachakacha/model/Wire.h"
#include "kachakacha/model/WorkPlane.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::model {

struct NamedWorkPlane {
    std::string name;
    WorkPlane plane;
};

struct NamedWire {
    std::string name;
    Wire wire;
};

class Project {
public:
    void AddWorkPlane(std::string name, WorkPlane plane);
    void AddWire(std::string name, Wire wire);

    [[nodiscard]] const std::vector<NamedWorkPlane>& WorkPlanes() const noexcept { return workPlanes_; }
    [[nodiscard]] const std::vector<NamedWire>& Wires() const noexcept { return wires_; }

    [[nodiscard]] std::optional<WorkPlane> FindWorkPlane(std::string_view name) const;

private:
    std::vector<NamedWorkPlane> workPlanes_;
    std::vector<NamedWire> wires_;
};

} // namespace kachakacha::model

