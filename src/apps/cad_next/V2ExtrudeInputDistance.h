#pragma once

#include "V2ExtrudeDock.h"
#include "V2ParameterDock.h"

namespace v2ui {
inline double ExtrudeInputDistance(const V2ParameterDock& numbers,
    const V2ExtrudeDock& tool, bool live)
{
    return live ? tool.DistanceMm() : kachakacha::v2::app::ParameterValueOf(
        numbers.Values(), kachakacha::v2::app::ParameterId::ExtrudeLengthMm);
}
}
