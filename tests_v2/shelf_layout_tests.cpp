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
    Require(FrontShelfFor(UiMode::Part, DrawingTool::Select) == Shelf::Part, "部品は部品の棚");
    Require(FrontShelfFor(UiMode::Fabrication, DrawingTool::Select) == Shelf::Fabrication,
        "製作は製作の棚");
    Require(FrontShelfFor(UiMode::Output, DrawingTool::Select) == Shelf::Export, "出力は書き出し");
}

KACHA_V2_TEST(shelf_layout, どのモードにも道具の設定の棚がある)
{
    // 部品モードだけ右が「役割の表」で、道具の設定がどこにも無かった
    // (オーナー指摘 2026-09-11)。どのモードでも、先頭は設定の棚にする。
    Require(FrontShelfFor(UiMode::Drawing, DrawingTool::Select) == Shelf::Edit, "作図");
    Require(FrontShelfFor(UiMode::Part, DrawingTool::Select) == Shelf::Part, "部品");
    Require(FrontShelfFor(UiMode::Fabrication, DrawingTool::Select) == Shelf::Fabrication,
        "製作");
    Require(FrontShelfFor(UiMode::Output, DrawingTool::Select) == Shelf::Export, "出力");
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

KACHA_V2_TEST(shelf, 押し出しの最中は押し出しの棚が前に出る)
{
    // これが無かったので `Shelf::Extrude` はどの組み合わせにも現れず、
    // `RefreshRightShelves` が毎回その棚を隠していた。
    // 棚に値を入れた直後に自分で隠すので、押し出しの欄は一度も出なかった
    // (UX-AUDIT-EXTRUDE-SURFACE-R1 BUGS #1)。
    for (const UiMode mode : {UiMode::Drawing, UiMode::Part, UiMode::Fabrication,
             UiMode::Output}) {
        for (const DrawingTool tool : kAllTools) {
            const auto shelves = ShelvesFor(mode, tool, true);
            Require(!shelves.empty(), "棚が出る");
            Require(shelves.front() == Shelf::Extrude, "押し出しの棚が先頭");
            Require(FrontShelfFor(mode, tool, true) == Shelf::Extrude,
                "前に出るのも押し出しの棚");
            Require(std::find(shelves.begin(), shelves.end(), Shelf::Extrude)
                    != shelves.end(),
                "並びの中にもある");
        }
    }
}

KACHA_V2_TEST(shelf, 押し出しが終われば元の棚へ戻る)
{
    // 確定・取消のあとは、ふだんの棚に戻らなければならない。
    // 押し出しの棚が出たままだと、いま何をしているのか読めなくなる。
    for (const UiMode mode : {UiMode::Drawing, UiMode::Part, UiMode::Fabrication,
             UiMode::Output}) {
        const auto after = ShelvesFor(mode, DrawingTool::Select, false);
        Require(std::find(after.begin(), after.end(), Shelf::Extrude) == after.end(),
            "押し出しの棚は残らない");
        Require(FrontShelfFor(mode, DrawingTool::Select, false) == after.front(),
            "前に出るのは並びの先頭");
    }
}

KACHA_V2_TEST(shelf, 出せる棚は全部どこかの組み合わせで出る)
{
    // 台帳にあるのに、どの組み合わせでも出ない棚を作らない。
    // 押し出しの棚がまさにそれだった。
    std::set<int> reachable;
    for (const UiMode mode : {UiMode::Drawing, UiMode::Part, UiMode::Fabrication,
             UiMode::Output}) {
        for (const DrawingTool tool : kAllTools) {
            for (const bool extruding : {false, true}) {
                for (const bool surfacing : {false, true}) {
                    for (const bool booleaning : {false, true}) {
                        for (const bool thickening : {false, true}) {
                            for (const Shelf shelf : ShelvesFor(mode, tool, extruding,
                                     surfacing, booleaning, thickening)) {
                                reachable.insert(static_cast<int>(shelf));
                            }
                        }
                    }
                }
            }
        }
    }
    // いまのところ、この2枚だけは自分の命令が `show()` + `raise()` で出している。
    // **押し出しの棚と同じ壊れ方をする形である**(次に棚を作り直したときに消える)。
    // ここに並べてあるのは「知っていて残している」という印で、
    // 3枚目が増えたらこの関所が鳴る。
    const std::set<int> openedByOwnCommand{
        static_cast<int>(Shelf::WorkPlane),   // workplane.create が出す
        static_cast<int>(Shelf::Display),     // view.display_settings が出す
    };
    std::string missing;
    for (const Shelf shelf : AllShelves()) {
        const int id = static_cast<int>(shelf);
        if (reachable.count(id) == 0 && openedByOwnCommand.count(id) == 0) {
            missing += std::string(ShelfNameJa(shelf)) + " ";
        }
    }
    Require(missing.empty(), "出ない棚が無い: " + missing);
    Require(reachable.count(static_cast<int>(Shelf::Extrude)) == 1,
        "押し出しの棚は、棚の決め方そのものから出る(命令が一瞬出すのではない)");
    Require(reachable.count(static_cast<int>(Shelf::Surface)) == 1,
        "「面を作る」の棚も、棚の決め方そのものから出る");
    Require(reachable.count(static_cast<int>(Shelf::Boolean)) == 1,
        "「足す・引く」の棚も、棚の決め方そのものから出る");
    Require(reachable.count(static_cast<int>(Shelf::Thicken)) == 1,
        "「厚み」の棚も、棚の決め方そのものから出る");
}

KACHA_V2_TEST(shelf, 足す引くの最中はその棚が前に出る)
{
    for (const UiMode mode : {UiMode::Drawing, UiMode::Part, UiMode::Fabrication,
             UiMode::Output}) {
        const auto shelves = ShelvesFor(mode, DrawingTool::Select, false, false, true);
        Require(!shelves.empty() && shelves.front() == Shelf::Boolean, "「足す・引く」の棚が先頭");
        Require(FrontShelfFor(mode, DrawingTool::Select, false, false, true) == Shelf::Boolean,
            "前に出るのもその棚");
    }
    Require(ShelvesFor(UiMode::Part, DrawingTool::Select, false, true, true).front()
            == Shelf::Surface,
        "面を作るが先(両方は起きないが、起きたときに黙って混ぜない)");
}

KACHA_V2_TEST(shelf, 面を作る最中はその棚が前に出る)
{
    for (const UiMode mode : {UiMode::Drawing, UiMode::Part, UiMode::Fabrication,
             UiMode::Output}) {
        const auto shelves = ShelvesFor(mode, DrawingTool::Select, false, true);
        Require(!shelves.empty(), "棚が出る");
        Require(shelves.front() == Shelf::Surface, "「面を作る」の棚が先頭");
        Require(FrontShelfFor(mode, DrawingTool::Select, false, true) == Shelf::Surface,
            "前に出るのもその棚");
    }
    // 押し出しのほうが先。両方は起きないが、起きたときに黙って混ぜない。
    Require(ShelvesFor(UiMode::Part, DrawingTool::Select, true, true).front()
            == Shelf::Extrude,
        "押し出しが先");
}

KACHA_V2_TEST(shelf, 厚みの最中はその棚が前に出る)
{
    // 優先度は足す・引くの次(指示書 matrix P-10)。
    for (const UiMode mode : {UiMode::Drawing, UiMode::Part, UiMode::Fabrication,
             UiMode::Output}) {
        const auto shelves = ShelvesFor(mode, DrawingTool::Select, false, false, false, true);
        Require(!shelves.empty() && shelves.front() == Shelf::Thicken, "「厚み」の棚が先頭");
        Require(FrontShelfFor(mode, DrawingTool::Select, false, false, false, true)
                == Shelf::Thicken,
            "前に出るのもその棚");
    }
    // 足す・引くが動いていれば、そちらが先。両方は起きないが、起きたときに黙って混ぜない。
    Require(ShelvesFor(UiMode::Part, DrawingTool::Select, false, false, true, true).front()
            == Shelf::Boolean,
        "足す・引くが先");
}

KACHA_V2_TEST_MAIN("shelf_layout_tests")
