#pragma once
#include "kachakacha/kernel/OcctExtrude.h"
#include "kachakacha/geometry/CurveSampling.h"

inline std::vector<std::vector<kachakacha::v2::geometry::Vector3>> ExtrudeWirePreview(
    const kachakacha::v2::kernel::ExtrudeBuildResult& built, bool start, bool end, bool sides)
{
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> lines;
    const auto append = [&](const auto& wires) {
        for (const auto& wire : wires)
            lines.push_back(kachakacha::v2::geometry::SampleChain(wire, .01));
    };
    if (start) append(built.startProfileWires);
    if (end) append(built.endProfileWires);
    if (sides) append(built.sideBoundaryWires);
    return lines;
}
