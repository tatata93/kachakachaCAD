#pragma once

#include "V2MainWindowTypes.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <map>
#include <optional>
#include <set>
#include <string>

namespace v2ui {
using NativeShapes=std::map<std::string,kachakacha::v2::modeling::KernelShapeHandle>;

// A preview and a committed document may briefly refer to the same native shape.
// Release only uncommitted handles, once, when replacing or cancelling a preview.
inline void ReleaseSurfacePreview(std::optional<V2SurfaceSnapshot>& preview,
    const NativeShapes& committed={})
{
    if(!preview)return;
    std::set<std::uint64_t> retained,released;
    for(const auto& [id,shape]:committed)retained.insert(shape.value);
    const auto release=[&](auto shape){if(!retained.contains(shape.value)&&released.insert(shape.value).second)
        kachakacha::v2::kernel::ReleaseShape(shape);};
    release(preview->built.handle);
    for(const auto& [table,built]:preview->batch)release(built.handle);
    preview.reset();
}

inline void ReleaseDocumentShapes(NativeShapes& parts,NativeShapes& surfaces)
{
    std::set<std::uint64_t> released;
    for(const auto* shapes:{&parts,&surfaces})for(const auto& [id,shape]:*shapes)
        if(released.insert(shape.value).second)kachakacha::v2::kernel::ReleaseShape(shape);
    parts.clear();surfaces.clear();
}
}
