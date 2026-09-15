// 押し出しの入力スロット(app/ExtrudeInputState.h)。
//
// これまでは決めるたびに選択を読み直していた。そのため
//   - 下見のあとで選択が変わると、確定は別のものを読む
//   - 素のクリックで前の入力が消えるので、Ctrl を知らないと始められない
//   - 対象と輪郭が1本の文字列なので、どちらを選び直すのか分からない
// が同時に起きていた。ここは入力を明示のスロットとして持つ。
#include "kachakacha/app/ExtrudeInputState.h"
#include "kachakacha/base/TestHarness.h"

#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#include <string>

using kachakacha::v2::app::ApplyPick;
using kachakacha::v2::app::ExtrudeInputState;
using kachakacha::v2::app::ExtrudeOutputPreset;
using kachakacha::v2::app::ExtrudeOutputPresets;
using kachakacha::v2::app::ExtrudeOutputs;
using kachakacha::v2::app::ExtrudeOutputsTextJa;
using kachakacha::v2::app::ExtrudeSlot;
using kachakacha::v2::app::ExtrudeStatusLinesJa;
using kachakacha::v2::app::NextNeededSlot;
using kachakacha::v2::app::OperationApplies;
using kachakacha::v2::app::OutputsForPreset;
using kachakacha::v2::app::PickFitsSlot;
using kachakacha::v2::app::PickedEntity;
using kachakacha::v2::app::PickedKind;
using kachakacha::v2::app::PresetForOutputs;
using kachakacha::v2::app::ReadyForPreview;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::modeling::ExtrudeBooleanMode;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

[[nodiscard]] EntityId Id(std::uint8_t value)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = value;
    return EntityId(kachakacha::v2::base::Uuid(bytes));
}

[[nodiscard]] PickedEntity Pick(std::uint8_t id, PickedKind kind)
{
    PickedEntity picked;
    picked.entityId = Id(id);
    picked.kind = kind;
    return picked;
}

} // namespace

KACHA_V2_TEST(extrude_input, 何も無ければ輪郭を求める)
{
    const ExtrudeInputState empty;
    Require(NextNeededSlot(empty) == ExtrudeSlot::Profile, "まず輪郭");
    Require(!ReadyForPreview(empty), "下見はまだ出せない");
}

KACHA_V2_TEST(extrude_input, 輪郭だけで下見が出せる)
{
    ExtrudeInputState state;
    state = ApplyPick(state, Pick(1, PickedKind::ClosedWire), false);
    Require(state.HasProfile(), "輪郭が入った");
    Require(NextNeededSlot(state) == ExtrudeSlot::None, "足りないものは無い");
    Require(ReadyForPreview(state), "下見が出せる");
}

KACHA_V2_TEST(extrude_input, 素のクリックで前の入力が消えない)
{
    // ここが本題。立体を選んでから輪郭を **Ctrl 無しで** クリックしても、
    // 立体が外れてはいけない。外れるので Ctrl を知らないと押し出せなかった。
    ExtrudeInputState state;
    state.operation = ExtrudeBooleanMode::SubtractFromPart;
    state = ApplyPick(state, Pick(10, PickedKind::Solid), false);
    Require(state.HasTarget(), "立体が対象へ入った");
    Require(NextNeededSlot(state) == ExtrudeSlot::Profile, "次は輪郭を求める");

    state = ApplyPick(state, Pick(20, PickedKind::ClosedWire), false);
    Require(state.HasProfile(), "輪郭が入った");
    Require(state.HasTarget(), "**立体はそのまま残っている**");
    Require(*state.target == Id(10), "同じ立体");
    Require(ReadyForPreview(state), "Ctrl を使わずに下見まで行けた");
}

KACHA_V2_TEST(extrude_input, 輪郭を足すのは_Ctrl_のときだけ)
{
    ExtrudeInputState state;
    state = ApplyPick(state, Pick(1, PickedKind::ClosedWire), false);
    state = ApplyPick(state, Pick(2, PickedKind::ClosedWire), false);
    RequireEqual(std::to_string(state.profiles.size()), "1", "素のクリックは置き換え");
    state = ApplyPick(state, Pick(3, PickedKind::ClosedWire), true);
    RequireEqual(std::to_string(state.profiles.size()), "2", "Ctrl は足す");
    // 同じものを2度足しても増えない。
    state = ApplyPick(state, Pick(3, PickedKind::ClosedWire), true);
    RequireEqual(std::to_string(state.profiles.size()), "2", "同じものは増えない");
}

