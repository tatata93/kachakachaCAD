#include "V2ApproxPreview.h"
#include "V2Viewport.h"
#include "kachakacha/modeling/ShapeMesh.h"
#include "V2FabricationMaterial.h"
#include "V2Calculation.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <algorithm>

bool ShowBandApproxPreview(V2Viewport& viewport,
    const std::vector<std::vector<kachakacha::v2::geometry::Vector3>>& rails,
    const kachakacha::v2::app::FabricationEvaluation& evaluation,std::string& error)
{
    using namespace kachakacha::v2;
    modeling::ShapeMesh mesh;
    std::vector<std::vector<geometry::Vector3>> faces;
    for(std::size_t band=0;band+1<rails.size();band+=2) {
        if(band/2<evaluation.materialBands.size()&&evaluation.materialBands[band/2])continue;
        const auto& a=rails[band];const auto& b=rails[band+1];
        if(a.size()!=b.size())continue;
        for(std::size_t i=1;i<a.size();++i) {
            faces.push_back({a[i-1],a[i],b[i],b[i-1]});
            modeling::MeshTriangle first,second;
            first.points={a[i-1],a[i],b[i]};second.points={a[i-1],b[i],b[i-1]};
            mesh.triangles.push_back(first);mesh.triangles.push_back(second);
        }
    }
    for(const auto& panel:evaluation.materialPanels){
        const auto native=V2MaterialMesh(panel,rails);
        if(native.Empty()){error="窓と切り欠きを保つ近似下見を作れません。";return false;}
        for(const auto& triangle:native.triangles){faces.push_back({triangle.points[0],triangle.points[1],triangle.points[2]});mesh.triangles.push_back(triangle);}
    }
    modeling::RefreshNormals(mesh);
    viewport.SetToolPreviewFaces(std::move(faces),&mesh);
    return true;
}
