#pragma once
#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
namespace kachakacha::v2::kernel {
struct GptFabricationInput {
    base::EntityId id;
    modeling::KernelShapeHandle handle;
};
base::Result<app::FabricationEvaluation> BuildGptFabrication(
    const domain::CreateFabricationModelDefinition& definition,
    const std::vector<GptFabricationInput>& inputs);
}
