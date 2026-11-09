#pragma once
#include "V2GeneratedCurves.h"
#include "kachakacha/kernel/OcctOutput.h"

// Editable output only: native edge indices remain untouched for subshape picks.
inline auto V2GeneratedShapeBoundaries(kachakacha::v2::modeling::KernelShapeHandle shape,
    double modelTolerance) {
    auto result=kachakacha::v2::kernel::OutputBoundaryWires(shape,modelTolerance);
    if(!result.HasValue())return result;
    return decltype(result)::Success(V2GeneratedBoundaryRuns(result.Value(),modelTolerance),result.Diagnostics());
}
