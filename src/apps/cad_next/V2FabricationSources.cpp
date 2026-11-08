#include "V2FabricationSources.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include "kachakacha/kernel/OcctFabricationRegion.h"

void AppendNativeFabricationSources(kachakacha::v2::modeling::KernelShapeHandle handle,
    kachakacha::v2::base::EntityId entity,const std::string& name,
    std::vector<kachakacha::v2::app::FabricationSource>& sources)
{
    using namespace kachakacha::v2;
    const auto count=kernel::ShapeFaceCount(handle);
    if(!count.HasValue()) {
        app::FabricationSource source;source.entityId=entity;source.name=name;
        source.samplingDiagnostics=count.Diagnostics();sources.push_back(std::move(source));return;
    }
    for(std::size_t face=0;face<count.Value();++face) {
        app::FabricationSource source;source.entityId=entity;source.faceIndex=face;
        source.name=name+(count.Value()>1 ? " 面"+std::to_string(face+1) : "");
        // A trimmed scan chart warps parameter lines around notches. It is not
        // the supporting surface from which physical strips must be developed.
        const auto boundary=kernel::FaceBoundaryOf(handle,face,geometry::GeometryTolerance{});
        if(boundary.HasValue()) {
            source.flatBoundary=boundary.Value().outerLoop;
            source.flatOpenings=boundary.Value().holeLoops;
        } else {
            const auto region=kernel::FabricationRegionOf(handle,face,.0005);
            if(region.HasValue()) {
                source.samples=region.Value().samples;source.evaluate=region.Value().evaluate;
                // A complete UV rectangle still needs its material/assembly
                // correspondence when classification is followed by freezing.
                // Omitting it produced a pattern without any rails to freeze.
                source.materialRegionsUv.push_back({region.Value().loops});
                source.boundarySamplingDeviationMm=region.Value().boundarySamplingDeviationMm;
            } else source.samplingDiagnostics=region.Diagnostics();
        }
        sources.push_back(std::move(source));
    }
}