KACHA_V2_TEST(extrude_input, 面を拾うと輪郭と対象の両方が決まる)
{
    ExtrudeInputState state;
    PickedEntity face = Pick(7, PickedKind::SolidFace);
    face.faceIndex = 3;
    state = ApplyPick(state, face, false);
    Require(state.profileIsFace, "面を押す");
    Require(state.faceIndex.has_value() && *state.faceIndex == 3, "面の番号を持つ");
    Require(state.HasTarget() && *state.target == Id(7), "その面の立体が対象");
}

KACHA_V2_TEST(extrude_input, スロットに合わないものは候補にしない)
{
    // 拾う候補を絞るのに使う。輪郭を求めているときに立体を先に出さない。
    Require(PickFitsSlot(ExtrudeSlot::Profile, PickedKind::ClosedWire), "閉じた輪郭は輪郭");
    Require(PickFitsSlot(ExtrudeSlot::Profile, PickedKind::SolidFace), "面も輪郭の代わり");
    Require(!PickFitsSlot(ExtrudeSlot::Profile, PickedKind::OpenWire), "開いた輪郭は不可");
    Require(!PickFitsSlot(ExtrudeSlot::Profile, PickedKind::Solid), "立体は輪郭ではない");
    Require(PickFitsSlot(ExtrudeSlot::Target, PickedKind::Solid), "立体は対象");
    Require(!PickFitsSlot(ExtrudeSlot::Target, PickedKind::ClosedWire), "輪郭は対象でない");
}

KACHA_V2_TEST(extrude_input, 足す引くには相手が要る)
{
    ExtrudeInputState state;
    state = ApplyPick(state, Pick(1, PickedKind::ClosedWire), false);
    state.operation = ExtrudeBooleanMode::AddToPart;
    Require(NextNeededSlot(state) == ExtrudeSlot::Target, "相手を求める");
    Require(!ReadyForPreview(state), "相手がいないうちは下見を出さない");
    state = ApplyPick(state, Pick(2, PickedKind::Solid), false);
    Require(ReadyForPreview(state), "相手がそろえば出せる");
}

KACHA_V2_TEST(extrude_input, 出力プリセットの中身が仕様どおり)
{
    const auto solid = OutputsForPreset(ExtrudeOutputPreset::SolidOnly);
    Require(solid.body && !solid.startWire && !solid.endWire && !solid.sideWires,
        "ソリッドのみ");
    const auto wires = OutputsForPreset(ExtrudeOutputPreset::WiresOnly);
    Require(!wires.body && wires.startWire && wires.endWire && wires.sideWires,
        "ワイヤーのみ");
    const auto both = OutputsForPreset(ExtrudeOutputPreset::WiresAndSolid);
    Require(both.body && both.startWire && both.endWire && both.sideWires,
        "ワイヤー + ソリッド");
    const auto endOnly = OutputsForPreset(ExtrudeOutputPreset::EndWireOnly);
    Require(!endOnly.body && !endOnly.startWire && endOnly.endWire && !endOnly.sideWires,
        "押し出し先ワイヤーのみ");
}

KACHA_V2_TEST(extrude_input, 出力からプリセットを言い当てる)
{
    // 欄を手で触った結果が、たまたまプリセットと同じなら、その名前を出す。
    for (const ExtrudeOutputPreset preset : ExtrudeOutputPresets()) {
        if (preset == ExtrudeOutputPreset::Custom) {
            continue;
        }
        Require(PresetForOutputs(OutputsForPreset(preset)) == preset, "往復する");
    }
    ExtrudeOutputs odd;
    odd.body = false;
    odd.startWire = true;
    odd.endWire = true;
    odd.sideWires = false;
    Require(PresetForOutputs(odd) == ExtrudeOutputPreset::Custom, "どれでもなければカスタム");
}

KACHA_V2_TEST(extrude_input, 全部_OFF_は確定できず理由が出る)
{
    ExtrudeInputState state;
    state = ApplyPick(state, Pick(1, PickedKind::ClosedWire), false);
    state.outputs = ExtrudeOutputs{};
    state.outputs.body = false;
    Require(!state.outputs.Any(), "何も作らない");
    const auto lines = ExtrudeStatusLinesJa(state, {}, {"輪郭1"}, false);
    const bool said = std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
        return line.find("作るものが1つも") != std::string::npos;
    });
    Require(said, "理由が出る");
}

