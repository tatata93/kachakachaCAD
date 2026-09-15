// 「面を作る」の入力スロット(app/SurfaceInputState.h)。
//
// いまの面生成は人から見て2本立てだった。
//   - 「形状ガイド」= 選んだ線を全部 断面 にして、本数で方式が決まる
//   - 役割表      = 方式を先に決め、役割を選んで行を足し、表から作る
// 前者では平面も案内付きロフトも境界埋めも作れず、後者は後ろの札にあって、
// しかも方式を変える前に要らない行を自分で消さないと UI-R003 で断られた。
#include "kachakacha/app/SurfaceInputState.h"
#include "kachakacha/base/TestHarness.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

using kachakacha::v2::app::MainSurfaceMethods;
using kachakacha::v2::app::OtherSurfaceMethods;
using kachakacha::v2::app::DefaultSurfaceIntakeSlot;
using kachakacha::v2::app::RecommendSurfaceMethod;
using kachakacha::v2::app::RoleForSurfaceSlot;
using kachakacha::v2::app::SurfaceInputState;
using kachakacha::v2::app::SurfaceOrdering;
using kachakacha::v2::app::SurfaceCountProblemJa;
using kachakacha::v2::app::SurfaceReadyToBuild;
using kachakacha::v2::app::SurfaceSectionOrder;
using kachakacha::v2::app::SurfaceSelectionFacts;
using kachakacha::v2::app::SurfaceSlotState;
using kachakacha::v2::app::SurfaceSlotsFor;
using kachakacha::v2::app::SurfaceStatusLinesJa;
using kachakacha::v2::app::WithSurfaceEntries;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::modeling::ChainRole;
using kachakacha::v2::modeling::GuideSurfaceMethod;
using kachakacha::v2::test::Require;

namespace {

[[nodiscard]] EntityId Id(std::uint8_t value)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = value;
    return EntityId(kachakacha::v2::base::Uuid(bytes));
}

[[nodiscard]] SurfaceSlotState StateOf(const SurfaceInputState& state, ChainRole role)
{
    for (const auto& view : SurfaceSlotsFor(state)) {
        if (view.role == role) {
            return view.state;
        }
    }
    return SurfaceSlotState::NotUsedByMethod;
}

} // namespace

KACHA_V2_TEST(surface_input, 閉じた平面輪郭1本は平面を薦める)
{
    // ここが「断面が2つ以上要ります。線を2本以上選んでください。」で
    // 断られていた。もっとも素朴な操作が、もっとも遠い道になっていた。
    SurfaceSelectionFacts facts;
    facts.closedWires = 1;
    facts.closedPlanarWires = 1;
    Require(RecommendSurfaceMethod(facts) == GuideSurfaceMethod::PlanarBoundary, "平面");
}

KACHA_V2_TEST(surface_input, 本数で薦める作り方が変わる)
{
    SurfaceSelectionFacts two;
    two.closedWires = 2;
    Require(RecommendSurfaceMethod(two) == GuideSurfaceMethod::RuledSections, "2本はルールド");
    SurfaceSelectionFacts three;
    three.closedWires = 3;
    Require(RecommendSurfaceMethod(three) == GuideSurfaceMethod::LoftSections,
        "3本はロフト");
    SurfaceSelectionFacts surface;
    surface.guideSurfaces = 1;
    Require(RecommendSurfaceMethod(surface) == GuideSurfaceMethod::OffsetGuide,
        "面1枚は離した面");
}

KACHA_V2_TEST(surface_input, 作り方を変えても入力を捨てない)
{
    // これが本題。方式を変える前に要らない行を自分で消さないと UI-R003 で
    // 断られていた。人は「線を入れてから方式を試す」ものである。
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::LoftSections;
    state = WithSurfaceEntries(state, ChainRole::Section, {Id(1), Id(2), Id(3)}, true);
    state = WithSurfaceEntries(state, ChainRole::GuideU, {Id(4)}, true);
    Require(state.sections.size() == 3, "断面が入った");
    Require(state.guides.size() == 1, "ガイドも入った");

    // ロフトはガイドを使わない。**でも消えない。**
    Require(StateOf(state, ChainRole::Section) == SurfaceSlotState::Used, "断面は使う");
    Require(StateOf(state, ChainRole::GuideU) == SurfaceSlotState::NotUsedByMethod,
        "ガイドはこの作り方では使わない");
    Require(state.guides.size() == 1, "**入れたものは残っている**");

    // 案内付きロフトへ変えると、そのまま使える。入れ直さなくてよい。
    state.method = GuideSurfaceMethod::GuidedLoft;
    Require(StateOf(state, ChainRole::GuideU) == SurfaceSlotState::Used, "ガイドを使う");
    Require(SurfaceReadyToBuild(state), "そのまま作れる");
}

