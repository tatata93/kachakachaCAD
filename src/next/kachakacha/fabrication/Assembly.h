#pragma once

//! 組立スライダー(fabrication-contract.md §10)。
//!
//! いちばん大事な決まり: **頂点を個別に補間しない**(§10.1)。
//! V1はそれをやっていたので、途中の状態で辺の長さが変わり、しわが出た。
//!
//! ここでは、動くものをすべて「剛体の板」と「その間のヒンジ」に還元する。
//! 曲げる板も、母線ごとに区切った細い剛体の帯の連なりとして扱う。
//! こうすると、辺の長さが変わらないことが *作りとして* 保証される。
//! 検査で見つけて直すのではなく、そもそも変わりようがない形にする。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/Vector3.h"

#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::fabrication {

using geometry::Point2;
using geometry::Vector3;

//! 型紙の上での1枚。座標は型紙の座標系(mm)。
struct AssemblyPanel {
    std::string panelId;
    //! 型紙の上の輪郭。閉じた並び。
    std::vector<Point2> flatOutline;
    //! 曲げる板のとき、母線の位置(輪郭を横切る直線の u 座標)。
    //! 空なら平らな板として、剛体のまま動く。
    std::vector<double> rulingUCoordinates;
    //! 母線1本ごとに、100%のときに何ラジアン曲がるか。
    //! 母線がヒンジになるので、rulingUCoordinates と同じ数だけ必要。
    std::vector<double> targetBendAngleRad;
};

//! 板と板をつなぐ折り線。
struct AssemblyFold {
    std::string foldId;
    std::string parentPanelId;
    std::string childPanelId;
    //! 折り線の両端。型紙の座標で、親と子の両方の輪郭上にある。
    Point2 hingeFrom{};
    Point2 hingeTo{};
    double targetAngleRad = 0.0;
    //! 個別指定。無ければマスターに従う(§10.4)。
    std::optional<double> progressPercentOverride;
};

struct AssemblyState {
    double masterPercent = 0.0;
};

//! 3次元へ置いた1枚。
struct PlacedPanel {
    std::string panelId;
    //! 型紙の輪郭と同じ並び。
    std::vector<Vector3> outline;
    //! 曲げる板は、細い帯ごとに分かれた形も持つ。
    std::vector<std::vector<Vector3>> strips;
};

struct AssemblyResult {
    std::vector<PlacedPanel> panels;
    std::vector<base::Diagnostic> notes;
};

//! 指定した状態へ組み立てる。
[[nodiscard]] base::Result<AssemblyResult> EvaluateAssembly(
    const std::vector<AssemblyPanel>& panels, const std::vector<AssemblyFold>& folds,
    const AssemblyState& state);

//! §10.2 のしわ防止条件。作ったあと必ず通す。
struct AssemblyMetricCheck {
    double maximumEdgeLengthChangeRelative = 0.0;
    double maximumScaleError = 0.0;
    std::size_t flippedTriangleCount = 0;
    std::string worstPanelId;
    bool withinTolerance = true;
    //! 許容差を超えたときの診断(FAB-A003 MetricDistortion)。
    std::vector<base::Diagnostic> diagnostics;
};

[[nodiscard]] AssemblyMetricCheck CheckAssemblyMetric(
    const std::vector<AssemblyPanel>& flat, const AssemblyResult& placed);

//! 板どうしがぶつかっていないか。見つけたら黙らずに知らせる(§10.2)。
struct AssemblyIntersectionReport {
    struct Hit {
        std::string firstPanelId;
        std::string secondPanelId;
        double distanceMm = 0.0;
        Vector3 position{};
    };
    std::vector<Hit> hits;
    //! ぶつかりを見つけたときの診断(FAB-A002 AssemblySelfIntersection)。
    std::vector<base::Diagnostic> diagnostics;
};

[[nodiscard]] AssemblyIntersectionReport FindAssemblyIntersections(
    const AssemblyResult& placed, const std::vector<AssemblyFold>& folds,
    double toleranceMm);

} // namespace kachakacha::v2::fabrication
