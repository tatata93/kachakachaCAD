#pragma once

//! core の曲線と OCCT の曲線を行き来させる(WP-06)。
//!
//! 大事な約束: 種類を落とさない。
//! 円弧は Geom_TrimmedCurve(Geom_Circle) として渡し、折れ線にしない。
//! 戻すときも、円は円、円弧は円弧、Bezier は Bezier として戻す。
//! どうしても種類を保てないものは、折れ線へ落とさずに断る。
//!
//! このヘッダは OCCT の型を使うので、core からは include しない。
//! 使うのは src/next_occt の中だけ(AT-ARC-001)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"

#ifdef KACHACAD_V2_WITH_OCCT

#include <Geom_Curve.hxx>
#include <Standard_Handle.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <vector>

namespace kachakacha::v2::kernel {

//! 変換で使う診断コード。すべて KER-C0xx。
inline constexpr const char* kCurveUnsupported = "KER-C001";
inline constexpr const char* kKernelFailure = "KER-C002";
inline constexpr const char* kNotConnected = "KER-C003";

[[nodiscard]] gp_Pnt ToPoint(const geometry::Vector3& value);
[[nodiscard]] gp_Vec ToVector(const geometry::Vector3& value);
[[nodiscard]] geometry::Vector3 FromPoint(const gp_Pnt& value);

//! 1本の曲線を OCCT の曲線にする。種類を保つ。
[[nodiscard]] base::Result<occ::handle<Geom_Curve>> ToGeomCurve(
    const geometry::CurveSegment& segment);

//! 1本の曲線を辺にする。
[[nodiscard]] base::Result<TopoDS_Edge> ToEdge(const geometry::CurveSegment& segment);

//! 順に繋がった曲線群を1本のワイヤーにする。
//! 繋がっていない並びは、勝手に繋げずに KER-C003 で断る。
[[nodiscard]] base::Result<TopoDS_Wire> ToWire(
    const std::vector<geometry::CurveSegment>& segments, double toleranceMm);

//! OCCT の曲線を core の曲線へ戻す。
//! 直線・円・円弧・3次Bezier・B-spline はその種類のまま戻す。
//! それ以外は、折れ線へ落とさずに断る。
[[nodiscard]] base::Result<geometry::CurveSegment> FromEdge(const TopoDS_Edge& edge,
    double toleranceMm);

//! ワイヤーの辺を順に core の曲線へ戻す。1本でも戻せなければ全体を断る。
[[nodiscard]] base::Result<std::vector<geometry::CurveSegment>> FromWire(
    const TopoDS_Wire& wire, double toleranceMm);

} // namespace kachakacha::v2::kernel

#endif