KACHA_V2_TEST(surface_input, 足りない役割を名前で言う)
{
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::GuidedLoft;
    state = WithSurfaceEntries(state, ChainRole::Section, {Id(1), Id(2)}, true);
    Require(StateOf(state, ChainRole::GuideU) == SurfaceSlotState::Missing, "ガイドが足りない");
    Require(!SurfaceReadyToBuild(state), "まだ作れない");
    const auto lines = SurfaceStatusLinesJa(state, false);
    const bool said = std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
        return line.find("ガイドを選んでください") != std::string::npos;
    });
    Require(said, "何を選べばよいかを言う");
}

KACHA_V2_TEST(surface_input, 使わない入力は消していないと言う)
{
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::LoftSections;
    state = WithSurfaceEntries(state, ChainRole::Section, {Id(1), Id(2)}, true);
    state = WithSurfaceEntries(state, ChainRole::BoundarySide, {Id(9)}, true);
    const auto lines = SurfaceStatusLinesJa(state, false);
    const bool said = std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
        return line.find("消してはいません") != std::string::npos;
    });
    Require(said, "捨てていないことを言う");
}

KACHA_V2_TEST(surface_input, 手動固定の並びがそのまま生成順になる)
{
    // §13。画面の 1,2,3... をそのまま生成順にする。カーネルに並べ替えさせない。
    SurfaceInputState state;
    state = WithSurfaceEntries(state, ChainRole::Section, {Id(1), Id(2), Id(3)}, true);
    Require(SurfaceSectionOrder(state) == state.sections, "自動なら入れた順のまま");

    state.ordering = SurfaceOrdering::ManualLock;
    state.explicitOrder = {Id(3), Id(1), Id(2)};
    const auto ordered = SurfaceSectionOrder(state);
    Require(ordered.size() == 3, "3本のまま");
    Require(ordered[0] == Id(3) && ordered[1] == Id(1) && ordered[2] == Id(2),
        "画面の並びのとおり");
}

KACHA_V2_TEST(surface_input, 手動固定の並びから漏れた断面も落とさない)
{
    SurfaceInputState state;
    state = WithSurfaceEntries(state, ChainRole::Section, {Id(1), Id(2), Id(3)}, true);
    state.ordering = SurfaceOrdering::ManualLock;
    state.explicitOrder = {Id(2)};   // 1本しか並べていない
    const auto ordered = SurfaceSectionOrder(state);
    Require(ordered.size() == 3, "**落とさない**");
    Require(ordered.front() == Id(2), "並べたものが先");
}

KACHA_V2_TEST(surface_input, 同じものを二度入れない)
{
    SurfaceInputState state;
    state = WithSurfaceEntries(state, ChainRole::Section, {Id(1)}, true);
    state = WithSurfaceEntries(state, ChainRole::Section, {Id(1), Id(2)}, false);
    Require(state.sections.size() == 2, "増えるのは新しいものだけ");
}

KACHA_V2_TEST(surface_input, 主要6方式とその他が重ならない)
{
    // UI の正本「1. 作り方」は6枚。残りは消さずに「その他」へ置く。
    Require(MainSurfaceMethods().size() == 6, "6枚");
    for (const auto method : OtherSurfaceMethods()) {
        Require(std::find(MainSurfaceMethods().begin(), MainSurfaceMethods().end(), method)
                == MainSurfaceMethods().end(),
            "重ならない");
    }
    // 8方式のどれも、どちらかに入っている。**既存機能を消さない。**
    const GuideSurfaceMethod all[] = {GuideSurfaceMethod::PlanarBoundary,
        GuideSurfaceMethod::RuledSections, GuideSurfaceMethod::LoftSections,
        GuideSurfaceMethod::GuidedLoft, GuideSurfaceMethod::GordonNetwork,
        GuideSurfaceMethod::BoundaryFill, GuideSurfaceMethod::OffsetGuide,
        GuideSurfaceMethod::Revolve};
    for (const auto method : all) {
        const bool listed = std::find(MainSurfaceMethods().begin(),
                                MainSurfaceMethods().end(), method)
                != MainSurfaceMethods().end()
            || std::find(OtherSurfaceMethods().begin(), OtherSurfaceMethods().end(), method)
                != OtherSurfaceMethods().end();
        Require(listed, "どこかに出る");
    }
}

