#pragma once
#include "V2Viewport.h"
#include <algorithm>

// One decision for frame composition and shape shading. Hidden overlays never
// force software shading. Keep GPU QPainter acceleration for grids and wires.
struct V2VisibleRenderLayers {bool shapes=false,images=false,analysis=false;};
inline V2VisibleRenderLayers V2RenderLayersOf(const V2Viewport& viewport,
    const std::vector<V2Viewport::AnalysisView>& analysis)
{
    V2VisibleRenderLayers result;
    result.shapes=viewport.DisplaySettingsNow().shapesVisible&&std::any_of(
        viewport.ShapeViews().begin(),viewport.ShapeViews().end(),[&](const auto& shape){
            return viewport.EntityShown(shape.entityId)&&(!shape.mesh.triangles.empty()||!shape.mesh.edges.empty());});
    result.images=std::any_of(viewport.ImageViews().begin(),viewport.ImageViews().end(),[&](const auto& image){
        return viewport.EntityShown(image.entityId)&&image.image.opacity>0&&!image.image.pixels.empty()&&!image.triangles.empty();});
    result.analysis=viewport.DisplaySettingsNow().shapesVisible&&std::any_of(analysis.begin(),analysis.end(),[&](const auto& item){
        return viewport.EntityShown(item.entityId)&&item.Painted()
            &&kachakacha::v2::app::AnalysisPaintsSurface(item.mode);});
    return result;
}
