// 「立体を作る」の入力の状態(app/SolidInputState)。3D で押したものが種類で欄に入る。
#include "kachakacha/app/SolidInputState.h"
#include "kachakacha/base/TestHarness.h"

#include <string>
#include <vector>

using kachakacha::v2::app::NextSolidSlot;
using kachakacha::v2::app::RevolveMode;
using kachakacha::v2::app::SolidAngleRad;
using kachakacha::v2::app::SolidEntries;
using kachakacha::v2::app::SolidFooterLine;
using kachakacha::v2::app::SolidInputState;
using kachakacha::v2::app::SolidPickKind;
using kachakacha::v2::app::SolidPreviewOutcome;
using kachakacha::v2::app::SolidReady;
using kachakacha::v2::app::SolidSlot;
using kachakacha::v2::app::SolidStatusLinesJa;
using kachakacha::v2::app::RevolveModeOfCard;
using kachakacha::v2::app::SolidMethodCardIndex;
using kachakacha::v2::app::SolidMethodCards;
using kachakacha::v2::app::SolidMethodForCommand;
using kachakacha::v2::app::WithActiveSolidSlot;
using kachakacha::v2::app::WithSolidBoolean;
using kachakacha::v2::app::WithSolidMethod;
using kachakacha::v2::app::WithSolidPick;
using kachakacha::v2::app::WithSolidSlotCleared;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::modeling::SolidMethod;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] EntityId Id(int n)
{
    kachakacha::v2::base::DeterministicIdGenerator ids{static_cast<std::uint64_t>(n)};
    return ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
}

} // namespace

KACHA_V2_TEST(solid_state, 回転体は閉じた線が輪郭に直線が軸に入り押し直すと外れる)
{
    SolidInputState state;
    Require(NextSolidSlot(state) == SolidSlot::Profiles, "最初は輪郭");
    state = WithSolidPick(state, Id(1), SolidPickKind::ClosedWire);
    Require(state.profiles.size() == 1 && NextSolidSlot(state) == SolidSlot::Axis, "輪郭が入り次は軸");
    Require(!SolidReady(state), "軸が無いうちは作れない");
    state = WithSolidPick(state, Id(2), SolidPickKind::LineWire);
    Require(state.axis == Id(2) && SolidReady(state), "直線が軸に入り作れる");
    std::string why;
    const auto refused = WithSolidPick(state, Id(3), SolidPickKind::OpenWire, &why);
    Require(refused.profiles.size() == 1 && refused.axis == Id(2) && !why.empty(),
        "曲がった開いた線は回転体に入らず理由を言う: " + why);
    const auto part = WithSolidPick(state, Id(4), SolidPickKind::Part, &why);
    Require(part.target.IsNil() && why.find("足す") != std::string::npos,
        "新しい部品のときは部品を相手に入れない");
    state = WithSolidPick(state, Id(2), SolidPickKind::LineWire);
    Require(state.axis.IsNil() && NextSolidSlot(state) == SolidSlot::Axis, "押し直すと軸が外れる");
    Require(SolidEntries(WithSolidPick(state, Id(2), SolidPickKind::LineWire)).size() == 2,
        "入っているもの(輪郭・軸)");
}

KACHA_V2_TEST(solid_state, ロフト立体は断面2つ以上で押した順に並びスイープは経路を何本でも)
{
    SolidInputState loft;
    loft.method = SolidMethod::Loft;
    loft = WithSolidPick(loft, Id(1), SolidPickKind::ClosedWire);
    Require(!SolidReady(loft), "断面 1 つでは作れない");
    loft = WithSolidPick(loft, Id(2), SolidPickKind::ClosedWire);
    loft = WithSolidPick(loft, Id(3), SolidPickKind::ClosedWire);
    Require(SolidReady(loft) && loft.profiles.size() == 3 && loft.profiles[1] == Id(2),
        "3 つが押した順に並ぶ");
    SolidInputState sweep;
    sweep.method = SolidMethod::Sweep;
    sweep = WithSolidPick(sweep, Id(1), SolidPickKind::ClosedWire);
    sweep = WithSolidPick(sweep, Id(2), SolidPickKind::LineWire);
    sweep = WithSolidPick(sweep, Id(3), SolidPickKind::OpenWire);
    Require(SolidReady(sweep) && sweep.path.size() == 2, "経路は直線も曲線も何本でも");
    sweep = WithSolidSlotCleared(sweep, SolidSlot::Path);
    Require(sweep.path.empty() && NextSolidSlot(sweep) == SolidSlot::Path, "解除すると経路待ち");
}

