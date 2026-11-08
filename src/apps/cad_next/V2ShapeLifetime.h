#pragma once

#include "V2MainWindowTypes.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/document/Document.h"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace v2ui {
using NativeShapes=std::map<std::string,kachakacha::v2::modeling::KernelShapeHandle>;

struct ShapeListOwner {
    std::vector<kachakacha::v2::modeling::KernelShapeHandle> values;
    explicit ShapeListOwner(const decltype(values)& handles):values(handles){}
    ShapeListOwner(const ShapeListOwner&)=delete;
    ShapeListOwner& operator=(const ShapeListOwner&)=delete;
    ~ShapeListOwner(){for(const auto handle:values)if(handle.Valid())kachakacha::v2::kernel::ReleaseShape(handle);}
    [[nodiscard]] auto Take(std::size_t index){const auto handle=values[index];values[index]={};return handle;}
};

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

inline void ReleaseDeletedShapes(const std::set<std::string>& liveEntities,
    NativeShapes& parts,NativeShapes& surfaces)
{
    std::set<std::uint64_t> removed,retained;
    for (auto* shapes:{&parts,&surfaces}) {
        std::erase_if(*shapes,[&](const auto& entry) {
            if (liveEntities.contains(entry.first)) return false;
            removed.insert(entry.second.value);
            return true;
        });
        for (const auto& [id,shape]:*shapes) retained.insert(shape.value);
    }
    for (const auto value:removed)
        if (!retained.contains(value)) kachakacha::v2::kernel::ReleaseShape({value});
}

template<class... EntityMaps>
void ReleaseDeletedShapes(const kachakacha::v2::document::DocumentSnapshot& snapshot,
    NativeShapes& parts,NativeShapes& surfaces,EntityMaps&... derived)
{
    std::set<std::string> liveEntities;
    for (const auto& entity:snapshot.entities) liveEntities.insert(entity.id.ToString());
    ReleaseDeletedShapes(liveEntities,parts,surfaces);
    const auto gone=[&](const auto& entry){return !liveEntities.contains(entry.first);};
    (std::erase_if(derived,gone),...);
}
}
