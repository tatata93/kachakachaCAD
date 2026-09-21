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
using kachakacha::v2::app::AllSurfaceEntries;
using kachakacha::v2::app::CanActivateSurfaceSlot;
using kachakacha::v2::app::SurfaceActiveSlotHintJa;
using kachakacha::v2::app::SurfaceSlotHolding;
using kachakacha::v2::app::WithActiveSlotSettled;
using kachakacha::v2::app::WithActiveSurfaceSlot;
using kachakacha::v2::app::WithSurfaceEntries;
using kachakacha::v2::app::WithSurfaceEntriesToggled;
using kachakacha::v2::app::WithSurfaceSlotCleared;
using kachakacha::v2::app::WithoutSurfaceEntries;
using kachakacha::v2::app::WithSurfaceSlotAdvanced;
using kachakacha::v2::app::SurfaceSlotNameJa;
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

KACHA_V2_TEST(surface_input, 道具を先に押した空入力は平面輪郭待ちになる)
{
    SurfaceSelectionFacts empty;
    Require(RecommendSurfaceMethod(empty) == GuideSurfaceMethod::PlanarBoundary,
        "閉じた領域を内側クリックできる入口");
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
    // 2026-09-22: ロフトはガイドを使う(0〜任意)。使わない作り方はルールド。
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::RuledSections;
    state = WithSurfaceEntries(state, ChainRole::Section, {Id(1), Id(2), Id(3)}, true);
    state = WithSurfaceEntries(state, ChainRole::GuideU, {Id(4)}, true);
    Require(state.sections.size() == 3, "断面が入った");
    Require(state.guides.size() == 1, "ガイドも入った");

    // ルールドはガイドを使わない。**でも消えない。**
    Require(StateOf(state, ChainRole::Section) == SurfaceSlotState::Used, "断面は使う");
    Require(StateOf(state, ChainRole::GuideU) == SurfaceSlotState::NotUsedByMethod,
        "ガイドはこの作り方では使わない");
    Require(state.guides.size() == 1, "**入れたものは残っている**");

    // ロフトへ変えると、そのまま使える。入れ直さなくてよい。
    state.method = GuideSurfaceMethod::LoftSections;
    Require(StateOf(state, ChainRole::GuideU) == SurfaceSlotState::Used, "ガイドを使う");
    Require(SurfaceReadyToBuild(state), "そのまま作れる");
    // ガイドを外してもロフトは作れる(ガイドは任意)。
    state = WithSurfaceSlotCleared(state, ChainRole::GuideU);
    Require(StateOf(state, ChainRole::GuideU) == SurfaceSlotState::Optional, "ガイドは任意");
    Require(SurfaceReadyToBuild(state), "断面だけのロフトも作れる");
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
    // 2026-09-22: 案内付きロフトはロフトへ統合し、主要の入口は四辺面に譲った。
    Require(std::find(MainSurfaceMethods().begin(), MainSurfaceMethods().end(),
                GuideSurfaceMethod::FourEdgePatch)
            != MainSurfaceMethods().end(),
        "四辺面は主要の入口");
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
        GuideSurfaceMethod::Revolve, GuideSurfaceMethod::FourEdgePatch};
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
    Require(!SurfaceReadyToBuild(gordon), "V が 1 本では網にならない(外側に 2 本要る)");
    gordon = WithSurfaceEntries(gordon, ChainRole::GuideU, {Id(4)}, false);
    Require(SurfaceReadyToBuild(gordon), "U 2 本と V 2 本が揃えば作れる");
}

KACHA_V2_TEST(surface_input, 本数が合わないときは足りないのか多いのかを言う)
{
    // 「まだ作れません」だけでは、何を直せばよいのか分からない。
    // 2026-09-22: ルールドは 2〜任意(3 本以上は隣り合う 2 本ずつの帯)。
    // 足りないときを見るのは 1 本のルールドと、辺が 3 本の四辺面。
    SurfaceInputState ruled;
    ruled.method = GuideSurfaceMethod::RuledSections;
    ruled = WithSurfaceEntries(ruled, ChainRole::Section, {Id(1)}, false);
    Require(!SurfaceReadyToBuild(ruled), "1本のルールドは作れない");
    const auto why = SurfaceCountProblemJa(ruled);
    Require(why.find("2本以上") != std::string::npos, std::string("2本以上と言う: ") + why);

    SurfaceInputState ok;
    ok.method = GuideSurfaceMethod::RuledSections;
    ok = WithSurfaceEntries(ok, ChainRole::Section, {Id(1), Id(2), Id(3)}, false);
    Require(SurfaceReadyToBuild(ok), "3本のルールドは帯をつないで作れる");
    Require(SurfaceCountProblemJa(ok).empty(), "合っていれば何も言わない");

    SurfaceInputState patch;
    patch.method = GuideSurfaceMethod::FourEdgePatch;
    patch = WithSurfaceEntries(patch, ChainRole::BoundarySide, {Id(1), Id(2), Id(3), Id(4),
        Id(5)}, false);
    Require(!SurfaceReadyToBuild(patch), "辺が5本の四辺面は作れない");
    Require(SurfaceCountProblemJa(patch).find("ちょうど4本") != std::string::npos,
        "多いときは「ちょうど4本」と言う");

    // 状態の行にも、その理由がそのまま出る。
    const auto ruledLines = SurfaceStatusLinesJa(ruled, false);
    const bool told = std::any_of(ruledLines.begin(), ruledLines.end(),
        [](const std::string& line) { return line.find("2本以上") != std::string::npos; });
    Require(told, "状態の行に理由が出る(足りないとき)");
}

