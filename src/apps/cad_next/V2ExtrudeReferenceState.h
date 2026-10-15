#pragma once
#include "kachakacha/app/ExtrudeOptions.h"
#include "kachakacha/domain/Feature.h"

inline void StoreExtrudeReference(kachakacha::v2::domain::ExtrudeDefinition& definition,
    const kachakacha::v2::app::ExtrudeChoice& choice)
{
    definition.extentTarget = choice.extent == kachakacha::v2::modeling::ExtrudeExtentMode::ToTarget
        ? choice.targetEntityId : std::nullopt;
    definition.endCap = choice.endCap;
    definition.targetParameter = choice.targetParameter;
    definition.targetOffsetMm = choice.targetOffsetMm;
    definition.targetFaceBrep = choice.targetFaceBrep;
}

inline void RestoreExtrudeReference(kachakacha::v2::app::ExtrudeChoice& choice,
    const kachakacha::v2::domain::ExtrudeDefinition& definition)
{
    choice.targetEntityId = definition.extentTarget;
    choice.endCap = definition.endCap;
    choice.targetParameter = definition.targetParameter;
    choice.targetOffsetMm = definition.targetOffsetMm;
    choice.targetFaceBrep = definition.targetFaceBrep;
}
