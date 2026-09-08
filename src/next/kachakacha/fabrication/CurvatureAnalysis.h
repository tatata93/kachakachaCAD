#pragma once

//! 面の曲がり方を測って、展開できる形かどうかを決める(fabrication-contract.md §4)。
//!
//! ここが「勝手に三角形へ割らない」ための入口。
//! Gauss曲率が許容を超える領域を、そのまま1枚の展開可能パネルとして登録してはならない。
//! 超えているなら、境界の変更・切れ目・追加分割のどれかで吸収する。
//! 黙って三角形へ割った結果を完成として出さない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/SurfacePatch.h"

#include <vector>

namespace kachakacha::v2::fabrication {

enum class PanelGeometryClass {
    Planar,             //!< 両主曲率が0
    Cylindrical,        //!< 片方が0、もう片方が一定
    Conical,            //!< 片方が0、もう片方が母線方向へ一次に変わる
    TangentDevelopable, //!< Gauss曲率が0だが上のどれでもない
    DoubleCurved,       //!< Gauss曲率が0でない。1枚では展開できない
};

[[nodiscard]] std::string_view PanelGeometryClassNameJa(PanelGeometryClass value) noexcept;

//! 標本1点ぶんの曲がり方。
struct CurvatureSample {
    std::size_t row = 0;
    std::size_t column = 0;
    double firstPrincipal = 0.0;    //!< 絶対値の大きいほう
    double secondPrincipal = 0.0;
    double gaussian = 0.0;
    double mean = 0.0;
    bool valid = false;
};

struct CurvatureAnalysis {
    std::vector<CurvatureSample> samples;
    PanelGeometryClass classification = PanelGeometryClass::Planar;
    double maximumAbsoluteGaussian = 0.0;
    double maximumAbsolutePrincipal = 0.0;
    //! 展開できない領域(Gauss曲率が許容を超えた標本)の割合。
    double doubleCurvedRatio = 0.0;
    //! 一番曲がっている場所。分割や切れ目の候補になる。
    std::size_t worstRow = 0;
    std::size_t worstColumn = 0;
};

//! Gauss曲率をどこまで0とみなすか。
//!
//! 目標最大偏差 d と面の代表長さ L から決める。
//! 幅 L の帯が Gauss曲率 K を持つとき、無理に平らにしたときの誤差はおおよそ
//! K * L^3 / 8 の程度になる。これが d を超えないことを条件にする。
[[nodiscard]] double GaussianToleranceFor(double targetMaxDeviationMm,
    double representativeLengthMm);

//! 標本から曲がり方を測る。格子が壊れていれば値を返さない。
[[nodiscard]] base::Result<CurvatureAnalysis> AnalyzeCurvature(
    const SurfacePatchSamples& samples, double targetMaxDeviationMm);

} // namespace kachakacha::v2::fabrication
