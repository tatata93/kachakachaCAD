#pragma once

//! 曲線網(Gordon)の形を、U 線と V 線から直接組み立てる(核は面へ写すだけ)。
//!
//!   S(u, v) = L_U(u, v) + L_V(u, v) - T(u, v)
//!     L_U: U 線を v の向きにつないだ面(U 線を全部通る)
//!     L_V: V 線を u の向きにつないだ面(V 線を全部通る)
//!     T  : 交点だけを通る面(2 つを足すと交点のまわりが 2 重になるぶんを引く)
//! つなぎ方は u・v とも同じ 1 次元の補間(自然 3 次スプライン)にする。同じ補間を使うと、
//! S は **U 線も V 線も全部通る**(Gordon の恒等式)。
//!
//! 線の t は線ごとにばらばらなので、交点が同じ u・v に来るように線ごとに t を付け替える
//! (交点の位置で区切った折れ線の対応)。これが無いと、交点がずれて網がねじれる。
//!
//! ここは OCCT を呼ばない。出来た格子点を核が B-spline 面へ写す。

#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"

#include <cstddef>
#include <vector>

namespace kachakacha::v2::modeling {

struct GordonGrid {
    std::size_t rows = 0;      //!< v の数
    std::size_t columns = 0;   //!< u の数
    //! 行・列の u・v。等間隔の点に、U 線・V 線の位置そのものを必ず混ぜる
    //! (格子の行が U 線の上を、列が V 線の上を通るようにする)。
    std::vector<double> uParameters;
    std::vector<double> vParameters;
    //! U 線が通る行・V 線が通る列(試験と、核の測り直しに使う)。
    std::vector<std::size_t> uCurveRows;
    std::vector<std::size_t> vCurveColumns;
    //! 行ごと(v)に並べた点。points[row * columns + column]。
    std::vector<Vector3> points;

    [[nodiscard]] const Vector3& At(std::size_t row, std::size_t column) const
    {
        return points[row * columns + column];
    }
};

//! U 線(外形U)と V 線(外形V)から Gordon の格子点を作る。検査(曲線網(Gordon))を
//! 通った入力を渡す。samples は u・v それぞれの点の数(3 以上)。
[[nodiscard]] base::Result<GordonGrid> BuildGordonGrid(const GuideSurfaceRequest& request,
    const GeometryTolerance& tolerance, std::size_t samples);

//! 自然 3 次スプライン(値はベクトル)。x は増える順。2 点なら直線。
[[nodiscard]] Vector3 NaturalSplineAt(const std::vector<double>& x,
    const std::vector<Vector3>& values, double at);

} // namespace kachakacha::v2::modeling
