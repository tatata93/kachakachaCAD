#pragma once

//! 立体を作る(部品モードの「作成」: 回転体・ロフト立体・スイープ、matrix P-08/P-09)。
//!
//! 入力の検査と予測は core(modeling/SolidInput)がする。ここは形を作り、
//! 出来た立体が正しいか(BRepCheck で壊れていない・体積が正)と、回転体なら
//! 予測した体積(Pappus)と合うかを確かめる。合わなければ作れたことにしない。
//!
//! OCCT の型を外へ出さない。返すのは core の型だけ。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include "kachakacha/modeling/SolidInput.h"

namespace kachakacha::v2::kernel {

//! 立体を作れなかった。
inline constexpr const char* kSolidBuildFailed = "KER-O001";
//! 出来た形が立体として壊れている・体積が予測と合わない。
inline constexpr const char* kSolidResultInvalid = "KER-O002";
//! カーネルが入っていない版。
inline constexpr const char* kSolidUnsupported = "KER-O003";

struct SolidBuildResult {
    modeling::KernelShapeHandle handle;
    double volumeMm3 = 0.0;
};

[[nodiscard]] base::Result<SolidBuildResult> BuildRevolveSolid(
    const modeling::RevolveSolidRequest& request, const modeling::RevolveSolidAnalysis& analysis,
    const geometry::GeometryTolerance& tolerance);

[[nodiscard]] base::Result<SolidBuildResult> BuildLoftSolid(
    const modeling::LoftSolidRequest& request, const modeling::LoftSolidAnalysis& analysis,
    const geometry::GeometryTolerance& tolerance);

[[nodiscard]] base::Result<SolidBuildResult> BuildSweepSolid(
    const modeling::SweepSolidRequest& request, const modeling::SweepSolidAnalysis& analysis,
    const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::kernel
