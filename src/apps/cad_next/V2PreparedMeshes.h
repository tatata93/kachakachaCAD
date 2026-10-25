#pragma once
#include "kachakacha/modeling/ShapeMesh.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include "kachakacha/base/Diagnostic.h"
#include <map>
#include <string>
#include <vector>
struct V2PreparedMeshRequest {
    std::string id;
    kachakacha::v2::modeling::KernelShapeHandle shape;
    bool surface=false;
};
kachakacha::v2::base::Result<bool> V2BuildPreparedMeshes(
    const std::vector<V2PreparedMeshRequest>& requests,
    std::map<std::string,kachakacha::v2::modeling::ShapeMesh>& meshes,bool parallel);
