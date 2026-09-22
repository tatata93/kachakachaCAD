// 作図の棚の中身が、いまの道具に合っているか(オーナー指摘 2026-09-13)。
#include "kachakacha/app/DrawingShelfRows.h"
#include "kachakacha/base/TestHarness.h"

#include <set>
#include <string>

using kachakacha::v2::app::DrawingShelfRowsFor;
using kachakacha::v2::app::DrawingShelfTitleJa;
using kachakacha::v2::app::DrawingToolHintJa;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::modeling::DrawingToolNameJa;
using kachakacha::v2::test::Require;

namespace {

constexpr DrawingTool kAllTools[] = {
    DrawingTool::Select, DrawingTool::SetGridOrigin, DrawingTool::Point, DrawingTool::Line,
    DrawingTool::Polyline, DrawingTool::Rectangle, DrawingTool::Circle, DrawingTool::Arc,
    DrawingTool::Bezier, DrawingTool::Spline, DrawingTool::Move, DrawingTool::Copy,
    DrawingTool::Mirror, DrawingTool::Rotate, DrawingTool::Split, DrawingTool::Trim,
    DrawingTool::Extend, DrawingTool::JoinEndpoints, DrawingTool::TangentJoin,
    DrawingTool::CurvatureJoin, DrawingTool::Measure, DrawingTool::ConnectTwoPoints,
    DrawingTool::ChamferOrFilletPair, DrawingTool::Scale,
};

} // namespace

KACHA_V2_TEST(drawing_shelf, 円弧の欄は円弧のときだけ出る)
{
    // これが直したかったことそのものである。直線や円弧を引いたあと
    // ベジェ曲線へ持ち替えても、右にはまだ「円弧の作り方」が出ていた。
    for (const DrawingTool tool : kAllTools) {
        const bool shown = DrawingShelfRowsFor(tool).arc;
        const bool isArc = tool == DrawingTool::Arc;
        Require(shown == isArc,
            std::string(DrawingToolNameJa(tool)) + ": 円弧の欄は円弧のときだけ");
    }
}

KACHA_V2_TEST(drawing_shelf, ベジェには決める欄が無い)
{
    for (const DrawingTool tool : {DrawingTool::Bezier, DrawingTool::Spline}) {
        const auto rows = DrawingShelfRowsFor(tool);
        Require(!rows.arc, "円弧の欄は出ない");
        // 補助線と作図点は、線を引く道具なら意味がある。
        Require(rows.construction && rows.keepPoints, "補助線と作図点は出る");
    }
}

KACHA_V2_TEST(drawing_shelf, 制御多角形の欄はベジェのときだけ出る)
{
    // D-11: 制御点を順に結んだ折れ線を補助線として残す。制御点があるのはベジェだけ。
    for (const DrawingTool tool : kAllTools) {
        Require(DrawingShelfRowsFor(tool).controlPolygon == (tool == DrawingTool::Bezier),
            std::string(DrawingToolNameJa(tool)) + ": 制御多角形の欄はベジェのときだけ");
    }
}

KACHA_V2_TEST(drawing_shelf, 線を引かない道具では道具の区画が空になる)
{
    for (const DrawingTool tool : {DrawingTool::Select, DrawingTool::Move,
             DrawingTool::Trim, DrawingTool::Measure}) {
        Require(DrawingShelfRowsFor(tool).ToolSectionEmpty(),
            std::string(DrawingToolNameJa(tool)) + ": 決める欄が無い");
    }
    Require(!DrawingShelfRowsFor(DrawingTool::Line).ToolSectionEmpty(), "直線にはある");
}

KACHA_V2_TEST(drawing_shelf, 数値で線を作る区画はいつでも出る)
{
    // ここは道具と関係ない別の区画である。道具で消すと、
    // 「さっきまであった欄が消えた」に見える。
    for (const DrawingTool tool : kAllTools) {
        Require(DrawingShelfRowsFor(tool).directWire, "いつでも出る");
    }
}

KACHA_V2_TEST(drawing_shelf, 見出しにいまの道具の名前が入る)
{
    for (const DrawingTool tool : kAllTools) {
        const std::string title = DrawingShelfTitleJa(tool);
        Require(title.find(std::string(DrawingToolNameJa(tool))) != std::string::npos,
            std::string(DrawingToolNameJa(tool)) + ": 名前が入る");
        Require(title.find("作図") != std::string::npos, "棚の名前も残る");
    }
}

KACHA_V2_TEST(drawing_shelf, どの道具にも使い方の一文がある)
{
    // 決める欄が無い道具ほど、次に何をするのかが分からない。
    std::set<std::string> hints;
    for (const DrawingTool tool : kAllTools) {
        const std::string hint(DrawingToolHintJa(tool));
        Require(!hint.empty(), std::string(DrawingToolNameJa(tool)) + ": 一文がある");
        hints.insert(hint);
    }
    // 全部が同じ文だと、書いていないのと同じである。
    Require(hints.size() >= 15, "道具ごとに違う文になっている");
}

KACHA_V2_TEST(drawing_shelf, 円弧の一文は作り方の欄を指す)
{
    const std::string hint(DrawingToolHintJa(DrawingTool::Arc));
    Require(hint.find("作り方") != std::string::npos, "欄を指す");
    // 逆に、欄の無い道具では欄の話をしない。
    const std::string bezier(DrawingToolHintJa(DrawingTool::Bezier));
    Require(bezier.find("決める欄はありません") != std::string::npos, "無いと言う");
}

KACHA_V2_TEST_MAIN("drawing_shelf_rows_tests")
