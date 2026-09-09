#pragma once

//! ワイヤーの制御点を掴んで動かす(v1-input-parity.md §1-2 の 12)。
//!
//! V1では、選んでいるワイヤーの制御点が四角で出て、掴んで引きずれた。
//! V2には無く、いちど引いた線は消して引き直すしかなかった。
//!
//! ここで決めるのは「どの点が掴めるか」と「動かした後どんな曲線になるか」だけ。
//! 当たり判定の px と画面の描き方は Qt 側にある。
//!
//! **種類は保つ。** 直線を動かしたら直線、円弧を動かしたら円弧である。
//! 折れ線へ落とすと、動かすたびに形が近似で崩れていく。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/Vector3.h"

#include <string_view>
#include <vector>

namespace kachakacha::v2::geometry {

//! 掴める点1つ。
struct EditableControlPoint {
    Vector3 position{};
    //! 画面に出す名前。「始点」「制御点2」など。
    std::string_view labelJa;
};

//! その曲線の掴める点。動かせない曲線なら空。
//!
//! - 直線: 始点と終点
//! - 円弧: 始点・終点・中心
//! - 円: 中心と、半径を決める円周上の1点
//! - ベジエ / B-spline: 制御点そのもの
[[nodiscard]] std::vector<EditableControlPoint> EditableControlPointsOf(
    const CurveSegment& curve);

//! index 番目の点を to へ動かした曲線を作る。
//!
//! 作れない形(長さ0の直線、半径0の円など)は断る。つぶれた形を作らない。
[[nodiscard]] base::Result<CurveSegment> WithControlPointMoved(const CurveSegment& curve,
    std::size_t index, const Vector3& to);

} // namespace kachakacha::v2::geometry
