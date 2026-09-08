#pragma once

//! パネルをまたぐ開口(fabrication-contract.md §8.2)。
//!
//! V1がいちばん派手に壊れたところ。窓が部材の境目をまたぐと、
//! 切り口が「境目に沿った1本の直線」になり、曲がった面の上で弦になって形が崩れた。
//! さらに、切り取った断片の点が面から少し外れていると断片ごと捨てられ、
//! 窓が丸ごと消えることもあった。
//!
//! ここでの約束:
//!   - 開口を消さない。消して成功にしない。
//!   - 切り口を1本の直線で済ませない。境目に沿って面の上を辿る。
//!   - 断片は同じ `OpeningJunctionId` で結び、組立100%で元の開口へ戻ることを確かめる。
//!   - 丸窓を多角形へ置き換えない。曲線のまま持つか、明示した偏差以内で近似する。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <string>
#include <vector>

namespace kachakacha::v2::fabrication {

using geometry::Vector3;

//! パネルの領域。境目は「面の上を通る線」として与える。
//! 直線1本で代用しない(ここがV1の壊れどころ)。
struct PanelRegion {
    std::string panelId;
    //! この領域を囲む線。閉じていること。面の上を通る点列で持つ。
    std::vector<Vector3> boundary;
};

//! 切り分けたあとの、1つのパネルに属する開口の断片。
struct OpeningPiece {
    std::string panelId;
    //! 断片の輪郭。開口の元の線と、境目に沿った切り口が交互に並ぶ。
    std::vector<Vector3> outline;
    //! 境目に沿った切り口の区間(outline の添字の範囲)。型紙では別レイヤーにする。
    struct Junction {
        std::size_t firstIndex = 0;
        std::size_t lastIndex = 0;
    };
    std::vector<Junction> junctions;
    //! 開口が境目を横切る点。隣の断片と同じIDになる(§8.2 の OpeningJunctionId)。
    //! entry は outline の先頭、exit は切り口が始まる点。
    std::string entryJunctionId;
    std::string exitJunctionId;
    bool closed = true;
};

struct OpeningClipResult {
    std::vector<OpeningPiece> pieces;
    //! 元の開口の周長。断片の合計と突き合わせる。
    double originalPerimeterMm = 0.0;
    double totalPieceBoundaryMm = 0.0;
    std::vector<base::Diagnostic> notes;
};

//! 開口を、パネルの領域ごとに切り分ける。
//!
//! `maximumStepMm` は切り口を辿るときの刻み。曲がった面の上で弦にしないために、
//! 境目に沿って細かく点を置く。V1はここを1本の直線で済ませていた。
[[nodiscard]] base::Result<OpeningClipResult> ClipOpeningAcrossPanels(
    const std::vector<Vector3>& opening, const std::vector<PanelRegion>& panels,
    double maximumStepMm, double toleranceMm);

//! 切り分けた断片を組み立て直したときに、元の開口へ戻るかを確かめる(§8.2)。
struct OpeningClosureCheck {
    double maximumGapMm = 0.0;
    double perimeterDifferenceMm = 0.0;
    bool closed = true;
};

[[nodiscard]] OpeningClosureCheck CheckOpeningClosure(const OpeningClipResult& result,
    double toleranceMm);

} // namespace kachakacha::v2::fabrication
