// 型紙を画面へ収める変換(棚卸し A-3)。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/view/PatternView.h"

#include <cmath>

using kachakacha::v2::geometry::Point2;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireNear;
using kachakacha::v2::view::FitPatternPage;
using kachakacha::v2::view::PatternFit;

KACHA_V2_TEST(pattern_view, 縦長の紙は高さで決まる)
{
    // A4 縦(210×297)を 400×400 の枠へ。高さのほうが足りない。
    const PatternFit fit = FitPatternPage(210.0, 297.0, 400.0, 400.0, 10.0);
    RequireNear(fit.pixelsPerMm, 380.0 / 297.0, 1.0e-9, "高さで決まる");
    RequireNear(fit.heightPx, 380.0, 1.0e-9, "余白を引いた高さいっぱい");
}

KACHA_V2_TEST(pattern_view, 横長の紙は幅で決まる)
{
    const PatternFit fit = FitPatternPage(297.0, 210.0, 400.0, 400.0, 10.0);
    RequireNear(fit.pixelsPerMm, 380.0 / 297.0, 1.0e-9, "幅で決まる");
    RequireNear(fit.widthPx, 380.0, 1.0e-9, "余白を引いた幅いっぱい");
}

KACHA_V2_TEST(pattern_view, 縦横は必ず同じ倍率)
{
    // 別の倍率にすると型紙が歪む。原寸で切るものとして信用できなくなる。
    const PatternFit fit = FitPatternPage(100.0, 50.0, 800.0, 200.0, 0.0);
    RequireNear(fit.widthPx / 100.0, fit.heightPx / 50.0, 1.0e-9, "同じ倍率");
}

KACHA_V2_TEST(pattern_view, 紙は真ん中に来る)
{
    const PatternFit fit = FitPatternPage(100.0, 100.0, 400.0, 200.0, 0.0);
    RequireNear(fit.heightPx, 200.0, 1.0e-9, "高さいっぱい");
    RequireNear(fit.originPx.u, 100.0, 1.0e-9, "左右の余りを等しく配る");
    RequireNear(fit.originPx.v, 0.0, 1.0e-9, "上下は余らない");
}

KACHA_V2_TEST(pattern_view, 紙の左上と右下が合う)
{
    const PatternFit fit = FitPatternPage(210.0, 297.0, 400.0, 500.0, 10.0);
    const Point2 topLeft = fit.ToScreen(Point2{0.0, 0.0});
    const Point2 bottomRight = fit.ToScreen(Point2{210.0, 297.0});
    RequireNear(topLeft.u, fit.originPx.u, 1.0e-9, "左上");
    RequireNear(topLeft.v, fit.originPx.v, 1.0e-9, "左上");
    RequireNear(bottomRight.u, fit.originPx.u + fit.widthPx, 1.0e-9, "右下");
    RequireNear(bottomRight.v, fit.originPx.v + fit.heightPx, 1.0e-9, "右下");
}

KACHA_V2_TEST(pattern_view, 余白がなければ枠いっぱい)
{
    const PatternFit fit = FitPatternPage(100.0, 100.0, 300.0, 300.0, 0.0);
    RequireNear(fit.pixelsPerMm, 3.0, 1.0e-9, "3px/mm");
}

KACHA_V2_TEST(pattern_view, 枠が余白より狭ければ倍率1で返す)
{
    // 落ちないこと。描いても何も出ないが、画面を畳んだ瞬間に落ちてはいけない。
    const PatternFit fit = FitPatternPage(100.0, 100.0, 10.0, 10.0, 20.0);
    RequireNear(fit.pixelsPerMm, 1.0, 1.0e-9, "1px/mm");
    RequireNear(fit.widthPx, 0.0, 1.0e-9, "大きさは0");
}

KACHA_V2_TEST(pattern_view, 紙の寸法が0以下なら倍率1で返す)
{
    RequireNear(FitPatternPage(0.0, 100.0, 400.0, 400.0, 10.0).pixelsPerMm, 1.0, 1.0e-9, "幅0");
    RequireNear(FitPatternPage(100.0, -5.0, 400.0, 400.0, 10.0).pixelsPerMm, 1.0, 1.0e-9, "高さ負");
}

KACHA_V2_TEST(pattern_view, 数値でない値なら倍率1で返す)
{
    RequireNear(FitPatternPage(std::nan(""), 100.0, 400.0, 400.0, 10.0).pixelsPerMm, 1.0,
        1.0e-9, "NaN");
}

KACHA_V2_TEST(pattern_view, 余白が負でも枠からはみ出さない)
{
    const PatternFit fit = FitPatternPage(100.0, 100.0, 300.0, 300.0, -50.0);
    RequireNear(fit.pixelsPerMm, 3.0, 1.0e-9, "負の余白は0とみなす");
}

KACHA_V2_TEST_MAIN("pattern_view_tests")
