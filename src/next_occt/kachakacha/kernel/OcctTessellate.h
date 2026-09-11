#pragma once

//! 核の形を、画面に出せる網にする(棚卸し A-1)。
//!
//! V2 の 3D 画面は線と点しか描いていなかった。押し出しても面を作っても、
//! 画面には何も増えず、できた物は一覧に名前が出るだけだった。
//! **工程5から先が目で確かめられない。** ここがその道である。
//!
//! 三角形は STL の書き出しと **同じ道**(`BRepMesh_IncrementalMesh`)で作る。
//! 別々に近似すると、画面で見た形と出した形が食い違う。
//!
//! 稜線は元の辺(`TopAbs_EDGE`)から取る。三角形の網だけを描くと、
//! 面の継ぎ目が三角形の辺として全部出て、形が読めなくなる。
//!
//! 粗さ(deflection)は **形の大きさから決める**。固定値にすると、
//! 小さい部品は面が10枚になり、大きい部品は三角形が何十万枚にもなる。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/modeling/ShapeMesh.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

namespace kachakacha::v2::kernel {

inline constexpr const char* kTessellateFailed = "KER-M001";
inline constexpr const char* kTessellateUnknownShape = "KER-M002";

//! 形の外接箱の対角から、ほどよい粗さを決める。
//! 返す値は mm。小さすぎると重く、大きすぎると角張って見える。
[[nodiscard]] double DeflectionForSize(double boundingDiagonalMm) noexcept;

//! 形を網にする。三角形と稜線と外接箱が入る。
//! deflectionMm に 0 以下を渡すと、大きさから決める。
[[nodiscard]] base::Result<modeling::ShapeMesh> BuildShapeMesh(
    modeling::KernelShapeHandle handle, double deflectionMm = 0.0);

} // namespace kachakacha::v2::kernel
