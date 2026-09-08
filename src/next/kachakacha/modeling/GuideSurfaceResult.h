#pragma once

//! 形状ガイドを作った結果(core側の型)。
//!
//! OCCT の型をここへ出さない(AT-ARC-001)。
//! カーネルが作った面は、識別子と、検査に使える標本と、分かっているなら面の正体で表す。
//! 実体(TopoDS_Shape)はカーネル側の表に置き、識別子で引く。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/fabrication/SurfacePatch.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <string>
#include <vector>

namespace kachakacha::v2::modeling {

using fabrication::AnalyticSurfaceInfo;
using fabrication::SurfacePatchSamples;
using geometry::Vector3;

//! カーネルの中の実体を指す番号。core はこれ以上の中身を知らない。
struct KernelShapeHandle {
    std::uint64_t value = 0;
    [[nodiscard]] bool Valid() const noexcept { return value != 0; }
};

struct GuideSurfaceResult {
    KernelShapeHandle handle;
    //! 検査と製作近似のための標本。行×列の格子。
    SurfacePatchSamples samples;
    //! カーネルが正体を知っているなら、それも返す(円筒・円錐の厳密展開に要る)。
    AnalyticSurfaceInfo analytic;
    //! 面の境界。曲線種類を保ったまま返す。
    std::vector<geometry::CurveSegment> boundary;
    //! 入力の鎖を、出来た面がどれだけ外れて通っているか。
    double maximumDeviationMm = 0.0;
    double rmsDeviationMm = 0.0;
    double areaMm2 = 0.0;
};

} // namespace kachakacha::v2::modeling
