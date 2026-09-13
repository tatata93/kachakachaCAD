#pragma once

//! 作図面の上のものだけを相手にする(V1 の「作図面以外の線を常に薄く」の続き)。
//!
//! 3次元の空間に2次元の図面が何枚も浮いているのが、このCADの形である。
//! 画面では手前の面と奥の面が重なって見えるので、**別の面の線を掴んでしまう** 。
//! 薄くするだけでは足りない。薄い線でも、押せば掴めてしまうからである。
//!
//! そこで、**作図の道具を持っている間は、作業平面の上の線しか掴まない** 。
//! 選択道具のときは全部掴める。掴めないと、別の面のものを直せなくなる。
//!
//! 薄くするかどうかと、掴めるかどうかは、同じ判断から出す。
//! 別々に持つと、「薄いのに掴める」「濃いのに掴めない」が起きる。

#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/modeling/WorkPlane.h"

namespace kachakacha::v2::app {

//! 面に載っているとみなす厚み(mm)。V1 と同じ 1/1000 mm。
inline constexpr double kOnPlaneToleranceMm = 1.0e-3;

//! その線が作業平面の上にあるか。どの点も面から toleranceMm 以内なら上とみなす。
//! 判定は geometry::CurveLiesInPlane に任せ、スナップと同じ答えを出す。
//! 両端と真ん中の3点だけで見ると、途中で浮く3次曲線を「上」と読んでしまう。
[[nodiscard]] bool CurveLiesOnPlane(const geometry::CurveSegment& segment,
    const modeling::WorkPlaneFrame& plane, double toleranceMm = kOnPlaneToleranceMm);

//! いまその線を薄くするか。
//! drawing は作図の道具を持っているか、enabled は「作図面以外の線を常に薄く」の印。
//! selected なものは薄くしない。選んでいるものが消えたように見えてはいけない。
[[nodiscard]] bool DimsOffPlaneCurve(bool drawing, bool enabled, bool selected,
    bool onPlane) noexcept;

//! いまその線を掴んでよいか。薄くする判断と同じところから出す。
//! 薄くしているものは掴まない。薄いのに掴めると、見た目と手が食い違う。
[[nodiscard]] bool PickableOffPlaneCurve(bool drawing, bool enabled, bool onPlane) noexcept;

} // namespace kachakacha::v2::app