KACHA_V2_TEST(surface_input, 押したものはいまの欄へ入り再び押すと外れる)
{
    // 引継ぎ 2026-09-17 の 1。断面を入れたあとにガイドを入れる道が無かった。
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::GuidedLoft;
    state.activeSlot = ChainRole::Section;
    state = WithSurfaceEntriesToggled(state, state.activeSlot, {Id(1)});
    state = WithSurfaceEntriesToggled(state, state.activeSlot, {Id(2)});
    Require(state.sections.size() == 2 && state.sections[0] == Id(1), "押した順に入る");
    // 欄を替えてから押すと、そちらへ入る。
    state = WithActiveSurfaceSlot(state, ChainRole::GuideU);
    Require(state.activeSlot == ChainRole::GuideU, "ガイドの欄になった");
    state = WithSurfaceEntriesToggled(state, state.activeSlot, {Id(5)});
    Require(state.guides.size() == 1 && state.sections.size() == 2, "ガイドへ入り断面は無事");
    // もう一度押すと外れる。Ctrl は要らない。
    state = WithSurfaceEntriesToggled(state, ChainRole::GuideU, {Id(5)});
    Require(state.guides.empty(), "再び押すと外れる");
}

KACHA_V2_TEST(surface_input, 一つのものは一つの欄にしか入らない)
{
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::GuidedLoft;
    state = WithSurfaceEntriesToggled(state, ChainRole::Section, {Id(1)});
    // 断面に入っている線を、ガイドの欄で押す → 断面から外れてガイドへ移る。
    state = WithSurfaceEntriesToggled(state, ChainRole::GuideU, {Id(1)});
    Require(state.sections.empty() && state.guides.size() == 1, "欄を移る");
}

KACHA_V2_TEST(surface_input, 解除は並びからも消す)
{
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::LoftSections;
    state = WithSurfaceEntries(state, ChainRole::Section, {Id(1), Id(2), Id(3)}, false);
    state.ordering = SurfaceOrdering::ManualLock;
    state.explicitOrder = {Id(3), Id(1), Id(2)};
    state = WithoutSurfaceEntries(state, {Id(1)});
    Require(state.sections.size() == 2, "断面から消える");
    Require(state.explicitOrder.size() == 2 && state.explicitOrder[0] == Id(3),
        "手動固定の並びからも消える。古い入力を残さない");
    state = WithSurfaceSlotCleared(state, ChainRole::Section);
    Require(state.sections.empty() && state.explicitOrder.empty(), "欄ごと空になる");
}

KACHA_V2_TEST(surface_input, その作り方で使わない欄は選べない)
{
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::RuledSections;
    state.activeSlot = ChainRole::Section;
    Require(!CanActivateSurfaceSlot(state, ChainRole::GuideU), "ルールドにガイド欄は無い");
    const auto same = WithActiveSurfaceSlot(state, ChainRole::GuideU);
    Require(same.activeSlot == ChainRole::Section, "黙って変えない");
    // 作り方を平面へ変えると、断面の欄は使えないので既定(境界)へ戻る。
    state.method = GuideSurfaceMethod::PlanarBoundary;
    state = WithActiveSlotSettled(state);
    Require(state.activeSlot == ChainRole::BoundarySide, "既定の欄へ戻る");
}

