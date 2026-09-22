// 「辺の丸め・面取り」の入力の状態(app/EdgeFinishInputState、matrix P-12)。
#include "kachakacha/app/EdgeFinishInputState.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::app::EdgeFinishFooterLine;
using kachakacha::v2::app::EdgeFinishInputState;
using kachakacha::v2::app::EdgeFinishKindForCommand;
using kachakacha::v2::app::EdgeFinishOutcome;
using kachakacha::v2::app::EdgeFinishReady;
using kachakacha::v2::app::EdgeFinishStatusLinesJa;
using kachakacha::v2::app::WithEdgeFinishPick;
using kachakacha::v2::app::WithoutEdgeFinishEdges;
using kachakacha::v2::app::WithoutEdgeFinishPart;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;

namespace {

[[nodiscard]] EntityId Id(int n)
{
    kachakacha::v2::base::DeterministicIdGenerator ids{static_cast<std::uint64_t>(n)};
    return ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
}

} // namespace

KACHA_V2_TEST(edge_finish_state, 部品を押すと相手と近い辺が入り同じ辺をもう一度押すと外れる)
{
    EdgeFinishInputState state;
    Require(!EdgeFinishReady(state), "空では作れない");
    state = WithEdgeFinishPick(state, Id(1), Vector3{20, 0, 30}, 0.01);
    Require(state.part == Id(1) && state.edges.size() == 1 && EdgeFinishReady(state),
        "部品と辺が 1 本入り作れる");
    state = WithEdgeFinishPick(state, Id(1), Vector3{20, 20, 30}, 0.01);
    Require(state.edges.size() == 2, "同じ部品の別の辺が足される(何本でも)");
    state = WithEdgeFinishPick(state, Id(1), Vector3{20.001, 0, 30}, 0.01);
    Require(state.edges.size() == 1 && state.edges.front().y == 20.0, "入っている辺の近くを押すと外れる");
    state = WithEdgeFinishPick(state, Id(2), Vector3{0, 5, 0}, 0.01);
    Require(state.part == Id(2) && state.edges.size() == 1 && state.edges.front().y == 5.0,
        "別の部品を押すと相手が替わり辺は持ち越さない");
    state = WithEdgeFinishPick(state, Id(2), std::nullopt, 0.01);
    Require(state.edges.size() == 1, "辺が見つからない押しは辺を変えない");
    Require(WithoutEdgeFinishEdges(state).edges.empty() && WithoutEdgeFinishEdges(state).part == Id(2),
        "辺の解除は部品を残す");
    Require(WithoutEdgeFinishPart(state).part.IsNil() && WithoutEdgeFinishPart(state).edges.empty(),
        "部品の解除は辺も空にする");
    state.sizeMm = 0.0;
    Require(!EdgeFinishReady(state), "大きさ 0 では作れない");
}

KACHA_V2_TEST(edge_finish_state, 命令と一行と状態が作り方を言う)
{
    int kind = -1;
    Require(EdgeFinishKindForCommand("part.fillet", kind) && kind == 0
            && EdgeFinishKindForCommand("part.chamfer", kind) && kind == 1
            && !EdgeFinishKindForCommand("part.extrude", kind),
        "命令の名前から種類");
    EdgeFinishInputState state;
    state.kind = 1;
    state.sizeMm = 2.0;
    state = WithEdgeFinishPick(state, Id(3), Vector3{1, 2, 3}, 0.01);
    EdgeFinishOutcome outcome;
    outcome.evaluated = true;
    outcome.available = true;
    outcome.previousVolumeMm3 = 24000.0;
    outcome.volumeMm3 = 23920.0;
    const auto footer = EdgeFinishFooterLine(state, "箱", outcome, true);
    Require(footer == "面取り: PART=箱 / EDGES=1 / C 2.000 mm / VOLUME=23920.0000 mm3 / Preview only",
        "一行: " + footer);
    bool sawVolume = false;
    for (const auto& line : EdgeFinishStatusLinesJa(state, outcome, true)) {
        sawVolume = sawVolume || line.find("24000.0000 mm3 → 23920.0000 mm3") != std::string::npos;
    }
    Require(sawVolume, "状態に体積の変わり方が出る");
    const auto waiting = EdgeFinishFooterLine(EdgeFinishInputState{}, "", EdgeFinishOutcome{}, false);
    Require(waiting.find("NEXT=PART") != std::string::npos, "最初は部品待ち: " + waiting);
}

KACHA_V2_TEST_MAIN("edge_finish_input_state_tests")
