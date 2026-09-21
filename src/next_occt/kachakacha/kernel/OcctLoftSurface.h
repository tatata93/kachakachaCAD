#pragma once

//! ロフト(断面 2〜任意 + ガイド 0〜任意 + 中心線 0〜1)と四辺面を作る(OcctGuideSurface の中身)。
//!
//! 外へ見せる API ではない。OcctGuideSurface.cpp だけが使う。
//! 作り方は検査(modeling/LoftInput・FourEdgeInput)が決めて analysis に書いてある。
//! ここはそのとおりに作るだけで、**どの入力も捨てない**。出来たものは呼び出し元が
//! 全部の線からの外れを測り、外れすぎていれば捨てる。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#ifdef KACHACAD_V2_WITH_OCCT
#include <TopoDS_Shape.hxx>

namespace kachakacha::v2::kernel::detail {

//! ガイドや中心線を使うロフト(解析の LoftSolver が Sections 以外)。
[[nodiscard]] base::Result<TopoDS_Shape> BuildLoftShape(
    const modeling::GuideSurfaceRequest& request,
    const modeling::GuideSurfaceAnalysis& analysis,
    const geometry::GeometryTolerance& tolerance);

//! 四辺面。内側の通る線があれば、4 辺の面を初期面にして通る線へ寄せて張り直す。
[[nodiscard]] base::Result<TopoDS_Shape> BuildFourEdgeShape(
    const modeling::GuideSurfaceRequest& request,
    const modeling::GuideSurfaceAnalysis& analysis,
    const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::kernel::detail
#endif
