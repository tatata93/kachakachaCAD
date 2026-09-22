#pragma once

//! 部品(立体)を動かす・回す・鏡に写す(P-18、部品モードの「配置」)。
//!
//! 形は **元の部品の形に変換を掛けて** 作る。文書には変換(移動量・軸・鏡の面)だけを残し、
//! 開き直したときは元の部品を作り直してから同じ変換を掛ける(形そのものは持たない)。
//!
//! 約束。剛体の変換なので **体積は変わらない。** 変わったら(鏡で裏返って体積が負に
//! なるのを直せなかった等)、作れたことにしない。
//!
//! OCCT の型を外へ出さない。返すのは core の型だけ。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

namespace kachakacha::v2::kernel {

//! 変換を掛けられなかった。
inline constexpr const char* kPlaceFailed = "KER-P001";
//! 元の部品の形が見つからない。
inline constexpr const char* kPlaceSourceMissing = "KER-P002";
//! 変換のあと体積が変わった(剛体の変換になっていない)。
inline constexpr const char* kPlaceVolumeChanged = "KER-P003";
//! カーネルが入っていない版。
inline constexpr const char* kPlaceUnsupported = "KER-P004";
//! 変換の中身が読めない(長さ 0 の軸・鏡の法線など)。
inline constexpr const char* kPlaceBadTransform = "KER-P005";

enum class ShapeTransformKind {
    Translate,   //!< 移動・複製・並べる
    Rotate,      //!< 回転(軸と角度)
    Mirror,      //!< 鏡映(面)
};

struct ShapeTransform {
    ShapeTransformKind kind = ShapeTransformKind::Translate;
    //! 移動量 / 回転の軸の向き / 鏡の面の法線。
    geometry::Vector3 vector{};
    //! 回転の軸上の点 / 鏡の面上の点(移動では使わない)。
    geometry::Vector3 point{};
    //! 回転角(ラジアン)。
    double angleRad = 0.0;
};

struct ShapeTransformResult {
    modeling::KernelShapeHandle handle;
    double volumeMm3 = 0.0;
};

//! 元の部品の形に変換を掛けた、新しい形を作る(元の形は変えない)。
[[nodiscard]] base::Result<ShapeTransformResult> TransformShape(
    modeling::KernelShapeHandle source, const ShapeTransform& transform);

} // namespace kachakacha::v2::kernel
