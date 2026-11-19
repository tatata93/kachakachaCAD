#pragma once
#include "kachakacha/document/FeatureReevaluation.h"
#include "kachakacha/geometry/PlanarWireOffset.h"

namespace v2ui {
inline auto EvaluateWireEdit(const kachakacha::v2::domain::TransformWireDefinition& definition,
    const std::vector<kachakacha::v2::geometry::CurveSegment>& curves,
    const kachakacha::v2::geometry::GeometryTolerance& tolerance)
{
    if(definition.method==kachakacha::v2::domain::WireTransformMethod::Offset)
        return kachakacha::v2::geometry::OffsetCurvesInPlane(curves,definition.vectorArgument,
            definition.scalarArgument.value,tolerance);
    return kachakacha::v2::document::EvaluateWireTransform(definition,curves);
}
}
