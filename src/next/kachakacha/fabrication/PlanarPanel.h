#pragma once

//! 平らな部品を、そのまま型紙にする(fabrication-contract §9)。
//!
//! プラ板から作るとき、いちばん多いのは「平らな板を切って組む」である。
//! 平らな面は展開の必要がない。**そのまま型紙になる。**
//! 曲がった面は展開が要るので、ここでは扱わない。
//! 扱えるふりをして近似すると、切ってから合わないことに気づく。
//!
//! ここでやるのは3つ。
//!   1. 部品の境界の線が、1つの平面に載っているかを調べる
//!   2. 載っていれば、その平面の上の2D座標へ落とす
//!   3. 型紙の部材(PatternPanel)にする
//!
//! 落とすときに寸法は変えない。平面上の距離をそのまま mm として使う。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/exporters/PatternExport.h"
#include "kachakacha/fabrication/PatternLayout.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <string>
#include <vector>

namespace kachakacha::v2::fabrication {

//! 型紙にしたい部品1つぶん。
struct PlanarPanelRequest {
    //! 部材の名前。型紙に載る番号のもとになる。
    std::string panelId;
    //! 外周の線。順不同でよい。1つの平面に載っていること。
    std::vector<geometry::CurveSegment> boundary;
    //! 開口(窓など)の線。外周と同じ平面に載っていること。
    std::vector<std::vector<geometry::CurveSegment>> openings;
};

//! 平らかどうかの見立て。
struct PlanarityCheck {
    bool planar = false;
    //! 平面からいちばん離れている点の距離(mm)。
    double maxDeviationMm = 0.0;
    geometry::Vector3 normal{};
    geometry::Vector3 origin{};
};

//! 線の集まりが1つの平面に載っているか。載っていなければ、どれだけ外れているかを返す。
[[nodiscard]] PlanarityCheck CheckPlanar(
    const std::vector<geometry::CurveSegment>& curves, double toleranceMm);

//! 平らな部品を型紙の部材にする。
//!
//! 平らでなければ断る。曲がった面は展開が要る。
//! ここで近似すると、切ってから合わないことに気づくことになる。
[[nodiscard]] base::Result<PatternPanel> BuildPlanarPanel(const PlanarPanelRequest& request,
    double toleranceMm);

//! まとめて作る。1つでも作れなければ、そこで断る。
[[nodiscard]] base::Result<std::vector<PatternPanel>> BuildPlanarPanels(
    const std::vector<PlanarPanelRequest>& requests, double toleranceMm);

//! 型紙の部材を、原寸のまま紙の上の曲線へ戻す。書き出しはこれを使う。
//! 置き場所(PatternPlacement)は回転と平行移動だけなので、寸法は変わらない。
[[nodiscard]] base::Result<std::vector<exporters::PatternCurve>> PlacePanelCurves(
    const PatternPanel& panel, const PatternPlacement& placement);

} // namespace kachakacha::v2::fabrication
