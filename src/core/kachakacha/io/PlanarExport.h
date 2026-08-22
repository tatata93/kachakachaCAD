#pragma once

#include "kachakacha/model/Project.h"

#include <iosfwd>
#include <vector>

namespace kachakacha::io {

struct PlanarExportOptions {
    double chordToleranceMillimeters = 0.01;
    double marginMillimeters = 5.0;
    double planeToleranceMillimeters = 1.0e-6;
};

[[nodiscard]] bool WireLiesOnWorkPlane(
    const model::Wire& wire,
    const model::WorkPlane& plane,
    double toleranceMillimeters = 1.0e-6);

void WritePlanarSvg(
    std::ostream& output,
    const model::WorkPlane& plane,
    const std::vector<model::NamedWire>& wires,
    PlanarExportOptions options = {});

void WritePlanarDxf(
    std::ostream& output,
    const model::WorkPlane& plane,
    const std::vector<model::NamedWire>& wires,
    PlanarExportOptions options = {});

} // namespace kachakacha::io
