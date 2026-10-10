#pragma once
#include "kachakacha/kernel/OcctFaceQuery.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <TopoDS_Face.hxx>
namespace kachakacha::v2::kernel::detail {
base::Result<fabrication::SurfaceEvaluator> TrimmedFabricationEvaluator(const TopoDS_Face& face);
}
#endif
