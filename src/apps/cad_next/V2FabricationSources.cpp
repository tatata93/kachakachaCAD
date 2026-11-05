#include "V2FabricationSources.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include "kachakacha/kernel/OcctFabricationRegion.h"
#include <cmath>

namespace {
bool FullRectangle(const std::vector<std::vector<kachakacha::v2::geometry::Point2>>& loops)
{
    using namespace kachakacha::v2;
    if(loops.size()!=1||loops.front().size()<4)return false;
    const auto& loop=loops.front();const double epsilon=geometry::GeometryTolerance{}.numericEpsilon;
    double twiceArea=0;
    for(std::size_t i=0;i<loop.size();++i){
        const auto a=loop[i],b=loop[(i+1)%loop.size()];
        const bool onU=std::abs(a.u-b.u)<=epsilon&&(std::abs(a.u)<=epsilon||std::abs(a.u-1)<=epsilon);
        const bool onV=std::abs(a.v-b.v)<=epsilon&&(std::abs(a.v)<=epsilon||std::abs(a.v-1)<=epsilon);
        if(!onU&&!onV)return false;
        twiceArea+=a.u*b.v-a.v*b.u;
    }
    return std::abs(std::abs(twiceArea)*.5-1)<=epsilon*(loop.size()+1);
}
}

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
                if(!FullRectangle(region.Value().loops)) {
                    source.materialRegionsUv.push_back({region.Value().loops});
                    source.boundarySamplingDeviationMm=region.Value().boundarySamplingDeviationMm;
                }
            } else source.samplingDiagnostics=region.Diagnostics();
        }
        sources.push_back(std::move(source));
    }
}
