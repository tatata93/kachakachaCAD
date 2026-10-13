#pragma once
#include "kachakacha/kernel/OcctWireProjection.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <TopoDS_Face.hxx>
namespace kachakacha::v2::kernel::detail {
base::Result<WireProjectionResult> WrapNativeWires(const TopoDS_Face& face,
    const std::vector<WireProjectionInput>& inputs,const WireProjectionOptions& options);
}
#endif
