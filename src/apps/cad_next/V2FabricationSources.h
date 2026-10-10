#pragma once
#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
void AppendNativeFabricationSources(kachakacha::v2::modeling::KernelShapeHandle handle,
    kachakacha::v2::base::EntityId entity,const std::string& name,
    std::vector<kachakacha::v2::app::FabricationSource>& sources);
