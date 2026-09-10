#pragma once

//! 線を、標本化した曲面(形状ガイド)へ向きを決めて落とす(V1 の「面へ投影」の曲面版)。
//!
//! 曲がった面に窓を開けるには、平らに描いた窓の線を面の上へ落とす必要がある。
//! 作業平面へ落とす投影(geometry/CurveProjection)は平面にしか落とせないので、
//! 曲面用をここに置く。落とし方は「向きに沿って面へ当てる」。
//! 当たらない点があれば、その線は落とせないと言う(近い点へ寄せない)。
//!
//! 落ちた線は折れ線になる。曲面の上の曲線は V2 の線の種類(直線・円弧・ベジエ…)では
//! 表せないので、標本化して直線でつなぐ。近似の側も折れ線で扱うので、ここで足りる。
//! OCCT を使わない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/SurfacePatch.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/Vector3.h"

#include <vector>

namespace kachakacha::v2::fabrication {

inline constexpr const char* kProjectionMisses = "FAB-J001";
inline constexpr const char* kProjectionBadInput = "FAB-J002";

//! 点1つを、向き(前後どちらでもよい)に沿って面へ当てる。当たらなければ空。
[[nodiscard]] base::Result<geometry::Vector3> ProjectPointOntoSampledSurface(
    const SurfacePatchSamples& samples, const geometry::Vector3& point,
    const geometry::Vector3& direction);

//! 線の並びを面へ落とす。1本でも落ちなければ断る(半分だけ落ちた形を返さない)。
//! 戻りは折れ線(直線の並び)。閉じた輪は閉じたまま落ちる。
[[nodiscard]] base::Result<std::vector<geometry::CurveSegment>>
ProjectCurvesOntoSampledSurface(const SurfacePatchSamples& samples,
    const std::vector<geometry::CurveSegment>& curves, const geometry::Vector3& direction,
    double toleranceMm);

} // namespace kachakacha::v2::fabrication
