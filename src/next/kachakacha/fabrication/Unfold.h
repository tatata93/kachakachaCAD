#pragma once

//! 展開(fabrication-contract.md §4、§9)。
//!
//! 約束は1つ。**伸縮させない**。
//! 展開図の上での長さは、立体の上での長さと一致しなければならない。
//! 一致させられない面は「展開できない」と言う。近い形へ均して成功にしない。
//!
//! 円筒と円錐は、面の正体が分かっているなら解析的に厳密展開する(AT-FAB-001/002)。
//! 正体が分からない場合は、母線で区切った帯を1枚ずつ倒して積分する。
//! このとき、帯の平面性の残差をそのまま「どれだけ展開できていないか」として返す。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/SurfacePatch.h"
#include "kachakacha/geometry/CurveSampling.h"

#include <vector>

namespace kachakacha::v2::fabrication {

using geometry::Point2;

//! 母線で区切った帯。ruling[i] は firstRail[i] と secondRail[i] を結ぶ線。
struct DevelopableStrip {
    std::vector<Vector3> firstRail;
    std::vector<Vector3> secondRail;

    [[nodiscard]] bool Valid() const
    {
        return firstRail.size() >= 2 && firstRail.size() == secondRail.size();
    }
};

struct UnfoldedStrip {
    std::vector<Point2> firstRail;
    std::vector<Point2> secondRail;
    //! 立体の上と展開図の上で、長さがどれだけ違うか(相対値)。
    //! §10.2 は 1e-6 以下を求める。
    double maximumLengthErrorRelative = 0.0;
    //! 四辺形がどれだけ平面から外れているか(mm)。
    //! これが目標偏差を超えるなら、その帯は本当は展開できない。
    double maximumPlanarityErrorMm = 0.0;
};

//! 帯を展開する。伸縮させない置き方で1枚ずつ倒す。
[[nodiscard]] base::Result<UnfoldedStrip> UnfoldStrip(const DevelopableStrip& strip,
    double targetMaxDeviationMm);

//! 標本が本当に「伸ばさずに平らにできる」形かどうかを測る。
//!
//! 縁だけを見ても分からない。中がふくらんでいても縁は一致してしまう。
//! そこで、内側の各点のまわりで三角形の角を足し、2πからのずれ(角欠損)を見る。
//! 角欠損が0であることが、伸ばさずに平らにできることと同じ意味になる
//! (離散版のGauss-Bonnet)。球はここで必ず引っかかる。
struct DevelopabilityCheck {
    double maximumAngleDefectRad = 0.0;
    //! 角欠損を、平らにしたときの実際のずれ(mm)へ換算した見積り。
    double estimatedDistortionMm = 0.0;
    std::size_t worstRow = 0;
    std::size_t worstColumn = 0;
    bool valid = false;
};

[[nodiscard]] DevelopabilityCheck CheckDevelopability(const SurfacePatchSamples& samples);

//! 面の標本を、行を母線として帯に分けて展開する。
//! 列方向を母線と見なす場合は転置して渡すこと。
[[nodiscard]] base::Result<UnfoldedStrip> UnfoldSamples(const SurfacePatchSamples& samples,
    double targetMaxDeviationMm);

//! 円筒の厳密展開。軸まわりの角度 × 半径を弧長にする。
//! 面の正体が Cylinder でなければ値を返さない。
[[nodiscard]] base::Result<std::vector<Point2>> UnfoldOnCylinder(
    const AnalyticSurfaceInfo& surface, const std::vector<Vector3>& points,
    double toleranceMm);

//! 円錐の厳密展開。頂点からの距離を半径、軸まわりの角度を縮めた扇形にする。
[[nodiscard]] base::Result<std::vector<Point2>> UnfoldOnCone(
    const AnalyticSurfaceInfo& surface, const std::vector<Vector3>& points,
    double toleranceMm);

//! 平面の展開。面内の座標へ落とすだけ。
[[nodiscard]] base::Result<std::vector<Point2>> UnfoldOnPlane(
    const AnalyticSurfaceInfo& surface, const std::vector<Vector3>& points,
    double toleranceMm);

//! 展開が長さを保っているかを測る。作ったあと必ず通す。
struct LengthPreservationCheck {
    double maximumRelativeError = 0.0;
    std::size_t worstIndex = 0;
    bool withinTolerance = true;
};

[[nodiscard]] LengthPreservationCheck CheckLengthPreservation(
    const std::vector<Vector3>& spatial, const std::vector<Point2>& flat,
    double relativeTolerance = 1.0e-6);

} // namespace kachakacha::v2::fabrication
