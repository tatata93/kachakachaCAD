#pragma once

//! 部品どうしの足し引き(ブール演算、WP-09)。
//!
//! 押し出しにも「足す・引く」はあるが、あれは押し出した形を相手へ入れるものである。
//! ここは、**すでにある部品2つ**を足したり引いたりする。
//!
//! 大事な約束が2つ。
//!   1. **何も変わらなかったら、成功したことにしない。**
//!      触れていない2つを足しても、体積は変わらない。
//!      それを「足しました」と言うのが V1 の悪癖だった。
//!   2. **何も残らなかったら断る。** 全部引き切ると、部品が消える。
//!      消えた部品を持ち続けると、選べるのに何も無いものが一覧に並ぶ。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"

namespace kachakacha::v2::kernel {

//! 足し引きが行えなかった。
inline constexpr const char* kBooleanFailed = "KER-B001";
//! 相手が見つからない。
inline constexpr const char* kBooleanTargetMissing = "KER-B002";
//! 触れていないので何も変わらない。
inline constexpr const char* kBooleanNoChange = "KER-B003";
//! 引き切って何も残らない。
inline constexpr const char* kBooleanNothingLeft = "KER-B004";
//! カーネルが入っていない版。
inline constexpr const char* kBooleanUnsupported = "KER-B005";

enum class BooleanOperation {
    Union,       //!< 足す
    Difference,  //!< 引く
};

struct BooleanBuildResult {
    modeling::KernelShapeHandle handle;
    double volumeMm3 = 0.0;
    //! 足し引きの前の、1つ目の体積。どれだけ変わったかを言うために持つ。
    double previousVolumeMm3 = 0.0;
};

//! 2つの部品を足す/引く。first が土台で、second が相手である。
[[nodiscard]] base::Result<BooleanBuildResult> BuildBoolean(BooleanOperation operation,
    modeling::KernelShapeHandle first, modeling::KernelShapeHandle second,
    double toleranceMm);

} // namespace kachakacha::v2::kernel
