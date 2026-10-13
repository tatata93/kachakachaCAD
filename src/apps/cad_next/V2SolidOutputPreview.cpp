#include "V2SolidOutputPreview.h"
#include "V2Viewport.h"
#include <utility>
void ShowSolidOutputPreview(V2Viewport& viewport, const kachakacha::v2::modeling::ShapeMesh& mesh,
    kachakacha::v2::app::SolidOutputMode mode) {
    viewport.HideToolPreview();
    viewport.ShowToolPreview(mesh.edges);
    if (!kachakacha::v2::app::IncludesSolid(mode)) return;
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> faces;
    for (const auto& t : mesh.triangles) faces.push_back({t.points[0], t.points[1], t.points[2]});
    viewport.SetToolPreviewFaces(std::move(faces), &mesh);
}
