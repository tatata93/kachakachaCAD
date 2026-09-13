#pragma once

//! 立体の面どうしの隣り合わせを数える(製作近似 §5.3 の分割優先順位)。
//!
//! いまの V2 方式は、展開できない面が1枚でもあるとそこで全部を断る。
//! 「1枚では無理だが、3枚に分ければできる」と言えないし、分けることもできない。
//!
//! 分け方を決める `fabrication::BuildPanelPartition` は、面ごとの分類
//! (`PanelCandidate`)と **隣り合わせ**(`PanelAdjacency`)の2つを要る。
//! 前者は `CurvatureAnalysis` から作れる。**後者を作る道が無かった。**
//!
//! 隣り合わせは形の話なので、共有辺を数えられるのはカーネルだけである。
//! ここが、辺 → 面の対応から「2枚の面が共有する辺の長さと二面角」を返す。
//!
//! 面の番号は `OcctTessellate` と `OcctFaceQuery` と同じ順で数える
//! (`TopExp_Explorer(shape, TopAbs_FACE)` の順)。3つが揃っていないと、
//! 分類・縁・隣り合わせが別々の面を指してしまう。
//!
//! OCCT の型を外へ出さない。返すのは core の型だけ(AT-ARC-001)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/PanelStrategy.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#include <vector>

namespace kachakacha::v2::kernel {

//! 隣り合わせを数える層の診断コード。
inline constexpr const char* kAdjacencySourceMissing = "KER-A001";
inline constexpr const char* kAdjacencyFailed = "KER-A002";

//! 立体(またはシェル)の面どうしの隣り合わせを返す。
//!
//! 返す並びは決定的である(小さいほうの面番号、次に大きいほうの面番号の昇順)。
//! 並びが揺れると、同じ形から違う分け方が出てしまう。
//!
//! 2枚より多くの面が同じ辺を共有する形(非多様体)は、その辺を飛ばす。
//! どちらへ折るのかが決まらないためである。飛ばしたことは Diagnostic で言う。
//!
//! `forcedBoundary` と `keepTogether` はここでは立てない。
//! あれは人が決めることで、形からは分からない。
[[nodiscard]] base::Result<std::vector<fabrication::PanelAdjacency>> FaceAdjacenciesOf(
    modeling::KernelShapeHandle handle, const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::kernel
