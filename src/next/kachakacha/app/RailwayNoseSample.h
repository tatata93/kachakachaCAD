#pragma once

//! 1/87 の流線形鉄道車両前頭部。AT-FAB-013 と配布見本で同じ形を使う。
//!
//! ER1 / 初期 ER2 の丸形前頭部を参考にした **試験用近似形状** である。
//! 幅 3520 mm だけは資料値を 1/87 にする。奥行き、曲率、窓寸法は実車寸法を
//! 名乗らず、円筒に近い腰部と二重曲率の肩を同時に試すための近似値とする。

#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/io/DocumentFile.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace kachakacha::v2::app {

inline constexpr double kRailwayNoseScaleDenominator = 87.0;
inline constexpr double kRailwayNoseWidthMm = 3520.0 / kRailwayNoseScaleDenominator;
inline constexpr double kRailwayNoseHeightMm = 3600.0 / kRailwayNoseScaleDenominator;
inline constexpr double kRailwayNosePlanBulgeMm = 6.0;
inline constexpr double kRailwayNoseShoulderRoundMm = 9.0;
inline constexpr double kRailwayNoseDiagonalMm = 60.0;

//! u=左右(0..1)、v=上下(0..1)。Y 正方向が車両前方。
[[nodiscard]] geometry::Vector3 RailwayNosePoint(double u, double v) noexcept;

[[nodiscard]] fabrication::SurfacePatchSamples BuildRailwayNoseSurfaceSamples(
    std::size_t rows = 41, std::size_t columns = 41);

//! 6枚の丸角窓と中央上部前照灯。面の上に載った閉じた曲線で返す。
[[nodiscard]] FabricationMarkings BuildRailwayNoseOpenings();

[[nodiscard]] FabricationSource BuildRailwayNoseFabricationSource();

//! V2 で実際に開ける見本。断面、ロフト面、厚み付き部品、製作モデルを含む。
[[nodiscard]] io::DocumentFile BuildRailwayNoseSampleDocument();
[[nodiscard]] base::Result<std::string> BuildRailwayNoseSampleArchive();

[[nodiscard]] std::string_view RailwayNoseSampleVersion() noexcept;

} // namespace kachakacha::v2::app
