#pragma once

//! 型紙を画面で見る(棚卸し A-3)。
//!
//! 型紙は作れるが、画面で確かめる道が無かった。SVG や DXF に出して、
//! 別の道具で開くまで、紙に収まっているのかも、部材が何枚あるのかも分からない。
//! **紙とプラ板を無駄にしてから気づく** ことになる。
//!
//! ここが決めるのは「紙の1枚を、与えられた画面の広さにどう収めるか」だけである。
//! 色や線の太さは画面の仕事。ここは Qt を知らない。
//!
//! 収め方は **必ず縦横同じ倍率**。縦横で変えると、型紙が歪んで出る。
//! 型紙は原寸で切るためのものなので、歪んで見えてはいけない。

#include "kachakacha/geometry/CurveSampling.h"

namespace kachakacha::v2::view {

using geometry::Point2;

//! 紙を画面へ収める変換。mm → px。
struct PatternFit {
    //! 1mm が何 px になるか。縦横とも同じ。
    double pixelsPerMm = 1.0;
    //! 紙の左上が画面のどこに来るか(px)。
    Point2 originPx{};
    //! 収めた紙の大きさ(px)。
    double widthPx = 0.0;
    double heightPx = 0.0;

    //! 紙の上の点(mm、左上が原点、下向きが +y)を画面の点へ。
    [[nodiscard]] Point2 ToScreen(const Point2& millimetres) const noexcept
    {
        return Point2{originPx.u + millimetres.u * pixelsPerMm,
            originPx.v + millimetres.v * pixelsPerMm};
    }
};

//! 紙1枚を、画面の広さに収める。余白は marginPx。
//!
//! 画面が狭すぎる・紙の寸法が0以下などで決められないときは、
//! 倍率 1 の変換を返す(描いても何も出ないが、落ちはしない)。
[[nodiscard]] PatternFit FitPatternPage(double pageWidthMm, double pageHeightMm,
    double viewWidthPx, double viewHeightPx, double marginPx);

} // namespace kachakacha::v2::view
