#include "V2FabricationSources.h"
#include "kachakacha/kernel/OcctFaceQuery.h"

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
        const auto sampled=kernel::FaceSamplesOf(handle,face);
        if(!sampled.HasValue())source.samplingDiagnostics=sampled.Diagnostics();
        else {
            source.samples=sampled.Value().samples;
            const auto native=kernel::FabricationEvaluatorOf(handle,face);
            if(native.HasValue())source.evaluate=native.Value();
            else {
                // Planar trims have a true polygon boundary; keep that and its holes.
                const auto boundary=kernel::FaceBoundaryOf(handle,face,geometry::GeometryTolerance{});
                if(boundary.HasValue()) {
                    source.samples.reset();source.flatBoundary=boundary.Value().outerLoop;
                    source.flatOpenings=boundary.Value().holeLoops;
                } else source.samplingDiagnostics=native.Diagnostics();
            }
        }
        sources.push_back(std::move(source));
    }
}
