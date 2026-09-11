// 右に出す棚の決め方(オーナー指摘「右画面は選択ツールの設定だけ」)。
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/base/TestHarness.h"

#include <algorithm>
#include <set>
#include <string>

using kachakacha::v2::app::AllShelves;
using kachakacha::v2::app::FrontShelfFor;
using kachakacha::v2::app::Shelf;
using kachakacha::v2::app::ShelfNameJa;
using kachakacha::v2::app::ShelvesFor;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::modeling::DrawingTool;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

constexpr DrawingTool kAllTools[] = {
    DrawingTool::Select, DrawingTool::SetGridOrigin, DrawingTool::Point, DrawingTool::Line,
    DrawingTool::Polyline, DrawingTool::Rectangle, DrawingTool::Circle, DrawingTool::Arc,
    DrawingTool::Bezier, DrawingTool::Spline, DrawingTool::Move, DrawingTool::Copy,
    DrawingTool::Mirror, DrawingTool::Rotate, DrawingTool::Split, DrawingTool::Trim,
    DrawingTool::Extend, DrawingTool::JoinEndpoints, DrawingTool::TangentJoin,
    DrawingTool::CurvatureJoin, DrawingTool::Measure, DrawingTool::ConnectTwoPoints,
    DrawingTool::ChamferOrFilletPair,
};

constexpr UiMode kAllModes[] = {
    UiMode::Drawing, UiMode::Part, UiMode::Fabrication, UiMode::Output,
};

} // namespace

KACHA_V2_TEST(shelf_layout, どの道具にも出す棚がある)
{
    // 空になると、右が真っ白になって「壊れた」ようにしか見えない。
    for (const DrawingTool tool : kAllTools) {
        for (const UiMode mode : kAllModes) {
            Require(!ShelvesFor(mode, tool).empty(), "棚が1枚以上ある");
        }
    }
}

KACHA_V2_TEST(shelf_layout, 前に出る棚は先頭と同じ)
{
    for (const DrawingTool tool : kAllTools) {
        for (const UiMode mode : kAllModes) {
            Require(FrontShelfFor(mode, tool) == ShelvesFor(mode, tool).front(),
                "先頭が前に出る");
        }
    }
}

KACHA_V2_TEST(shelf_layout, 出す棚は多くても2枚)
{
    // 積みすぎると1枚あたりが潰れる。これが直したかったことそのものである。
    for (const DrawingTool tool : kAllTools) {
        for (const UiMode mode : kAllModes) {
            Require(ShelvesFor(mode, tool).size() <= 2, "2枚まで");
        }
    }
}

KACHA_V2_TEST(shelf_layout, 同じ棚を2度出さない)
{
    for (const DrawingTool tool : kAllTools) {
        for (const UiMode mode : kAllModes) {
            const auto shelves = ShelvesFor(mode, tool);
            const std::set<Shelf> unique(shelves.begin(), shelves.end());
            Require(unique.size() == shelves.size(), "重なりがない");
        }
    }
}

KACHA_V2_TEST(shelf_layout, 作図モードの選択は編集の棚だけ)
{
    const auto shelves = ShelvesFor(UiMode::Drawing, DrawingTool::Select);
    Require(shelves.size() == 1, "1枚だけ");
    Require(shelves.front() == Shelf::Edit, "選んだものを数値で直す欄");
}

KACHA_V2_TEST(shelf_layout, 線を引くときは作図の棚)
{
    for (const DrawingTool tool : {DrawingTool::Line, DrawingTool::Arc, DrawingTool::Circle,
             DrawingTool::Spline, DrawingTool::Rectangle}) {
        Require(FrontShelfFor(UiMode::Drawing, tool) == Shelf::Drawing, "作図の棚");
    }
}

KACHA_V2_TEST(shelf_layout, 直す道具は編集の棚)
{
    for (const DrawingTool tool : {DrawingTool::Move, DrawingTool::Copy, DrawingTool::Mirror,
             DrawingTool::Rotate, DrawingTool::Trim, DrawingTool::Extend}) {
        Require(FrontShelfFor(UiMode::Drawing, tool) == Shelf::Edit, "編集の棚");
    }
}

KACHA_V2_TEST(shelf_layout, 測るときは測る棚だけ)
{
    const auto shelves = ShelvesFor(UiMode::Part, DrawingTool::Measure);
    Require(shelves.size() == 1, "1枚だけ");
    Require(shelves.front() == Shelf::Measure, "測る棚");
}

KACHA_V2_TEST(shelf_layout, 道具はモードより強い)
{
    // どのモードでも、線を引いているなら作図の棚。
    // モードで上書きすると、道具を選んだのに欄が出てこない。
    for (const UiMode mode : kAllModes) {
        Require(FrontShelfFor(mode, DrawingTool::Line) == Shelf::Drawing, "作図の棚のまま");
        Require(FrontShelfFor(mode, DrawingTool::Measure) == Shelf::Measure, "測る棚のまま");
    }
}

KACHA_V2_TEST(shelf_layout, モードごとに選択の棚が変わる)
{
    Require(FrontShelfFor(UiMode::Part, DrawingTool::Select) == Shelf::GuideTable, "部品は役割の表");
    Require(FrontShelfFor(UiMode::Fabrication, DrawingTool::Select) == Shelf::Fabrication,
        "製作は製作の棚");
    Require(FrontShelfFor(UiMode::Output, DrawingTool::Select) == Shelf::Export, "出力は書き出し");
}

KACHA_V2_TEST(shelf_layout, 面取りは量の欄も一緒に出す)
{
    const auto shelves = ShelvesFor(UiMode::Drawing, DrawingTool::ChamferOrFilletPair);
    Require(shelves.size() == 2, "2枚");
    Require(shelves[0] == Shelf::Corner && shelves[1] == Shelf::Parameter, "面取りと数");
}

KACHA_V2_TEST(shelf_layout, すべての棚に名前がある)
{
    for (const Shelf shelf : AllShelves()) {
        Require(!ShelfNameJa(shelf).empty(), "名前が空でない");
        Require(ShelfNameJa(shelf) != std::string_view("なし"), "なし ではない");
    }
}

KACHA_V2_TEST(shelf_layout, 名前は重ならない)
{
    std::set<std::string> names;
    for (const Shelf shelf : AllShelves()) {
        names.insert(std::string(ShelfNameJa(shelf)));
    }
    Require(names.size() == AllShelves().size(), "全部ちがう名前");
}

KACHA_V2_TEST_MAIN("shelf_layout_tests")
