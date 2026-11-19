#pragma once
#include "kachakacha/document/FeatureReevaluation.h"
#include "kachakacha/geometry/PlanarWireOffset.h"
#include "kachakacha/geometry/WireEdit.h"

namespace v2ui {
inline auto EvaluateWireEdit(const kachakacha::v2::domain::TransformWireDefinition& definition,
    const std::vector<kachakacha::v2::geometry::CurveSegment>& curves,
    const kachakacha::v2::geometry::GeometryTolerance& tolerance)
{
    if(definition.method==kachakacha::v2::domain::WireTransformMethod::Offset)
        return kachakacha::v2::geometry::OffsetCurvesInPlane(curves,definition.vectorArgument,
            definition.scalarArgument.value,tolerance);
    using namespace kachakacha::v2;
    if(curves.size()==2&&(definition.method==domain::WireTransformMethod::Fillet||definition.method==domain::WireTransformMethod::Chamfer)){
        geometry::CornerOptions options;
        options.secondSetbackMm=definition.secondScalarMm;options.firstKeepSide=definition.firstKeepSide;
        options.secondKeepSide=definition.secondKeepSide;options.firstHint=definition.firstHint;options.secondHint=definition.secondHint;
        const auto result=definition.method==domain::WireTransformMethod::Fillet
            ?geometry::FilletLines(curves[0],curves[1],definition.scalarArgument.value,options,tolerance.modelLinearMm)
            :geometry::ChamferLines(curves[0],curves[1],definition.scalarArgument.value,options,tolerance.modelLinearMm);
        using Out=base::Result<std::vector<geometry::CurveSegment>>;
        return result.HasValue()?Out::Success(result.Value().Segments()):Out::Failure(result.Diagnostics());
    }
    return kachakacha::v2::document::EvaluateWireTransform(definition,curves);
}
}