KACHA_V2_TEST(extrude_input, ソリッドを作らないなら演算は効かない)
{
    ExtrudeInputState state;
    state = ApplyPick(state, Pick(1, PickedKind::ClosedWire), false);
    state.operation = ExtrudeBooleanMode::SubtractFromPart;
    state = ApplyPick(state, Pick(2, PickedKind::Solid), false);
    state.outputs = OutputsForPreset(ExtrudeOutputPreset::EndWireOnly);
    Require(!OperationApplies(state), "演算は起きない");
    const auto lines = ExtrudeStatusLinesJa(state, "部品1", {"輪郭1"}, true);
    const bool said = std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
        return line.find("適用なし") != std::string::npos;
    });
    Require(said, "適用なしと、その理由が出る");
}

KACHA_V2_TEST(extrude_input, 状態欄は通った道も言う)
{
    // 診断コードだけに頼らない(オーナー指示 §15)。
    ExtrudeInputState state;
    state = ApplyPick(state, Pick(1, PickedKind::ClosedWire), false);
    state.operation = ExtrudeBooleanMode::SubtractFromPart;
    state = ApplyPick(state, Pick(2, PickedKind::Solid), false);
    const auto lines = ExtrudeStatusLinesJa(state, "部品1", {"輪郭4"}, true);
    const auto has = [&lines](const char* piece) {
        return std::any_of(lines.begin(), lines.end(), [piece](const std::string& line) {
            return line.find(piece) != std::string::npos;
        });
    };
    Require(has("対象: 部品1"), "対象が出る");
    Require(has("輪郭: 輪郭4"), "輪郭が出る");
    Require(has("入力は有効"), "通っていることが出る");
    Require(has("下見を表示中"), "下見の状態が出る");
    Require(has("引く"), "確定すると何が起きるかが出る");
    Require(has("出力:"), "何を作るかが出る");
}

KACHA_V2_TEST(extrude_input, 足りないものを名前で言う)
{
    ExtrudeInputState state;
    const auto lines = ExtrudeStatusLinesJa(state, {}, {}, false);
    const bool said = std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
        return line.find("輪郭を選んでください") != std::string::npos;
    });
    Require(said, "何を選べばよいかを言う");
    Require(!ExtrudeOutputsTextJa(OutputsForPreset(ExtrudeOutputPreset::SolidOnly)).empty(),
        "出力の一言が出る");
}

KACHA_V2_TEST(extrude_input, 候補はスロットに合う順へ並べ替える)
{
    using kachakacha::v2::app::SortKindsForSlot;
    // ふだんの並びは 点 → 線 → 形 で固定だった。面の上に線が載っていると
    // 線が先に取れるので、面を押したいのに元の輪郭が選ばれていた。
    const std::vector<PickedKind> asPicked{PickedKind::ClosedWire, PickedKind::SolidFace,
        PickedKind::Solid};

    // 輪郭を求めているとき。閉じた輪郭と面が前に出る。
    const auto forProfile = SortKindsForSlot(ExtrudeSlot::Profile, asPicked);
    Require(forProfile.size() == asPicked.size(), "**捨てない**");
    Require(forProfile.front() == PickedKind::ClosedWire, "合うものが先頭");
    Require(forProfile.back() == PickedKind::Solid, "合わないものは後ろ");

    // 対象を求めているとき。立体と面が前に出る。
    const auto forTarget = SortKindsForSlot(ExtrudeSlot::Target, asPicked);
    Require(forTarget.size() == asPicked.size(), "**捨てない**");
    Require(forTarget.front() == PickedKind::SolidFace || forTarget.front() == PickedKind::Solid,
        "立体側が先頭");
    Require(std::find(forTarget.begin(), forTarget.end(), PickedKind::ClosedWire)
            != forTarget.end(),
        "輪郭も並びに残る(Tab で届く)");

    // 合うものが1つも無ければ、そのままの並び。
    const std::vector<PickedKind> none{PickedKind::OpenWire, PickedKind::Other};
    const auto unchanged = SortKindsForSlot(ExtrudeSlot::Profile, none);
    Require(unchanged == none, "並びを変えない");
}

