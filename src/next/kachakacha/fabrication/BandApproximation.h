#pragma once

//! 帯近似 ── V1 方式(docs/surface-unfolding-spec.md、V1 の model/PartModel)。
//!
//! 曲面を「1部材 = 基本1軸曲げ」の帯へ **近似し直す**。
//! V2 のもう一方の方式(CurvedPanel / PanelStrategy)は、面が既にあることを前提に
//! 展開できるかを検査して、できなければ断る。こちらは断らずに、
//! 許容偏差に収まるまで帯を細かく切り、収まらなければどれだけずれたかを報告する。
//! 二重曲面(球のような面)を板材の曲げで作るには、こちらが要る。
//!
//! 入力は `SurfacePatchSamples`(UV の格子)。V1 の `PartSource::Evaluate(u,v)` と
//! 同じ役目を、格子の双一次補間で果たす。格子そのものの標本化誤差は
//! `FabricationSurfacePatch::samplingDeviationMm` が別に持つ。
//!
//! ここは近似と展開まで。曲げ具合(0〜1)で形を動かすのは BandFold.h にある。
//! OCCT を使わない。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/fabrication/SurfacePatch.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/Vector3.h"

#include <string_view>
#include <vector>

namespace kachakacha::v2::fabrication {

using geometry::Point2;
using geometry::Vector3;

inline constexpr const char* kBandBadOptions = "FAB-B001";
inline constexpr const char* kBandBadBoundary = "FAB-B002";
inline constexpr const char* kBandBadSamples = "FAB-B003";
inline constexpr const char* kBandBadProgress = "FAB-B004";

//! 分割軸。境界をどちらのパラメータ方向に置くか。
enum class BandSplitAxis {
    U, //!< u = 一定 の線で切る(帯は v に沿って並ぶ)
    V, //!< v = 一定 の線で切る
};

[[nodiscard]] std::string_view BandSplitAxisNameJa(BandSplitAxis axis) noexcept;

//! 近似の決め方。V1 の PartApproximationOptions と同じ。
struct BandApproximationOptions {
    BandSplitAxis splitAxis = BandSplitAxis::V;
    //! true なら許容偏差から自動で切る。false なら manualBoundaries を境界にする。
    bool automaticBoundaries = true;
    //! 1軸曲げ近似からの許容偏差(mm)。自動分割の基準。
    double maximumDeviationMm = 0.25;
    //! 自動分割の上限部材数。超えたら公差より部材数を優先し、偏差は結果で報告する。
    int maximumPartCount = 12;
    //! 部材の最小実幅(mm)。これ未満の細い帯は作らない。
    double minimumPartWidthMm = 4.0;
    //! 手動境界(分割軸のパラメータ、0 と 1 の間)。
    std::vector<double> manualBoundaries;
};

//! 格子の標本を、連続なパラメータ面として読む。V1 の PartSource に当たる。
class SampledSurface {
public:
    explicit SampledSurface(const SurfacePatchSamples& samples);
    //! u は列方向(0..1)、v は行方向(0..1)。格子の中は双一次で補間する。
    [[nodiscard]] Vector3 Evaluate(double u, double v) const;
    //! 分割軸のパラメータ t と、直交方向 s で読む。
    [[nodiscard]] Vector3 EvaluateSplit(BandSplitAxis axis, double t, double s) const;
    [[nodiscard]] bool Valid() const noexcept { return samples_.Valid(); }

private:
    SurfacePatchSamples samples_;
};

//! 近似で出来た帯1つ。
struct ApproximatedBand {
    int number = 1;                //!< 型紙・一覧に出す部材番号(1始まり)
    double minimumParameter = 0.0; //!< 分割軸方向の範囲(0..1)
    double maximumParameter = 1.0;
    double widthMm = 0.0;          //!< 分割軸方向の実幅(平均)
    double estimatedDeviationMm = 0.0; //!< 1軸曲げ近似からの推定偏差(最大)
    bool planar = false;           //!< 偏差がほぼ0なら平ら
};

struct BandApproximationResult {
    std::vector<ApproximatedBand> bands;
    double maximumDeviationMm = 0.0;  //!< 全帯の推定偏差の最大
    bool reachedRequestedTolerance = true; //!< 全帯が許容偏差以下か
    //! 帯の境目(昇順、帯数+1)。展開と折りに渡す。
    std::vector<double> railParameters;
};

//! 面を帯へ近似分割する。V1 の ApproximatePlateParts。
//!
//! 貪欲法: 偏差が許容内に収まる限り帯を伸ばす。最小幅を満たすまでは、
//! 偏差を超えても伸ばす(公差超過よりも「作れない細さ」を避ける)。
//! 上限部材数を超えたら等分割へ切り替え、偏差は結果で報告する。断らない。
[[nodiscard]] base::Result<BandApproximationResult> ApproximateBands(
    const SampledSurface& source, const BandApproximationOptions& options);

//! 帯の境界(分割軸パラメータ = 一定の線)を、面の上の点列として作る。
[[nodiscard]] std::vector<Vector3> BuildBandBoundary(const SampledSurface& source,
    BandSplitAxis axis, double parameter, int samples = 64);

//! 帯メッシュ(近似の実形状)と、その厳密展開。V1 の PartMeshDevelopment。
//!
//! rows = レール本数(帯数+1)。各レールは columns 個の点列。
//! world は角ばった近似形状(レール間は直線 = ルールド)、developed はその等長展開。
//! 展開は三角形単位で辺長を厳密に保存する(標準的なペーパークラフト展開)。
struct BandMesh {
    int rows = 0;
    int columns = 0;
    //! [row][column]。row0 が範囲の始まり側。
    std::vector<std::vector<Vector3>> world;
    std::vector<std::vector<Point2>> developed;
    //! 内部レール(折り線)の山谷。サイズ rows-2。+1=山、-1=谷、0=ほぼ平ら。
    std::vector<int> creaseDirections;

    [[nodiscard]] int BandCount() const noexcept { return rows > 0 ? rows - 1 : 0; }
    [[nodiscard]] int CreaseCount() const noexcept { return rows > 1 ? rows - 2 : 0; }
};

//! レールの並びで区切ってメッシュ展開する。V1 の DevelopPartMesh。
[[nodiscard]] base::Result<BandMesh> DevelopBandMesh(const SampledSurface& source,
    BandSplitAxis axis, const std::vector<double>& railParameters, int columns = 96);

//! 閉じた輪郭(開口の窓など)を、帯ごとに切り出す。V1 の ClipClosedLoopIntoBands。
//!
//! 境目をまたぐ穴を「区間ごと」に切ると、3つ以上の帯にまたがる窓では真ん中の帯の
//! 取り分が細い三角2つに割れ、窓の中央が切り抜かれずに板が残る(=謎の面)。
//! 帯の範囲で多角形として切り出す。V1 でオーナーが報告した不具合の対策そのもの。
//!
//! points と parameters は同じ長さで、輪郭を一周する順(終点に始点を重ねない)。
//! parameters は分割方向の位置。boundaries は帯の境目(昇順、帯数+1本)。
struct BandLoopPiece {
    int band = 0;
    std::vector<Vector3> points; //!< 閉じた輪郭(始点=終点は含めない)
};

[[nodiscard]] std::vector<BandLoopPiece> ClipLoopIntoBands(
    const std::vector<Vector3>& points, const std::vector<double>& parameters,
    const std::vector<double>& boundaries);

} // namespace kachakacha::v2::fabrication
