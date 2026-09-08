// 画面サイズと拡大率(AT-UIX-010)。
//
// 絵を撮って目で見るのは PC の側でやる。ここでは、その絵が壊れる原因になる
// 「置き場所の計算」を数で確かめる。文字切れ・重なり・画面外・0サイズは、
// どれも置き場所の計算が破綻したときに起きる。
#include "kachakacha/app/CursorInput.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/GridModel.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <string>
#include <vector>

using kachakacha::v2::app::CursorPanelPlacement;
using kachakacha::v2::app::PlaceCursorPanel;
using kachakacha::v2::app::kCursorPanelOffsetPx;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::EvaluateGrid;
using kachakacha::v2::modeling::GridDefinition;
using kachakacha::v2::modeling::StandardPlane;
using kachakacha::v2::modeling::StandardPlaneKind;
using kachakacha::v2::modeling::kMinimumGridSpacingPx;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

//! 契約が名指ししている画面サイズ。
struct ScreenSize {
    const char* name;
    double widthPx;
    double heightPx;
};

const ScreenSize kScreens[] = {
    {"1366x768", 1366.0, 768.0},
    {"1920x1080", 1920.0, 1080.0},
};

//! 契約が名指ししている拡大率。
const double kScales[] = {1.0, 1.25, 1.5, 2.0};

//! 入力列の大きさ。拡大率がかかる。
[[nodiscard]] double PanelWidth(double scale) { return 210.0 * scale; }
[[nodiscard]] double PanelHeight(double scale, int rows)
{
    return (12.0 + 18.0 * rows) * scale;
}

[[nodiscard]] std::string Where(const ScreenSize& screen, double scale, double x, double y)
{
    return std::string(screen.name) + " x" + std::to_string(scale) + " のカーソル("
        + std::to_string(x) + "," + std::to_string(y) + ")";
}

} // namespace

KACHA_V2_TEST(theme, どの画面と拡大率でも入力列が画面の外へ出ない)
{
    for (const ScreenSize& screen : kScreens) {
        for (double scale : kScales) {
            // 画面全体を粗く走査する。角も端も真ん中も通る。
            for (double fx = 0.0; fx <= 1.0; fx += 0.1) {
                for (double fy = 0.0; fy <= 1.0; fy += 0.1) {
                    const double x = screen.widthPx * fx;
                    const double y = screen.heightPx * fy;
                    const double w = PanelWidth(scale);
                    const double h = PanelHeight(scale, 7);
                    const CursorPanelPlacement placement =
                        PlaceCursorPanel(x, y, w, h, screen.widthPx, screen.heightPx);
                    const std::string where = Where(screen, scale, x, y);
                    Require(placement.xPx >= -1e-9, where + " 左へ出ない");
                    Require(placement.yPx >= -1e-9, where + " 上へ出ない");
                    Require(placement.xPx + w <= screen.widthPx + 1e-9,
                        where + " 右へ出ない");
                    Require(placement.yPx + h <= screen.heightPx + 1e-9,
                        where + " 下へ出ない");
                }
            }
        }
    }
}

KACHA_V2_TEST(theme, 入力列は画面の真ん中ではカーソルの右下に出る)
{
    for (const ScreenSize& screen : kScreens) {
        for (double scale : kScales) {
            const double x = screen.widthPx * 0.3;
            const double y = screen.heightPx * 0.3;
            const CursorPanelPlacement placement = PlaceCursorPanel(x, y,
                PanelWidth(scale), PanelHeight(scale, 7), screen.widthPx, screen.heightPx);
            RequireNear(placement.xPx, x + kCursorPanelOffsetPx, 1e-9, "右へ16px");
            RequireNear(placement.yPx, y + kCursorPanelOffsetPx, 1e-9, "下へ16px");
            Require(!placement.flippedHorizontally && !placement.flippedVertically,
                "寄せない");
        }
    }
}

