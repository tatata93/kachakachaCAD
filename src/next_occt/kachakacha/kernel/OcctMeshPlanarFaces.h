#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include <TopoDS_Shape.hxx>
namespace kachakacha::v2::kernel::detail {
// A non-mutating meshing copy. Planar revolution poles are parameterized as
// planes so that their triangulation cannot fold across the singular pole.
TopoDS_Shape MeshReadyShape(const TopoDS_Shape& source);
// Independent native display copy, also used to capture a decal's face mesh.
TopoDS_Shape TessellatedCopy(const TopoDS_Shape& source,double deflectionMm=0.0);
}
#endif