KACHA_V2_TEST(surface_input, 今どこへ入るかは状態の一番上に出る)
{
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::LoftSections;
    state.activeSlot = ChainRole::Section;
    state = WithSurfaceEntries(state, ChainRole::Section, {Id(1)}, false);
    const auto hint = SurfaceActiveSlotHintJa(state);
    Require(hint.find("断面") != std::string::npos && hint.find("2本目") != std::string::npos,
        std::string("次が何本目かまで言う: ") + hint);
    const auto lines = SurfaceStatusLinesJa(state, false);
    Require(!lines.empty() && lines.front().find("次のクリック") != std::string::npos,
        "一番上の行");
    Require(AllSurfaceEntries(state).size() == 1, "合計は重複なし");
    ChainRole where = ChainRole::GuideU;
    Require(SurfaceSlotHolding(state, Id(1), where) && where == ChainRole::Section,
        "どの欄に入っているか分かる");
}

KACHA_V2_TEST(surface_input, 自動のときは採用した並びを出す)
{
    // 引継ぎ 2026-09-17 の 2。押した順ではなく、検査が実際に採用した順を出す。
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::LoftSections;
    state = WithSurfaceEntries(state, ChainRole::Section, {Id(3), Id(1), Id(2)}, false);
    Require(SurfaceSectionOrder(state)[0] == Id(3), "採用順が無いうちは押した順");
    state.adoptedOrder = {Id(1), Id(2), Id(3)};   // 下見を作った側が書き戻す
    const auto shown = SurfaceSectionOrder(state);
    Require(shown[0] == Id(1) && shown[2] == Id(3), "採用した順を出す");
    // 手動固定へ替えると、その表示順がそのまま固定される(呼ぶ側が書く)。
    state.explicitOrder = shown;
    state.ordering = SurfaceOrdering::ManualLock;
    Require(SurfaceSectionOrder(state)[0] == Id(1), "表示順がそのまま生成順");
    // 解除は採用順からも消す。
    state = WithoutSurfaceEntries(state, {Id(2)});
    Require(state.adoptedOrder.size() == 2 && state.explicitOrder.size() == 2,
        "どちらの並びからも消える");
}

KACHA_V2_TEST(surface_input_state, 回転体はガイドの欄が軸になり_断面が入ると自動で軸へ移る)
{
    // 引継ぎ 2026-09-17 の 6。欄は正本の3つのまま、言葉だけ「軸」にする。
    SurfaceInputState state;
    state.method = GuideSurfaceMethod::Revolve;
    ChainRole role = ChainRole::Section;
    Require(RoleForSurfaceSlot(GuideSurfaceMethod::Revolve, ChainRole::GuideU, role),
        "回転体はガイドの欄を使う(軸)");
    Require(SurfaceSlotNameJa(GuideSurfaceMethod::Revolve, ChainRole::GuideU) == "軸",
        "回転体ではガイドの欄は「軸」と呼ぶ");
    Require(SurfaceSlotNameJa(GuideSurfaceMethod::LoftSections, ChainRole::GuideU) == "ガイド",
        "ほかの作り方ではガイドのまま");
    Require(!SurfaceReadyToBuild(state), "何も無ければ作れない");
    state = WithSurfaceEntriesToggled(state, ChainRole::Section, {Id(1)});
    Require(!SurfaceReadyToBuild(state), "断面だけでは作れない(軸が要る)");
    Require(SurfaceCountProblemJa(state).find("軸") != std::string::npos, "軸が要ると言う");
    state = WithSurfaceSlotAdvanced(state);
    Require(state.activeSlot == ChainRole::GuideU, "断面が入ったら次のクリックは軸");
    Require(SurfaceActiveSlotHintJa(state) == "次のクリック → 軸(1本目)", "案内も軸");
    state = WithSurfaceEntriesToggled(state, ChainRole::GuideU, {Id(2)});
    Require(SurfaceReadyToBuild(state), "断面1本と軸1本で作れる");
    // 2026-09-22: 断面は 1〜任意(断面ごとに同じ軸で 1 つずつ作る一括)。軸は 1 本。
    state = WithSurfaceEntriesToggled(state, ChainRole::Section, {Id(3)});
    Require(SurfaceReadyToBuild(state), "断面2本でも作れる(1つずつ作る)");
    state = WithSurfaceEntriesToggled(state, ChainRole::GuideU, {Id(4)});
    Require(!SurfaceReadyToBuild(state) && SurfaceCountProblemJa(state).find("軸は1本") != std::string::npos,
        "軸は1本だけ");
    // ロフトでは欄を動かさない。
    SurfaceInputState loft;
    loft.method = GuideSurfaceMethod::LoftSections;
    loft = WithSurfaceEntriesToggled(loft, ChainRole::Section, {Id(1)});
    Require(WithSurfaceSlotAdvanced(loft).activeSlot == ChainRole::Section,
        "ロフトは断面のまま(自動で動かさない)");
}

KACHA_V2_TEST_MAIN("surface_input_state_tests")