KACHA_V2_TEST(surface_input, 平面の境界欄は外形として渡る)
{
    // 正本の欄は「断面 / ガイド / 境界」の3つ。内部の役割は外形・穴・境界辺と細かい。
    // 繋ぎ方を間違えると、**平面は欄から1本も入れられない。**
    ChainRole role = ChainRole::Section;
    Require(RoleForSurfaceSlot(GuideSurfaceMethod::PlanarBoundary,
                ChainRole::BoundarySide, role),
        "平面は境界欄を使う");
    Require(role == ChainRole::OuterBoundary, "中では外形になる");
    Require(!RoleForSurfaceSlot(GuideSurfaceMethod::PlanarBoundary, ChainRole::Section,
                role),
        "平面は断面欄を使わない");
    Require(DefaultSurfaceIntakeSlot(GuideSurfaceMethod::PlanarBoundary)
            == ChainRole::BoundarySide,
        "選んだものは境界欄へ入る");
}

KACHA_V2_TEST(surface_input, 曲線網は断面をUガイドをVにする)
{
    // 正本に U/V を別に入れる欄が無い(UI_DEVIATION_REQUEST)。
    // 「断面 = U、ガイド = V」として実装した。欄は増やしていない。
    ChainRole role = ChainRole::Section;
    Require(RoleForSurfaceSlot(GuideSurfaceMethod::GordonNetwork, ChainRole::Section, role),
        "断面欄を使う");
    Require(role == ChainRole::GuideU, "断面欄は U");
    Require(RoleForSurfaceSlot(GuideSurfaceMethod::GordonNetwork, ChainRole::GuideU, role),
        "ガイド欄を使う");
    Require(role == ChainRole::GuideV, "ガイド欄は V");
}

KACHA_V2_TEST(surface_input, 欄が空なら平面も曲線網も作れない)
{
    // 直す前は、平面のとき3つの欄が全部「この方式では不要」になり、
    // **何も入っていないのに「生成可能」と出ていた。**
    SurfaceInputState planar;
    planar.method = GuideSurfaceMethod::PlanarBoundary;
    Require(!SurfaceReadyToBuild(planar), "空の平面は作れない");
    planar = WithSurfaceEntries(planar, ChainRole::BoundarySide, {Id(1)}, false);
    Require(SurfaceReadyToBuild(planar), "境界を1本入れれば作れる");

    SurfaceInputState gordon;
    gordon.method = GuideSurfaceMethod::GordonNetwork;
    gordon = WithSurfaceEntries(gordon, ChainRole::Section, {Id(1), Id(2)}, false);
    Require(!SurfaceReadyToBuild(gordon), "U だけでは作れない");
    gordon = WithSurfaceEntries(gordon, ChainRole::GuideU, {Id(3)}, false);
    Require(SurfaceReadyToBuild(gordon), "U と V が揃えば作れる");
}

KACHA_V2_TEST(surface_input, 本数が合わないときは足りないのか多いのかを言う)
{
    // 「まだ作れません」だけでは、何を直せばよいのか分からない。
    // ルールドに3本入れて「生成可能」と出し、押したら断る、では最悪である。
    SurfaceInputState ruled;
    ruled.method = GuideSurfaceMethod::RuledSections;
    ruled = WithSurfaceEntries(ruled, ChainRole::Section, {Id(1), Id(2), Id(3)}, false);
    Require(!SurfaceReadyToBuild(ruled), "3本のルールドは作れない");
    const auto why = SurfaceCountProblemJa(ruled);
    Require(why.find("2本") != std::string::npos, std::string("2本と言う: ") + why);
    Require(why.find("ロフト") != std::string::npos, "ロフトへの逃げ道も言う");

    SurfaceInputState ok;
    ok.method = GuideSurfaceMethod::RuledSections;
    ok = WithSurfaceEntries(ok, ChainRole::Section, {Id(1), Id(2)}, false);
    Require(SurfaceReadyToBuild(ok), "2本なら作れる");
    Require(SurfaceCountProblemJa(ok).empty(), "合っていれば何も言わない");

    // 状態の行にも、その理由がそのまま出る。
    const auto lines = SurfaceStatusLinesJa(ruled, false);
    bool said = false;
    for (const auto& line : lines) {
        if (line.find("2本") != std::string::npos) {
            said = true;
        }
    }
    Require(said, "状態の行に理由が出る");
}

KACHA_V2_TEST_MAIN("surface_input_state_tests")
