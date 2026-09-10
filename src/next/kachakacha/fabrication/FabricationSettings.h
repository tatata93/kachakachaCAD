#pragma once

//! 製作近似の設定(fabrication-contract.md §3)。

#include "kachakacha/base/Diagnostic.h"

#include <string>

namespace kachakacha::v2::fabrication {

enum class FabricationStrategy {
    OnePiece,
    FewPieces,      //!< 既定
    SeparatePanels,
    Hybrid,
};

enum class BendDirection { Auto, U, V, Both };
enum class ReliefShape { Auto, StraightSlit, VNotch, CurvedVNotch };
enum class ThicknessPlacement { Outside, Centered, Inside };

//! 厚みの付け方の日本語。画面の帯と手順書で同じ言葉を使う。
[[nodiscard]] constexpr const char* ThicknessPlacementNameJa(ThicknessPlacement value) noexcept
{
    switch (value) {
    case ThicknessPlacement::Outside:  return "外側";
    case ThicknessPlacement::Centered: return "中央";
    case ThicknessPlacement::Inside:   return "内側";
    }
    return "不明";
}
enum class MaterialKind { Paper, Styrene, Brass, Other };

struct AllowedPanelTypes {
    bool planar = true;
    bool cylindrical = true;
    bool conical = true;
    bool tangentDevelopable = true;
};

struct FabricationSettings {
    FabricationStrategy strategy = FabricationStrategy::FewPieces;
    //! 1..10。大きいほど元の形に近い。
    int fidelityLevel = 5;
    //! 指定があればスライダー換算より優先する(§3.1)。
    std::optional<double> explicitMaxDeviationMm;
    int panelCountLimit = 24;
    double minimumPanelWidthMm = 1.0;
    BendDirection preferredBendDirection = BendDirection::Auto;
    AllowedPanelTypes allowedPanelTypes;
    bool reliefCutsEnabled = true;
    BendDirection reliefDirection = BendDirection::Auto;
    ReliefShape reliefShape = ReliefShape::Auto;
    double maximumReliefDepthRatio = 0.55;
    double minimumLigamentMm = 0.5;
    //! 常に true。UIで無効にしてはならない(§3)。
    bool preserveOpenings = true;
    double outputThicknessMm = 0.20;
    ThicknessPlacement thicknessPlacement = ThicknessPlacement::Centered;
    MaterialKind material = MaterialKind::Paper;
};

//! 再現度スライダーから目標最大偏差を出す(§3.1)。
//!
//!   q      = (level - 1) / 9
//!   coarse = max(0.50mm, D * 0.010)
//!   fine   = max(0.03mm, D * 0.0005)
//!   目標   = exp(lerp(log(coarse), log(fine), q))
//!
//! 明示指定があればそれを返す。
[[nodiscard]] double ResolveTargetMaxDeviationMm(const FabricationSettings& settings,
    double modelDiagonalMm);

//! 設定そのものの検査。範囲外を黙って丸めない。
[[nodiscard]] std::vector<base::Diagnostic> ValidateFabricationSettings(
    const FabricationSettings& settings);

} // namespace kachakacha::v2::fabrication
