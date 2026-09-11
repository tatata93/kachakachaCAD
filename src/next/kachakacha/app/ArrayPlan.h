#pragma once

//! 配列(並べて複製する。棚卸し B-2)。
//!
//! 車体には窓が10個並ぶ。1つずつ複製して位置を打つと、10回打ち間違える機会がある。
//! **等間隔に並べる** ことそのものを1つの操作にすれば、間隔は1つの数で決まる。
//!
//! ここが決めるのは **どこへ何個置くか** だけである。
//! 実際に複製するのは既にある「コピー」(TransformWire)の繰り返しで、
//! 新しい作り方は増やさない。増やすと、保存・読み込み・再計算を全部二重に作ることになる。
//!
//! 直線配列は「間隔」でも「全体の長さ」でも指定できる。
//! V1 の窓割りは「端から端まで L の間に n 個」と決まることが多く、
//! 間隔だけだと毎回 L/(n-1) を手で割ることになる。
//!
//! 断られかた:
//! - UI-A001 並べる数は 2 以上にしてください。
//! - UI-A002 並べる数が多すぎます。
//! - UI-A003 並べる向きが決まりません。
//! - UI-A004 回す角度が 0 です。
//! - UI-A005 回す軸が決まりません。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/Vector3.h"

#include <vector>

namespace kachakacha::v2::app {

using geometry::Vector3;

inline constexpr const char* kArrayCountTooSmall = "UI-A001";
inline constexpr const char* kArrayCountTooLarge = "UI-A002";
inline constexpr const char* kArrayDirectionUnknown = "UI-A003";
inline constexpr const char* kArrayAngleZero = "UI-A004";
inline constexpr const char* kArrayAxisUnknown = "UI-A005";

//! 並べられる上限。これを超えるのは打ち間違いとみなす。
//! 200 個の窓を1度に並べる模型は無く、間違って 20000 と打つと画面が固まる。
inline constexpr int kMaximumArrayCount = 200;

//! 直線に並べる。
//!
//! count は **元のものを含めた数**。10 個並べるなら 10。
//! spanIsTotal が真なら vector は「端から端まで」、偽なら「1つ分の間隔」。
//! 返すのは **元からの移動量** を count-1 個(元のものは動かさないので入らない)。
[[nodiscard]] base::Result<std::vector<Vector3>> PlanLinearArray(const Vector3& vector,
    int count, bool spanIsTotal);

//! 円に並べる1つ分。
struct CircularArrayStep {
    //! 回す角(ラジアン)。元からの角。
    double angleRad = 0.0;
};

//! 円に並べる。
//!
//! totalAngleDeg は **端から端までの角**。360 なら一周で、
//! そのときは最後の1つが元の上に重なるので **置かない**(count 個で割り切る)。
//! 360 でなければ count-1 個で割る(両端に置く)。
[[nodiscard]] base::Result<std::vector<CircularArrayStep>> PlanCircularArray(
    const Vector3& axis, double totalAngleDeg, int count);

} // namespace kachakacha::v2::app
