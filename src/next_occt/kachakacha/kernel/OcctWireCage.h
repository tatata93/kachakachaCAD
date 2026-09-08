#pragma once

//! ワイヤーかごから立体を作る(geometry-contract §7.3 の 7、AT-GEO-013)。
//!
//! core が「囲めている」と判断した候補だけを、ここで実物にする。
//! 判断は core が済ませてあるので、ここでやるのは
//!   面を作る → 殻に縫う → 立体にする → 出来たものを検査する
//! の4つだけである。
//!
//! **1つの閉シェル = 1つの立体。** 2つ選ばれても、中身が2つ入った
//! 1つの立体にはしない。まとめてしまうと、片方だけを消す・厚みを変える
//! といった操作ができなくなる。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include "kachakacha/modeling/WireCage.h"

#include <vector>

namespace kachakacha::v2::kernel {

//! 作った立体が不正だった。
inline constexpr const char* kCageInvalidSolid = "GEO-S005";
//! 立体を作るところでカーネルが失敗した。
inline constexpr const char* kCageKernelFailure = "GEO-S008";

//! 出来た立体1つ。
struct BuiltCagePart {
    modeling::KernelShapeHandle handle;
    std::size_t shellIndex = 0;
    double volumeMm3 = 0.0;
    std::size_t faceCount = 0;
    //! 面の意味的キー。core が決めたものをそのまま持つ。
    std::vector<std::string> faceKeys;
};

//! 選ばれたシェルを立体にする。選んだ数だけ立体が出る。
//!
//! `edges` は core へ渡したものと同じ並びでなければならない。
//! パッチは辺の添字で面を指しているので、並びが変わると別の面になる。
[[nodiscard]] base::Result<std::vector<BuiltCagePart>> BuildWireCageParts(
    const std::vector<modeling::CageEdgeInput>& edges,
    const modeling::WireCageAnalysis& analysis,
    const std::vector<modeling::WireCagePart>& plan,
    const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::kernel
