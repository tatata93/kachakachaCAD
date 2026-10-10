#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/base/Diagnostic.h"
#include <Geom_BSplineSurface.hxx>

namespace kachakacha::v2::kernel::detail {
// Boundary-preserving polynomial transfinite blend. No input guides are discarded.
[[nodiscard]] base::Result<occ::handle<Geom_BSplineSurface>> DomePatch(
    const occ::handle<Geom_BSplineSurface>& base);
[[nodiscard]] occ::handle<Geom_BSplineSurface> TransportDomeProfile(
    const occ::handle<Geom_BSplineSurface>& base);
}
#endif
