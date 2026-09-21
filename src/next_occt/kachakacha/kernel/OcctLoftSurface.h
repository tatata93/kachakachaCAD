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
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <cstddef>
#include <vector>

namespace kachakacha::v2::kernel::detail {

//! 境界の辺の連続条件(G1/G2)を、作ったあとで測るための控え。
struct ContinuityCheck {
    std::size_t chainIndex = 0;
    modeling::SurfaceContinuity order = modeling::SurfaceContinuity::G0;
    //! その辺が乗っている支持面の面。
    TopoDS_Face support;
};

//! 測った滑らかさ。負の値は「測る対象が無い」。
struct ContinuityMeasure {
    double g1ErrorDeg = -1.0;
    double g2Error = -1.0;
};

//! 1 本の境界辺を足す。G1/G2 なら支持面(その辺が乗っている面)に対して足し、控えを残す。
//! edge を渡せばその辺を、空なら鎖の曲線を 1 本ずつ辺にして足す。
[[nodiscard]] base::Result<bool> AddBoundaryEdges(BRepOffsetAPI_MakeFilling& filler,
    const modeling::GuideSurfaceRequest& request, std::size_t chainIndex,
    const std::vector<TopoDS_Edge>& edges, const geometry::GeometryTolerance& tolerance,
    std::vector<ContinuityCheck>& checks);

//! 作ったあとで、控えた G1/G2 を測る。許容を超えていれば、どの辺で何度折れたかを言って断る。
//!
//! 核の G1Error / G2Error は使わない。OCCT の GeomPlate_BuildPlateSurface::G1Error(Index)
//! は作業用の配列を「曲線上の点の数(作るときの引数)」で確保し、拘束の点がそれより多いと
//! 配列の外へ書く(2026-09-22 PC: ヒープ破損 0xc0000374)。だから辺の上の点で、
//! 出来た面と支持面の法線の角度(G1)と、辺を横切る向きの法曲率の差(G2)を自分で測る。
[[nodiscard]] base::Result<ContinuityMeasure> MeasureContinuity(const TopoDS_Shape& built,
    const modeling::GuideSurfaceRequest& request, const std::vector<ContinuityCheck>& checks);

//! ガイドや中心線を使うロフト(解析の LoftSolver が Sections 以外)。
[[nodiscard]] base::Result<TopoDS_Shape> BuildLoftShape(
    const modeling::GuideSurfaceRequest& request,
    const modeling::GuideSurfaceAnalysis& analysis,
    const geometry::GeometryTolerance& tolerance);

//! 曲線網(Gordon)。core が組み立てた格子点(U 線・V 線を全部通る)を B-spline 面へ写す。
[[nodiscard]] base::Result<TopoDS_Shape> BuildNetworkShape(
    const modeling::GuideSurfaceRequest& request,
    const geometry::GeometryTolerance& tolerance);

//! 四辺面。内側の通る線があれば、4 辺の面を初期面にして通る線へ寄せて張り直す。
[[nodiscard]] base::Result<TopoDS_Shape> BuildFourEdgeShape(
    const modeling::GuideSurfaceRequest& request,
    const modeling::GuideSurfaceAnalysis& analysis,
    const geometry::GeometryTolerance& tolerance, ContinuityMeasure& measure);

} // namespace kachakacha::v2::kernel::detail
#endif
