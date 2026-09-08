#pragma once

//! 部材に厚みを付けて立体にする(fabrication-contract.md §11、AT-FAB-011)。
//!
//! 固定した状態の部材は、型紙の上では線だが、部品として出すには厚みが要る。
//! 厚みの付け方(外側・中央・内側)で、出来る立体の位置が変わる。
//! 実物では、外側に付けるか内側に付けるかで 0.2mm ずれる。
//! 1/87 では 0.2mm は実車の 17mm にあたるので、無視できない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/FreezeState.h"
#include "kachakacha/geometry/GeometryTolerance.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

#include <vector>

namespace kachakacha::v2::kernel {

//! 厚みを付けられなかった。
inline constexpr const char* kPanelSolidFailed = "FAB-E002";

struct BuiltPanelSolid {
    modeling::KernelShapeHandle handle;
    std::string panelId;
    double volumeMm3 = 0.0;
    //! 実際に付いた厚み。指定と一致する。
    double thicknessMm = 0.0;
};

//! 部材1枚を立体にする。
[[nodiscard]] base::Result<BuiltPanelSolid> BuildPanelSolid(
    const fabrication::PanelSolidRequest& request,
    const geometry::GeometryTolerance& tolerance);

//! 束の部材をまとめて立体にする。1枚でも作れなければ全体を断る。
[[nodiscard]] base::Result<std::vector<BuiltPanelSolid>> BuildPanelSolids(
    const fabrication::FreezeBundle& bundle,
    const geometry::GeometryTolerance& tolerance);

} // namespace kachakacha::v2::kernel
