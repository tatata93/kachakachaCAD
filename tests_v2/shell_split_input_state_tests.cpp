// 「シェル・分割」の入力の状態(app/ShellSplitInputState、matrix P-13)。
#include "kachakacha/app/ShellSplitInputState.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::app::ShellSplitFooterLine;
using kachakacha::v2::app::ShellSplitInputState;
using kachakacha::v2::app::ShellSplitMethodForCommand;
using kachakacha::v2::app::ShellSplitMethodNameJa;
using kachakacha::v2::app::ShellSplitOutcome;
using kachakacha::v2::app::ShellSplitReady;
using kachakacha::v2::app::ShellSplitStatusLinesJa;
using kachakacha::v2::app::WithoutShellSplitFaces;
using kachakacha::v2::app::WithoutShellSplitPart;
using kachakacha::v2::app::WithShellSplitMethod;
using kachakacha::v2::app::WithShellSplitPick;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::test::Require;

namespace {

[[nodiscard]] EntityId Id(int n)
{
    kachakacha::v2::base::DeterministicIdGenerator ids{static_cast<std::uint64_t>(n)};
    return ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
}

[[nodiscard]] bool AnyLineHas(const std::vector<std::string>& lines, const std::string& text)
{
    for (const auto& line : lines) {
        if (line.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

KACHA_V2_TEST(shell_split_state, シェルは部品を押すと面が入り同じ面を押すと外れる)
{
    ShellSplitInputState state;
    Require(!ShellSplitReady(state), "空では作れない");
    state = WithShellSplitPick(state, Id(1), Vector3{20, 10, 30}, std::nullopt);
    Require(state.part == Id(1) && state.faces.size() == 1 && ShellSplitReady(state),
        "部品と面が 1 枚入り作れる");
    state = WithShellSplitPick(state, Id(1), Vector3{0, 10, 15}, std::nullopt);
    Require(state.faces.size() == 2, "同じ部品の別の面が足される(何枚でも)");
    state = WithShellSplitPick(state, Id(1), Vector3{21, 11, 30}, std::size_t{0});
    Require(state.faces.size() == 1 && state.faces.front().x == 0.0,
        "入っている面と同じ面を押すと外れる(同じかどうかは核が決めて渡す)");
    state = WithShellSplitPick(state, Id(2), Vector3{5, 5, 5}, std::nullopt);
    Require(state.part == Id(2) && state.faces.size() == 1 && state.faces.front().x == 5.0,
        "別の部品を押すと相手が替わり面は持ち越さない");
    state = WithShellSplitPick(state, Id(2), std::nullopt, std::nullopt);
    Require(state.faces.size() == 1, "面が見つからない押しは面を変えない");
    state = WithShellSplitPick(state, Id(2), Vector3{6, 6, 6}, std::size_t{7});
    Require(state.faces.size() == 2, "範囲外の番号は外す扱いにしない(足す)");
    Require(WithoutShellSplitFaces(state).faces.empty() && WithoutShellSplitFaces(state).part == Id(2),
        "面の解除は部品を残す");
    Require(WithoutShellSplitPart(state).part.IsNil() && WithoutShellSplitPart(state).faces.empty(),
        "部品の解除は面も空にする");
    state.thicknessMm = 0.0;
    Require(!ShellSplitReady(state), "肉厚 0 では作れない");
}

KACHA_V2_TEST(shell_split_state, 分割は部品だけで作れて面を持たない)
{
    ShellSplitInputState state = WithShellSplitMethod(ShellSplitInputState{}, 1);
    Require(!ShellSplitReady(state), "部品が無ければ作れない");
    state = WithShellSplitPick(state, Id(1), Vector3{1, 2, 3}, std::nullopt);
    Require(state.part == Id(1) && state.faces.empty() && ShellSplitReady(state),
        "分割は部品だけ(平面は作業平面から)");
    ShellSplitInputState shell;
    shell = WithShellSplitPick(shell, Id(1), Vector3{1, 2, 3}, std::nullopt);
    const auto switched = WithShellSplitMethod(shell, 1);
    Require(switched.method == 1 && switched.faces.empty() && switched.part == Id(1),
        "作り方を替えると面は持ち越さない(部品は残す)");
    Require(WithShellSplitMethod(shell, 0).faces.size() == 1, "同じ作り方を押しても面は消えない");
}

KACHA_V2_TEST(shell_split_state, 命令と一行と状態が作り方を言う)
{
    int method = -1;
    Require(ShellSplitMethodForCommand("part.shell", method) && method == 0
            && ShellSplitMethodForCommand("part.split", method) && method == 1
            && !ShellSplitMethodForCommand("part.fillet", method),
        "命令の名前から作り方");
    Require(ShellSplitMethodNameJa(0) == "シェル" && ShellSplitMethodNameJa(1) == "分割", "作り方の名前");

    ShellSplitInputState shell;
    shell.thicknessMm = 2.0;
    shell = WithShellSplitPick(shell, Id(3), Vector3{1, 2, 3}, std::nullopt);
    ShellSplitOutcome outcome;
    outcome.evaluated = true;
    outcome.available = true;
    outcome.previousVolumeMm3 = 24000.0;
    outcome.volumeMm3 = 7872.0;
    const auto footer = ShellSplitFooterLine(shell, "箱", outcome, true);
    Require(footer == "シェル: PART=箱 / FACES=1 / T 2.000 mm / VOLUME=7872.0000 mm3 / Preview only",
        "シェルの一行: " + footer);
    Require(AnyLineHas(ShellSplitStatusLinesJa(shell, outcome, true), "24000.0000 mm3 → 7872.0000 mm3"),
        "状態に体積の変わり方が出る");

    ShellSplitInputState split = WithShellSplitMethod(ShellSplitInputState{}, 1);
    split.splitOffsetMm = -5.0;
    split = WithShellSplitPick(split, Id(3), std::nullopt, std::nullopt);
    ShellSplitOutcome pieces;
    pieces.evaluated = true;
    pieces.available = true;
    pieces.volumeMm3 = 500.0;
    pieces.otherVolumeMm3 = 1000.0;
    pieces.positivePieces = 2;
    pieces.negativePieces = 1;
    const auto splitFooter = ShellSplitFooterLine(split, "U", pieces, true);
    Require(splitFooter
            == "分割: PART=U / PLANE=作業平面 -5.000 mm / VOLUME=500.0000 mm3 + 1000.0000 mm3 / Preview only",
        "分割の一行: " + splitFooter);
    const auto lines = ShellSplitStatusLinesJa(split, pieces, true);
    Require(AnyLineHas(lines, "離れた塊に分かれます"), "片側が塊に分かれることを知らせる");
    Require(AnyLineHas(lines, "いまの作業平面から -5.000 mm"), "平面の位置を言う");

    ShellSplitOutcome refused;
    refused.evaluated = true;
    refused.refusalJa = "分ける平面が部品を通っていないので、2 つに分かれません。";
    Require(AnyLineHas(ShellSplitStatusLinesJa(split, refused, false), "× 分ける平面が部品を通っていない"),
        "作れない理由をそのまま出す");
    const auto waiting = ShellSplitFooterLine(ShellSplitInputState{}, "", ShellSplitOutcome{}, false);
    Require(waiting.find("NEXT=PART") != std::string::npos, "最初は部品待ち: " + waiting);
    const auto needFace = ShellSplitFooterLine(WithShellSplitPick(ShellSplitInputState{}, Id(4), std::nullopt,
                                                 std::nullopt), "箱", ShellSplitOutcome{}, false);
    Require(needFace.find("NEXT=FACE") != std::string::npos, "シェルは次に面待ち: " + needFace);
}

KACHA_V2_TEST_MAIN("shell_split_input_state_tests")
