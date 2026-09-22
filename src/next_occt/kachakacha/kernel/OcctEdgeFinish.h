#pragma once

//! 立体の辺を丸める(フィレット)・落とす(面取り)。matrix P-12。
//!
//! 辺は **辺の真ん中の点** で指す。文書には番号ではなく点を残し、開き直したら同じ形の
//! 同じ真ん中を持つ辺を選び直す(番号は作り直しで並びが変わりうるが、同じ形の辺の真ん中は
//! 変わらない)。1 本でも見つからなければ作らない(黙って残りだけ丸めない)。
//!
//! 出来た形は BRepCheck で壊れていないか・体積が変わったかを見る。変わらなければ
//! 作れたことにしない。OCCT の型を外へ出さない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#include <vector>

namespace kachakacha::v2::kernel {

//! 丸め・面取りを作れなかった(大きすぎる・隣の面とぶつかる など)。
inline constexpr const char* kEdgeFinishFailed = "KER-R001";
//! 指した辺がその立体に無い(形が変わった・立体でない)。
inline constexpr const char* kEdgeFinishEdgeMissing = "KER-R002";
//! 出来た形が立体として壊れている・体積が変わらない。
inline constexpr const char* kEdgeFinishInvalid = "KER-R003";
//! カーネルが入っていない版。
inline constexpr const char* kEdgeFinishUnsupported = "KER-R004";

enum class EdgeFinishKind {
    Fillet,    //!< R 丸め(半径)
    Chamfer,   //!< C 面取り(両側に同じ距離)
};

struct SolidEdgeInfo {
    //! 辺の真ん中(記録と選び直しの鍵)。
    geometry::Vector3 midpoint{};
    //! 札・下見に出す折れ線。
    std::vector<geometry::Vector3> polyline;
    //! 押した点からの距離。
    double distanceMm = 0.0;
};

struct EdgeFinishResult {
    modeling::KernelShapeHandle handle;
    double volumeMm3 = 0.0;
    double previousVolumeMm3 = 0.0;
};

//! 点に一番近い、立体の辺(退化した辺は除く)。
[[nodiscard]] base::Result<SolidEdgeInfo> NearestSolidEdge(
    const modeling::KernelShapeHandle& solid, const geometry::Vector3& point);

//! 真ん中の点で指した辺の折れ線(札と下見)。見つからなければ断る。
[[nodiscard]] base::Result<SolidEdgeInfo> SolidEdgeAt(const modeling::KernelShapeHandle& solid,
    const geometry::Vector3& midpoint, const geometry::GeometryTolerance& tolerance);

//! 辺を丸める / 落とす。辺は真ん中の点で指す。1 本でも見つからなければ断る。
[[nodiscard]] base::Result<EdgeFinishResult> FinishSolidEdges(
    const modeling::KernelShapeHandle& solid, EdgeFinishKind kind, double sizeMm,
    const std::vector<geometry::Vector3>& edgeMidpoints,
    const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::kernel
