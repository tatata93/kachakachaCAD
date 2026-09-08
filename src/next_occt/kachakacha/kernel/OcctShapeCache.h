#pragma once

//! カーネルが作った実体を、番号で引ける表(WP-06 / WP-07 共通)。
//!
//! core は番号(`KernelShapeHandle`)しか知らない。OCCT の型は外へ出さない。
//! 番号は使い回さない。捨てた番号で引いたら「無い」と答える。
//! これは、消えた面を指す参照が別の面へ黙って移るのを防ぐためである。

#include "kachakacha/modeling/GuideSurfaceResult.h"

#ifdef KACHACAD_V2_WITH_OCCT
#include <TopoDS_Shape.hxx>

namespace kachakacha::v2::kernel {

[[nodiscard]] modeling::KernelShapeHandle StoreShape(const TopoDS_Shape& shape);
[[nodiscard]] bool LookupShape(modeling::KernelShapeHandle handle, TopoDS_Shape& out);

} // namespace kachakacha::v2::kernel
#endif
