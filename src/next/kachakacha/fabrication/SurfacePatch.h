#pragma once

//! 製作近似が受け取る面の形(fabrication-contract.md §4、§5.1)。
//!
//! ここが V2 仕様の穴だったところ。元の仕様は
//! 「FabricationInputGeometry は点の標本を持つ」としか書いていなかったが、
//! AT-FAB-001/002 は円筒と円錐を *厳密に* 展開することを求めている。
//! 点の標本だけでは厳密展開はできない。そこで、カーネルが面の正体を知っている場合は
//! `AnalyticSurfaceInfo` としてそれも一緒に渡す(pre-implementation-fixes.md B-2)。
//!
//! 表示用の三角形は、パネルの編集単位にも型紙の正本にもしない(§4)。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/Vector3.h"

#include <optional>
#include <vector>

namespace kachakacha::v2::fabrication {

using geometry::Vector3;

//! カーネルが分かっている面の正体。分からなければ Unknown のまま渡す。
enum class AnalyticSurfaceKind {
    Unknown,
    Plane,
    Cylinder,
    Cone,
    Sphere,
    Torus,
};

struct AnalyticSurfaceInfo {
    AnalyticSurfaceKind kind = AnalyticSurfaceKind::Unknown;
    Vector3 origin{};      //!< 平面の点、円筒/円錐の軸上の点、球/トーラスの中心
    Vector3 axis{0.0, 0.0, 1.0};
    Vector3 reference{1.0, 0.0, 0.0};   //!< 軸に垂直な基準方向。角度の0度
    double radiusMm = 0.0;              //!< 円筒半径、球半径、トーラス主半径
    double secondaryRadiusMm = 0.0;     //!< トーラス管半径
    double halfAngleRad = 0.0;          //!< 円錐の半頂角
};

//! 面を格子状に標本化したもの。行が母線方向、列がもう一方。
//! points[row * columnCount + column]。
struct SurfacePatchSamples {
    std::vector<Vector3> points;
    std::size_t rowCount = 0;
    std::size_t columnCount = 0;

    [[nodiscard]] const Vector3& At(std::size_t row, std::size_t column) const
    {
        return points[row * columnCount + column];
    }
    [[nodiscard]] bool Valid() const
    {
        return rowCount >= 2 && columnCount >= 2
            && points.size() == rowCount * columnCount;
    }
};

//! 面の一部だけを使う(V1 の板材の「範囲」、plate_range)。u は列方向、v は行方向の 0〜1。
//! 同じ格子の数で、範囲の中を双一次で読み直す。範囲が 0〜1 全体ならそのまま返す。
//! 範囲が壊れていれば FAB-M004 で断る(最小 >= 最大、0〜1 の外、数でない)。
[[nodiscard]] base::Result<SurfacePatchSamples> CropSamples(const SurfacePatchSamples& samples,
    double uMin, double uMax, double vMin, double vMax);

//! 近似の入力になる1枚の面。
struct FabricationSurfacePatch {
    SurfacePatchSamples samples;
    AnalyticSurfaceInfo analytic;
    //! 標本化したときの実測偏差。§5.1「目標最大偏差の1/5以下」を確かめるのに使う。
    double samplingDeviationMm = 0.0;
    //! この面の境界(型紙の外周になる)。曲線種類を保ったまま持つ。
    std::vector<geometry::CurveSegment> boundary;
    //! 開口。消してはならない(§8)。
    std::vector<std::vector<geometry::CurveSegment>> openings;
};

} // namespace kachakacha::v2::fabrication
