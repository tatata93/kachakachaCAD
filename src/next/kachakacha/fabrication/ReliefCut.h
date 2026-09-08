#pragma once

//! 切れ目(fabrication-contract.md §7)。
//!
//! 二重曲率を1枚のまま吸収するための切れ目。入れ方を間違えると、
//! 部材が千切れたり、小片が脱落したり、組んだときに合わなくなる。
//! ここは「入れてよいかどうか」を全部数えて確かめる層。
//! 条件を満たせないなら、無理に入れず、別の分割を勧める。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/FabricationSettings.h"
#include "kachakacha/geometry/CurveSampling.h"

#include <string>
#include <vector>

namespace kachakacha::v2::fabrication {

using geometry::Point2;

//! 型紙の上での1本の切れ目。
struct ReliefCut {
    std::string cutId;
    //! 入口から先端までの中心線。型紙の座標。
    std::vector<Point2> centerPath;
    //! VNotch のとき、左右の側辺。StraightSlit なら空。
    std::vector<Point2> leftSide;
    std::vector<Point2> rightSide;
    ReliefShape shape = ReliefShape::StraightSlit;
    //! 左右の側辺が組立後にどれだけ離れるか。左右を勝手に詰めて合わせない。
    double mateGapMm = 0.0;
};

//! 切れ目を入れる先の部材。
struct ReliefPanel {
    std::string panelId;
    //! 型紙の上の外周。閉じた並び。
    std::vector<Point2> outline;
    //! 開口。ここへ切れ目を入れてはならない。
    std::vector<std::vector<Point2>> openings;
};

struct ReliefValidation {
    //! 通った切れ目。
    std::vector<std::string> acceptedCutIds;
    //! 一番深い切れ目の、部材幅に対する比。
    double maximumDepthRatio = 0.0;
    //! 先端から向こう側の縁までの、いちばん狭いところ。
    double minimumLigamentMm = 0.0;
    double maximumMateGapMm = 0.0;
};

//! 切れ目を検査する。§7.5 の条件をすべて見る。
//! 満たせない切れ目があれば、FAB-C001〜C006 で断る。
[[nodiscard]] base::Result<ReliefValidation> ValidateReliefCuts(const ReliefPanel& panel,
    const std::vector<ReliefCut>& cuts, const FabricationSettings& settings,
    double targetMaxDeviationMm);

//! 切れ目が必要なのに、設定で切れ目を止めている場合を見つける(FAB-C001)。
[[nodiscard]] std::optional<base::Diagnostic> CheckReliefRequired(
    const FabricationSettings& settings, bool reliefWouldBeNeeded);

} // namespace kachakacha::v2::fabrication