KACHA_V2_TEST(solid_state, 足す引くのときは部品が相手に入り角度と一行が作り方を言う)
{
    SolidInputState state;
    state.booleanMode = 2;
    state = WithSolidPick(state, Id(1), SolidPickKind::ClosedWire);
    state = WithSolidPick(state, Id(2), SolidPickKind::LineWire);
    Require(!SolidReady(state) && NextSolidSlot(state) == SolidSlot::Target, "引くときは相手を待つ");
    state = WithSolidPick(state, Id(9), SolidPickKind::Part);
    Require(state.target == Id(9) && SolidReady(state), "部品が相手に入る");
    // 「ここへ選ぶ」で輪郭を明示すると、閉じた線はそこへ(輪郭は何個でも)。
    state = WithActiveSolidSlot(state, SolidSlot::Profiles);
    state = WithSolidPick(state, Id(5), SolidPickKind::ClosedWire);
    Require(state.profiles.size() == 2 && !state.activeSlot.has_value(), "明示した欄へ入り明示は解ける");
    RequireNear(SolidAngleRad(state), 2.0 * 3.14159265358979323846, 1.0e-12, "全回転は 360°");
    state.revolveMode = RevolveMode::Symmetric;
    state.angleDeg = 90.0;
    RequireNear(SolidAngleRad(state), 3.14159265358979323846 / 2.0, 1.0e-12, "角度は度から");
    SolidPreviewOutcome ok;
    ok.evaluated = true;
    ok.available = true;
    ok.volumeMm3 = 1234.5;
    const auto footer = SolidFooterLine(state, "矩形", "軸", ok, true);
    Require(footer == "回転体: PROFILE=矩形 / AXIS=軸 / 90.0° SYM / CUT / VOLUME=1234.5000 mm3 / Preview only",
        "一行: " + footer);
    bool sawVolume = false;
    for (const auto& line : SolidStatusLinesJa(state, ok, true)) {
        sawVolume = sawVolume || line.find("1234.5000 mm3") != std::string::npos;
    }
    Require(sawVolume, "状態に体積が出る");
}

KACHA_V2_TEST(solid_state, 作り方のカードは正本の3枚でまだ作れないものは理由つきで押せない)
{
    int unavailable = 0;
    for (const SolidMethod method : {SolidMethod::Revolve, SolidMethod::Loft, SolidMethod::Sweep}) {
        const auto& cards = SolidMethodCards(method);
        Require(cards[0].available, "先頭のカードはいつも作れる");
        for (const auto& card : cards) {
            Require(!card.labelJa.empty() && !card.tipJa.empty(), "字と説明がある");
            if (!card.available) {
                ++unavailable;
                Require(std::string(card.tipJa).find("まだ作れません") == 0,
                    "作れないカードは理由を言う: " + std::string(card.tipJa));
            }
        }
    }
    Require(SolidMethodCards(SolidMethod::Revolve)[2].labelJa == "対称回転"
            && SolidMethodCards(SolidMethod::Revolve)[2].available,
        "回転体の 3 枚は全部作れる");
    Require(unavailable == 4, "ロフト立体のガイド付き・中心線付き、スイープのねじれ・ガイド付きは押せない");
    SolidInputState state;
    Require(SolidMethodCardIndex(state) == 0, "全回転は 1 枚目");
    state.revolveMode = RevolveModeOfCard(2);
    Require(state.revolveMode == RevolveMode::Symmetric && SolidMethodCardIndex(state) == 2,
        "カード番号と回し方が往復する");
    SolidMethod method = SolidMethod::Revolve;
    Require(SolidMethodForCommand("part.sweep", method) && method == SolidMethod::Sweep
            && SolidMethodForCommand("part.loft_solid", method) && method == SolidMethod::Loft
            && !SolidMethodForCommand("part.extrude", method),
        "命令の名前から作り方");
}

KACHA_V2_TEST(solid_state, 作り方を替えると使わない欄が空になり新しい部品に戻すと相手が外れる)
{
    SolidInputState state;
    state = WithSolidPick(state, Id(1), SolidPickKind::ClosedWire);
    state = WithSolidPick(state, Id(2), SolidPickKind::LineWire);
    state = WithSolidBoolean(state, 1);
    state = WithSolidPick(state, Id(3), SolidPickKind::Part);
    Require(state.target == Id(3) && SolidReady(state), "足すの相手が入る");
    const auto sweep = WithSolidMethod(state, SolidMethod::Sweep);
    Require(sweep.profiles.size() == 1 && sweep.axis.IsNil() && sweep.path.empty()
            && sweep.target == Id(3) && NextSolidSlot(sweep) == SolidSlot::Path,
        "スイープへ替えると輪郭と相手は残り、軸は空になって経路待ち");
    const auto fresh = WithSolidBoolean(state, 0);
    Require(fresh.target.IsNil() && fresh.booleanMode == 0 && SolidReady(fresh),
        "新しい部品に戻すと相手は空になり、相手なしで作れる");
    Require(WithSolidBoolean(state, 7).booleanMode == 0, "知らない操作は新しい部品");
}

KACHA_V2_TEST_MAIN("solid_input_state_tests")