KACHA_V2_TEST(theme, 画面の端では必ず寄せる)
{
    for (const ScreenSize& screen : kScreens) {
        for (double scale : kScales) {
            const CursorPanelPlacement corner = PlaceCursorPanel(screen.widthPx - 1.0,
                screen.heightPx - 1.0, PanelWidth(scale), PanelHeight(scale, 7),
                screen.widthPx, screen.heightPx);
            Require(corner.flippedHorizontally, "左へ寄せる");
            Require(corner.flippedVertically, "上へ寄せる");
        }
    }
}

KACHA_V2_TEST(theme, 拡大しても入力列がカーソルを覆い隠さない)
{
    // 右下に出したとき、入力列の左上の角がカーソルより右下にあること。
    // ここが崩れると、拡大率を上げたときにカーソルが隠れて何も置けなくなる。
    for (const ScreenSize& screen : kScreens) {
        for (double scale : kScales) {
            const double x = screen.widthPx * 0.25;
            const double y = screen.heightPx * 0.25;
            const CursorPanelPlacement placement = PlaceCursorPanel(x, y,
                PanelWidth(scale), PanelHeight(scale, 7), screen.widthPx, screen.heightPx);
            Require(placement.xPx > x, "カーソルより右");
            Require(placement.yPx > y, "カーソルより下");
        }
    }
}

KACHA_V2_TEST(theme, 拡大しても細かすぎるグリッドは出さない)
{
    // 拡大率を上げると1mmあたりのpxが増える。増えたときに副点が出て、
    // 減ったときに消えること。閾値の向こう側で符号が反転しないこと。
    GridDefinition definition;
    definition.majorSpacingMm = 10.0;
    definition.subdivision = 4;
    const auto plane = StandardPlane(StandardPlaneKind::XY);
    bool sawVisible = false;
    bool sawHidden = false;
    double previousSpacingPx = -1.0;
    for (double pixelsPerMm = 0.05; pixelsPerMm <= 8.0; pixelsPerMm *= 1.3) {
        const auto evaluated = EvaluateGrid(definition, plane, pixelsPerMm);
        Require(evaluated.HasValue(), "評価できる");
        const auto& grid = evaluated.Value();
        Require(grid.majorSpacingPx > previousSpacingPx, "拡大すれば間隔も広がる");
        previousSpacingPx = grid.majorSpacingPx;
        const double minorPx = grid.majorSpacingPx / 4.0;
        RequireEqual(grid.minorVisible ? "出す" : "出さない",
            minorPx >= kMinimumGridSpacingPx ? "出す" : "出さない",
            "閾値どおりに決まる");
        sawVisible = sawVisible || grid.minorVisible;
        sawHidden = sawHidden || !grid.minorVisible;
    }
    Require(sawVisible && sawHidden, "出る場合と出ない場合の両方を通った");
}

KACHA_V2_TEST(theme, 画面が小さくても0サイズにならない)
{
    // 入力列より狭い画面でも、位置は画面の中に収まり、負にならない。
    for (double scale : kScales) {
        const double w = PanelWidth(scale);
        const double h = PanelHeight(scale, 7);
        const CursorPanelPlacement placement = PlaceCursorPanel(10.0, 10.0, w, h,
            w * 0.5, h * 0.5);
        Require(placement.xPx >= 0.0 && placement.yPx >= 0.0, "負にならない");
        RequireNear(placement.xPx, 0.0, 1e-9, "左端へ寄る");
        RequireNear(placement.yPx, 0.0, 1e-9, "上端へ寄る");
    }
}

KACHA_V2_TEST(theme, 欄の数が増えても縦にはみ出さない)
{
    // 3Dの直線は7欄ある。拡大率2倍の 1366x768 でも下へ出ない。
    for (int rows = 1; rows <= 10; ++rows) {
        for (const ScreenSize& screen : kScreens) {
            for (double scale : kScales) {
                const double h = PanelHeight(scale, rows);
                const CursorPanelPlacement placement = PlaceCursorPanel(
                    screen.widthPx * 0.5, screen.heightPx - 4.0, PanelWidth(scale), h,
                    screen.widthPx, screen.heightPx);
                Require(placement.yPx >= -1e-9, "上へ出ない");
                Require(placement.yPx + h <= screen.heightPx + 1e-9, "下へ出ない");
            }
        }
    }
}

KACHA_V2_TEST_MAIN("theme_layout_tests")
