#include "V2ApproxPreview.h"
#include "V2Viewport.h"
#include "kachakacha/modeling/ShapeMesh.h"

void ShowBandApproxPreview(V2Viewport& viewport,
    const std::vector<std::vector<kachakacha::v2::geometry::Vector3>>& rails)
{
    using namespace kachakacha::v2;
    modeling::ShapeMesh mesh;
    std::vector<std::vector<geometry::Vector3>> faces;
    for(std::size_t band=0;band+1<rails.size();band+=2) {
        const auto& a=rails[band];const auto& b=rails[band+1];
        if(a.size()!=b.size())continue;
        for(std::size_t i=1;i<a.size();++i) {
            faces.push_back({a[i-1],a[i],b[i],b[i-1]});
            modeling::MeshTriangle first,second;
            first.points={a[i-1],a[i],b[i]};second.points={a[i-1],b[i],b[i-1]};
            mesh.triangles.push_back(first);mesh.triangles.push_back(second);
        }
    }
    modeling::RefreshNormals(mesh);
    viewport.SetToolPreviewFaces(std::move(faces),&mesh);
}
