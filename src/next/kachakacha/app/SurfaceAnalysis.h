#pragma once

//! 面の解析の表示(プロンプト surface_analysis)。色の決め方と、製作性の診断の言葉。
//!
//!   ゼブラ         面の法線から縞を出す(反射の縞)。縞が切れる = 折れ目(G0)、
//!                  縞が折れる = 曲がり方の段差(G1)、なめらか = G2
//!   平均曲率       青(へこみ)― 白 ― 赤(ふくらみ)
//!   ガウス曲率     青(鞍形・K<0)― 白(K=0・可展)― 赤(椀形・K>0)
//!   U/V 線         面の格子の線
//!   曲率コーム     縁に沿って、曲率の大きさを歯の長さで出す
//!   境目の連続     縁ごとに隣の面とのつながりを G0/G1/G2 で色分け
//!   入力線からのずれ 面を作った線ごとに、面からの離れを色分け
//!
//! **製作性はここで断定しない。** ガウス曲率から「平らに広げるのに要る伸び縮みの目安」を
//! 出し、数値の基準で 3 つに分けるが、板厚・近似・許容差・部材の分け方でも変わる
//! 診断材料として出す(K = 0 だから必ず作れる、とは言わない)。

#include "kachakacha/app/Rgb.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/SurfaceAnalysisData.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kachakacha::v2::app {

enum class SurfaceAnalysisMode {
    None,
    Zebra,
    MeanCurvature,
    GaussianCurvature,
    IsoCurves,
    CurvatureComb,
    Continuity,
    Deviation,
};

[[nodiscard]] std::string_view SurfaceAnalysisModeLabelJa(SurfaceAnalysisMode mode) noexcept;
[[nodiscard]] const std::vector<SurfaceAnalysisMode>& SurfaceAnalysisModes();
//! 面を塗り替える解析か(ゼブラ・曲率)。線を重ねるだけのものは偽。
[[nodiscard]] bool AnalysisPaintsSurface(SurfaceAnalysisMode mode) noexcept;

//! 青 ― 白 ― 赤。value / scale を −1〜1 に丸めて塗る(0 が白)。
[[nodiscard]] Rgb DivergingColor(double value, double scale) noexcept;

//! ゼブラの縞。面の法線と、見ている向き・上の向きから、暗い縞か。
//! 縞は「法線が見る向きのまわりにどれだけ傾いているか」で決まる(反射の縞と同じ振る舞い)。
[[nodiscard]] bool ZebraDark(const geometry::Vector3& normal, const geometry::Vector3& viewDirection,
    const geometry::Vector3& upDirection, int stripes) noexcept;

//! 塗りの目盛り(両側の端)。曲率の最大の絶対値。0 なら小さな既定値を返す(全部白になる)。
[[nodiscard]] double CurvatureScale(const modeling::SurfaceAnalysisData& data, bool gaussian);
//! 何枚かを同じ目盛りで塗る(面どうしの曲がり方を見比べられる)。いちばん大きい目盛りを使う。
[[nodiscard]] double CurvatureScale(const std::vector<const modeling::SurfaceAnalysisData*>& all,
    bool gaussian);

// ---------------------------------------------------------------- 製作性(可展性)

enum class DevelopabilityClass {
    NearlyDevelopable,
    DoubleCurved,
    StronglyDoubleCurved,
};

//! 区分の数値基準。伸び縮みの目安 ε ≈ |K|·a²/6(a = 面の大きさの半分)。
//! 半径 R の球の帽子(半径 a)を平らにすると、縁の周がおよそ a²/(6R²) の割合で足りなくなる。
struct DevelopabilityThresholds {
    //! これ未満は「ほぼ可展」(0.1 %)。
    double nearlyDevelopableStrain = 0.001;
    //! これ以上は「強い二重曲率」(1 %)。
    double stronglyDoubleCurvedStrain = 0.01;
};

struct DevelopabilitySummary {
    DevelopabilityClass kind = DevelopabilityClass::NearlyDevelopable;
    double maximumAbsGaussian = 0.0;
    double sizeMm = 0.0;
    //! 伸び縮みの目安(割合。0.001 = 0.1 %)。
    double strain = 0.0;
    std::string labelJa;
    //! 基準と、断定しないことの一文。
    std::string explanationJa;
};

[[nodiscard]] DevelopabilitySummary ClassifyDevelopability(
    const modeling::SurfaceAnalysisData& data, const DevelopabilityThresholds& thresholds = {});

//! 何枚かのうち、平らに広げにくい(伸び縮みの目安が大きい)面の製作性。空なら labelJa が空。
[[nodiscard]] DevelopabilitySummary WorstDevelopability(
    const std::vector<const modeling::SurfaceAnalysisData*>& all,
    const DevelopabilityThresholds& thresholds = {});

[[nodiscard]] std::string_view DevelopabilityLabelJa(DevelopabilityClass kind) noexcept;

// ---------------------------------------------------------------- 境目の連続

enum class ContinuityGrade {
    //! 隣の面が無い(開いた縁)。
    Open,
    //! 離れている(G0 でもない)。
    Gap,
    G0,
    G1,
    G2,
};

//! 離れ・折れ目・曲率の差から段階を決める(G1 の許容 1.5 度、G2 の許容 0.1 /mm、離れ 0.01 mm)。
[[nodiscard]] ContinuityGrade GradeContinuity(const modeling::EdgeContinuitySample& sample) noexcept;
[[nodiscard]] std::string_view ContinuityGradeLabelJa(ContinuityGrade grade) noexcept;
[[nodiscard]] Rgb ContinuityGradeColor(ContinuityGrade grade) noexcept;

// ---------------------------------------------------------------- 入力線からのずれ

//! 通す許容(exactMm)以内は緑、近づける許容(approximateMm)以内は黄、それを超えると赤。
[[nodiscard]] Rgb DeviationColor(double distanceMm, double exactMm, double approximateMm) noexcept;

//! 棚に出す、いまの解析の読み方(凡例と要約)。
[[nodiscard]] std::vector<std::string> AnalysisLegendJa(SurfaceAnalysisMode mode,
    const modeling::SurfaceAnalysisData& data);
//! 何枚かまとめて塗るときの読み方。目盛りは全部で共通、製作性はいちばん作りにくい面で言う。
[[nodiscard]] std::vector<std::string> AnalysisLegendJa(SurfaceAnalysisMode mode,
    const std::vector<const modeling::SurfaceAnalysisData*>& all);

} // namespace kachakacha::v2::app
