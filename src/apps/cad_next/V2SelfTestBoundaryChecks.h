#pragma once
#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include <algorithm>

namespace kachakacha::v2::selftest {
// Independent invariant: one editable wire cannot contain disconnected edges.
inline bool NewBoundaryWiresAreConnected(V2MainWindow& window,const document::DocumentSnapshot& before) {
    const auto& document=window.Session().GetDocument();
    const double tolerance=document.Snapshot().settings.tolerance.modelLinearMm;
    for(const auto& e:document.Snapshot().entities) {
        if(e.kind!=domain::EntityKind::Wire||std::any_of(before.entities.begin(),before.entities.end(),
            [&](const auto& old){return old.id==e.id;}))continue;
        const auto* f=document.FindFeature(e.createdBy);
        const auto* wire=f?std::get_if<domain::CreateWireDefinition>(&f->definition):nullptr;
        if(!Explain("generated boundary contains editing curves",wire&&!wire->segments.empty()))return false;
        for(std::size_t i=1;i<wire->segments.size();++i)
            if(!Explain("each generated wire is a connected chain, not a bag of native edges",
                geometry::Distance(wire->segments[i-1].EndPoint(),wire->segments[i].StartPoint())<=tolerance))return false;
    }
    return true;
}
}