KACHA_V2_TEST(extrude_input, 輪郭が入れば求めるものが変わる)
{
    // 拾う候補の並べ替えは、いま足りないスロットに合わせる(§6)。
    // ずっと「輪郭」に留めておくと、輪郭が入ったあとに相手の立体を押しても
    // **その面が輪郭として拾われ**、「面と輪郭の両方」で止まってしまう。
    ExtrudeInputState state;
    Require(NextNeededSlot(state) == ExtrudeSlot::Profile, "はじめは輪郭");
    state.profiles = {Id(1)};
    Require(NextNeededSlot(state) == ExtrudeSlot::None,
        "新しい部品なら、輪郭が入れば足りている");
    state.operation = kachakacha::v2::modeling::ExtrudeBooleanMode::SubtractFromPart;
    Require(NextNeededSlot(state) == ExtrudeSlot::Target, "引くなら相手が要る");
    state.target = Id(2);
    Require(NextNeededSlot(state) == ExtrudeSlot::None, "相手が入れば足りている");
}

KACHA_V2_TEST(extrude_input, 素のクリックで足すかは道具が動いているかで決まる)
{
    using kachakacha::v2::app::PickedKind;
    using kachakacha::v2::app::PlainClickShouldAdd;
    // 道具が動いていなければ、いつもどおり置き換える。
    Require(!PlainClickShouldAdd(false, PickedKind::Solid, {PickedKind::ClosedWire}),
        "道具が動いていない");
    // 役割が違えば足す。輪郭を選んだあとに相手の立体を素で押しても消えない。
    Require(PlainClickShouldAdd(true, PickedKind::Solid, {PickedKind::ClosedWire}),
        "輪郭のあとに立体");
    Require(PlainClickShouldAdd(true, PickedKind::ClosedWire, {PickedKind::Solid}),
        "立体のあとに輪郭");
    // 同じ役割なら置き換える。輪郭を選び直せる。
    Require(!PlainClickShouldAdd(true, PickedKind::ClosedWire, {PickedKind::OpenWire}),
        "線と線");
    Require(!PlainClickShouldAdd(true, PickedKind::Solid, {PickedKind::SolidFace}),
        "立体と面は同じ役割");
    // 何も選んでいなければ足すも何もない。
    Require(!PlainClickShouldAdd(true, PickedKind::Solid, {}), "まだ何も無い");
    // **ここが落とし穴だった。**
    // 「いま足りないスロット」で判断すると、輪郭が入った時点で None になり、
    // 次に相手の立体を押した瞬間に輪郭が消える。判断に使うのは道具の動作である。
    ExtrudeInputState state;
    state.profiles = {Id(1)};
    Require(NextNeededSlot(state) == ExtrudeSlot::None, "スロットは足りている");
    Require(PlainClickShouldAdd(true, PickedKind::Solid, {PickedKind::ClosedWire}),
        "それでも足せる");
}

KACHA_V2_TEST(extrude_input, 輪郭が入っているとき拾った面は相手の立体になる)
{
    using kachakacha::v2::app::FacePickMeansItsSolid;
    using kachakacha::v2::app::PickedKind;
    // 立体を素で押すと、いちばん手前の面が拾える。押す面を選ぶ道である。
    Require(!FacePickMeansItsSolid(true, PickedKind::SolidFace, {}),
        "何も無ければ、面は面のまま");
    Require(!FacePickMeansItsSolid(true, PickedKind::SolidFace, {PickedKind::Solid}),
        "立体しか無くても、面は面のまま");
    // **ここが直したところ。**
    // 輪郭がもう入っているなら、その面は押す相手ではありえない。
    // そのまま面として受けると「面と輪郭の両方が選ばれています」で止まり、
    // 人には理由が分からない。
    Require(FacePickMeansItsSolid(true, PickedKind::SolidFace, {PickedKind::ClosedWire}),
        "輪郭が入っていれば、面はその立体を指す");
    Require(!FacePickMeansItsSolid(false, PickedKind::SolidFace, {PickedKind::ClosedWire}),
        "道具が動いていなければ、いつもどおり面を拾う");
    Require(!FacePickMeansItsSolid(true, PickedKind::Solid, {PickedKind::ClosedWire}),
        "面でなければ関係ない");
}

KACHA_V2_TEST_MAIN("extrude_input_state_tests")
