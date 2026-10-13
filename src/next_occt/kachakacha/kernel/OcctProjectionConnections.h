#pragma once
#include "kachakacha/kernel/OcctWireProjection.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <TopoDS_Face.hxx>
namespace kachakacha::v2::kernel::detail {
// Ephemeral provenance; source curves remain alive for the enclosing projection call.
struct ProjectionRunOrigin {const geometry::CurveSegment* source=nullptr;};
void ConnectSingleFaceProjection(WireProjectionResult& result,const TopoDS_Face& face,
    const std::vector<ProjectionRunOrigin>& origins,double tolerance);
}
#endif
