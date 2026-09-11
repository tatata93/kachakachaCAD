// 選択(ui-workflows §2、V1同等)。
#include "kachakacha/app/Selection.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/modeling/SubshapeKey.h"

#include <array>
#include <string>

using kachakacha::v2::app::ApplySelection;
using kachakacha::v2::app::IsSelected;
using kachakacha::v2::app::PickCandidate;
using kachakacha::v2::app::PickCurve;
using kachakacha::v2::app::PruneSelection;
using kachakacha::v2::app::SelectAllOfKind;
using kachakacha::v2::app::SelectedCountOfKind;
using kachakacha::v2::app::SelectedCurves;
using kachakacha::v2::app::SelectionElementKind;
using kachakacha::v2::app::SelectionItemCount;
using kachakacha::v2::app::SelectionMode;
using kachakacha::v2::app::SelectionRef;
using kachakacha::v2::app::SelectionSet;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::SegmentId;
using kachakacha::v2::document::DocumentSnapshot;
using kachakacha::v2::domain::Entity;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::MakeOrthographicMapping;
using kachakacha::v2::geometry::ScreenMapping;
using kachakacha::v2::geometry::ScreenPoint;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::SnapCurve;
using kachakacha::v2::modeling::SnapScene;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

//! 試験用のID。番号から決まったものを作る。毎回同じになる。
[[nodiscard]] EntityId Ent(std::uint8_t number)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = number;
    return EntityId(kachakacha::v2::base::Uuid(bytes));
}

[[nodiscard]] SegmentId Seg(std::uint8_t number)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[14] = number;
    return SegmentId(kachakacha::v2::base::Uuid(bytes));
}

//! 上から見た平行投影。100mm 幅を 1000px に写す。1mm = 10px。
[[nodiscard]] ScreenMapping TopView()
{
    return MakeOrthographicMapping({0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 100.0, 1000.0, 1000.0);
}

[[nodiscard]] CurveSegment Line(Vector3 start, Vector3 end)
{
    const auto made = CurveSegment::MakeLine(start, end);
    Require(made.HasValue(), "線が作れる");
    return made.Value();
}

[[nodiscard]] SnapCurve Curve(std::uint8_t id, Vector3 start, Vector3 end)
{
    return SnapCurve{Ent(id), Seg(id), Line(start, end), false};
}

//! X軸に沿う線が y=0 に、もう1本が y=20 にある場面。
[[nodiscard]] SnapScene TwoLines()
{
    SnapScene scene;
    scene.curves.push_back(Curve(1, {-40, 0, 0}, {40, 0, 0}));
    scene.curves.push_back(Curve(2, {-40, 20, 0}, {40, 20, 0}));
    return scene;
}

//! 同じワイヤーに2線分がある場面。部分選択を物体IDへ潰さない試験に使う。
[[nodiscard]] SnapScene TwoSegmentsOneWire()
{
    SnapScene scene;
    scene.curves.push_back({Ent(1), Seg(1), Line({-40, 0, 0}, {40, 0, 0}), false});
    scene.curves.push_back({Ent(1), Seg(2), Line({-40, 20, 0}, {40, 20, 0}), false});
    return scene;
}

[[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.displayPickPx = 8.0;
    return tolerance;
}

[[nodiscard]] DocumentSnapshot Snapshot()
{
    DocumentSnapshot snapshot;
    Entity wire;
    wire.id = Ent(1);
    wire.kind = EntityKind::Wire;
    snapshot.entities.push_back(wire);
    Entity other;
    other.id = Ent(2);
    other.kind = EntityKind::Wire;
    snapshot.entities.push_back(other);
    Entity part;
    part.id = Ent(3);
    part.kind = EntityKind::Part;
    snapshot.entities.push_back(part);
    return snapshot;
}

} // namespace

KACHA_V2_TEST(selection, 線の上を押せば拾える)
{
    // 画面の真ん中は世界の原点。そこに1本目の線がある。
    const auto picked = PickCurve(TwoLines(), TopView(), ScreenPoint{500.0, 500.0},
        Tolerance());
    Require(picked.has_value(), "拾える");
    Require(picked->entityId == Ent(1), "1本目");
    Require(picked->kind == SelectionElementKind::Edge, "辺として拾う");
    Require(picked->segmentId == Seg(1), "線分IDを失わない");
    Require(picked->curveParameter.has_value(), "曲線上の位置を持つ");
    RequireNear(*picked->curveParameter, 0.5, 1.0e-9, "線の中央");
    RequireNear(picked->hitPoint.x, 0.0, 1.0e-9, "命中位置を持つ");
    RequireNear(picked->hitPoint.y, 0.0, 1.0e-9, "命中位置を持つ");
}

KACHA_V2_TEST(selection, 離れたところを押しても拾わない)
{
    // 8px より遠いところ。1mm = 10px なので、2mm 離せば 20px。
    const auto picked = PickCurve(TwoLines(), TopView(), ScreenPoint{500.0, 520.0},
        Tolerance());
    Require(!picked.has_value(), "拾わない");
}

KACHA_V2_TEST(selection, 近いほうを拾う)
{
    // y=20mm の線は画面では上へ 200px。その近くを押す。
    const auto picked = PickCurve(TwoLines(), TopView(), ScreenPoint{500.0, 302.0},
        Tolerance());
    Require(picked.has_value(), "拾える");
    Require(picked->entityId == Ent(2), "2本目");
}

KACHA_V2_TEST(selection, 範囲に2本入っていても近いほうを拾う)
{
    // 0.5mm(=5px)しか離れていない2本。どちらも許容差(8px)の内側にある。
    // 「範囲に入った最後の1本」ではなく、いちばん近い1本を返す。
    SnapScene scene;
    scene.curves.push_back(Curve(1, {-40, 0, 0}, {40, 0, 0}));
    scene.curves.push_back(Curve(2, {-40, 0.5, 0}, {40, 0.5, 0}));
    const auto near1 = PickCurve(scene, TopView(), ScreenPoint{500.0, 500.0}, Tolerance());
    Require(near1.has_value(), "拾える");
    Require(near1->entityId == Ent(1), "先に入っているほうでも、近ければそれを返す");
    // 逆に、あとから入っているほうが近ければ、そちらを返す。
    const auto near2 = PickCurve(scene, TopView(), ScreenPoint{500.0, 495.0}, Tolerance());
    Require(near2.has_value(), "拾える");
    Require(near2->entityId == Ent(2), "近いほう");
}

KACHA_V2_TEST(selection, 同じ距離なら先に入っているものを返す)
{
    // 毎回同じ結果になるようにする。並び順で揺れると、選び直すたびに違うものが選ばれる。
    SnapScene scene;
    scene.curves.push_back(Curve(1, {-40, 0, 0}, {40, 0, 0}));
    scene.curves.push_back(Curve(2, {-40, 0, 0}, {40, 0, 0}));
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto picked = PickCurve(scene, TopView(), ScreenPoint{500.0, 500.0},
            Tolerance());
        Require(picked.has_value(), "拾える");
        Require(picked->entityId == Ent(1), "いつも先のほう");
    }
}

KACHA_V2_TEST(selection, 拾う範囲は許容差で決まる)
{
    SnapScene scene = TwoLines();
    GeometryTolerance narrow = Tolerance();
    narrow.displayPickPx = 1.0;
    Require(!PickCurve(scene, TopView(), ScreenPoint{500.0, 505.0}, narrow).has_value(),
        "狭ければ拾わない");
    GeometryTolerance wide = Tolerance();
    wide.displayPickPx = 20.0;
    Require(PickCurve(scene, TopView(), ScreenPoint{500.0, 505.0}, wide).has_value(),
        "広ければ拾う");
}

KACHA_V2_TEST(selection, 線の端の外を押しても線の上なら拾わない)
{
    // 線は x が -40..40mm。画面では 100..900px。その外を押す。
    const auto picked = PickCurve(TwoLines(), TopView(), ScreenPoint{950.0, 500.0},
        Tolerance());
    Require(!picked.has_value(), "端の外は拾わない");
}

KACHA_V2_TEST(selection, 何も無い場面では拾えない)
{
    Require(!PickCurve(SnapScene{}, TopView(), ScreenPoint{500.0, 500.0},
                Tolerance()).has_value(),
        "拾わない");
}

KACHA_V2_TEST(selection, 素で押すと前の選択は消える)
{
    SelectionSet current;
    current.entityIds.push_back(Ent(9));
    PickCandidate picked;
    picked.entityId = Ent(1);
    const SelectionSet next = ApplySelection(current, picked, SelectionMode::Replace);
    Require(next.entityIds.size() == 1, "1つだけ");
    Require(next.entityIds.front() == Ent(1), "押したもの");
    Require(next.ordered.size() == 1, "部分選択の正本も1つ");
}

KACHA_V2_TEST(selection, 足すと順に並ぶ)
{
    // 形状ガイドの役割は選んだ順で決まる。順が変わると役割が変わってしまう。
    SelectionSet selection;
    for (std::uint8_t id : {std::uint8_t{5}, std::uint8_t{3}, std::uint8_t{7}}) {
        PickCandidate picked;
        picked.entityId = Ent(id);
        selection = ApplySelection(selection, picked, SelectionMode::Add);
    }
    Require(selection.entityIds.size() == 3, "3つ");
    Require(selection.entityIds[0] == Ent(5), "押した順");
    Require(selection.entityIds[1] == Ent(3), "押した順");
    Require(selection.entityIds[2] == Ent(7), "押した順");
}

KACHA_V2_TEST(selection, 同じものを足しても増えない)
{
    SelectionSet selection;
    PickCandidate picked;
    picked.entityId = Ent(4);
    selection = ApplySelection(selection, picked, SelectionMode::Add);
    selection = ApplySelection(selection, picked, SelectionMode::Add);
    Require(selection.entityIds.size() == 1, "1つのまま");
}

KACHA_V2_TEST(selection, 切り替えは入っていれば外す)
{
    SelectionSet selection;
    PickCandidate picked;
    picked.entityId = Ent(4);
    selection = ApplySelection(selection, picked, SelectionMode::Toggle);
    Require(IsSelected(selection, Ent(4)), "入る");
    selection = ApplySelection(selection, picked, SelectionMode::Toggle);
    Require(!IsSelected(selection, Ent(4)), "外れる");
}

KACHA_V2_TEST(selection, 同じワイヤーの複数辺を別々に選べる)
{
    const SnapScene scene = TwoSegmentsOneWire();
    const auto first = PickCurve(scene, TopView(), ScreenPoint{500.0, 500.0}, Tolerance());
    const auto second = PickCurve(scene, TopView(), ScreenPoint{500.0, 300.0}, Tolerance());
    Require(first.has_value() && second.has_value(), "2辺を拾える");

    SelectionSet selection;
    selection = ApplySelection(selection, first, SelectionMode::Toggle);
    selection = ApplySelection(selection, second, SelectionMode::Toggle);
    Require(SelectionItemCount(selection) == 2, "部分要素は2件");
    Require(selection.entityIds.size() == 1, "互換投影の物体IDは重複しない");
    Require(selection.ordered[0].segmentId == Seg(1), "最初の辺");
    Require(selection.ordered[1].segmentId == Seg(2), "次の辺");
    Require(SelectedCurves(selection, scene).size() == 2, "選んだ2辺だけを渡す");

    selection = ApplySelection(selection, first, SelectionMode::Toggle);
    Require(SelectionItemCount(selection) == 1, "同じ辺だけを外す");
    Require(selection.ordered.front().segmentId == Seg(2), "もう一方は残る");
    Require(selection.entityIds.size() == 1, "物体はまだ選択中");
}

KACHA_V2_TEST(selection, 同じ部品の複数面を別々に選べる)
{
    PickCandidate startFace;
    startFace.entityId = Ent(3);
    startFace.kind = SelectionElementKind::Face;
    startFace.subshapeKey = kachakacha::v2::modeling::MakeExtrudeCapStart();
    PickCandidate endFace = startFace;
    endFace.subshapeKey = kachakacha::v2::modeling::MakeExtrudeCapEnd();

    SelectionSet selection;
    selection = ApplySelection(selection, startFace, SelectionMode::Toggle);
    selection = ApplySelection(selection, endFace, SelectionMode::Toggle);
    Require(SelectionItemCount(selection) == 2, "同じ部品でも2面");
    Require(selection.entityIds.size() == 1, "互換投影は1部品");
    Require(selection.ordered[0].subshapeKey->ToString()
            != selection.ordered[1].subshapeKey->ToString(),
        "意味的な面キーを区別する");

    SelectionRef target = selection.ordered.front();
    Require(IsSelected(selection, target), "面単位で選択を照合できる");
    selection = ApplySelection(selection, startFace, SelectionMode::Toggle);
    Require(SelectionItemCount(selection) == 1, "指定した面だけ外す");
}

KACHA_V2_TEST(selection, 外すのは入っていなくても落ちない)
{
    SelectionSet selection;
    PickCandidate picked;
    picked.entityId = Ent(4);
    const SelectionSet next = ApplySelection(selection, picked, SelectionMode::Subtract);
    Require(next.entityIds.empty(), "空のまま");
}

KACHA_V2_TEST(selection, 何も無いところを素で押すと空になる)
{
    SelectionSet selection;
    selection.entityIds.push_back(Ent(1));
    const SelectionSet next = ApplySelection(selection, std::nullopt,
        SelectionMode::Replace);
    Require(next.entityIds.empty(), "空になる");
}

KACHA_V2_TEST(selection, 複数選択中に外を押しても選択は消えない)
{
    // ここで消すと、選び直しになる。
    SelectionSet selection;
    selection.entityIds.push_back(Ent(1));
    for (SelectionMode mode : {SelectionMode::Add, SelectionMode::Toggle,
             SelectionMode::Subtract}) {
        const SelectionSet next = ApplySelection(selection, std::nullopt, mode);
        Require(next.entityIds.size() == 1, "残る");
    }
}

KACHA_V2_TEST(selection, 種類ごとに数えられる)
{
    SelectionSet selection;
    selection.entityIds.push_back(Ent(1));
    selection.entityIds.push_back(Ent(2));
    selection.entityIds.push_back(Ent(3));
    RequireEqual(std::string("2"),
        std::to_string(SelectedCountOfKind(selection, Snapshot(), EntityKind::Wire)),
        "ワイヤーの数");
    RequireEqual(std::string("1"),
        std::to_string(SelectedCountOfKind(selection, Snapshot(), EntityKind::Part)),
        "部品の数");
}

KACHA_V2_TEST(selection, 文書に無いものは数えない)
{
    SelectionSet selection;
    selection.entityIds.push_back(Ent(99));
    RequireEqual(std::string("0"),
        std::to_string(SelectedCountOfKind(selection, Snapshot(), EntityKind::Wire)),
        "数えない");
}

KACHA_V2_TEST(selection, 種類ごとに全部選べる)
{
    const SelectionSet selection = SelectAllOfKind(Snapshot(), EntityKind::Wire);
    Require(selection.entityIds.size() == 2, "2つ");
    Require(selection.entityIds[0] == Ent(1), "文書の並び");
}

KACHA_V2_TEST(selection, 消えたものは選択から外れる)
{
    SelectionSet selection;
    selection.entityIds.push_back(Ent(1));
    selection.entityIds.push_back(Ent(99));
    selection.entityIds.push_back(Ent(3));
    const SelectionSet next = PruneSelection(selection, Snapshot());
    Require(next.entityIds.size() == 2, "2つ残る");
    Require(next.entityIds[0] == Ent(1), "順は変わらない");
    Require(next.entityIds[1] == Ent(3), "順は変わらない");
}

KACHA_V2_TEST(selection, 選んだ線を選んだ順で取り出せる)
{
    SelectionSet selection;
    selection.entityIds.push_back(Ent(2));
    selection.entityIds.push_back(Ent(1));
    const auto curves = SelectedCurves(selection, TwoLines());
    Require(curves.size() == 2, "2本");
    // 2番目の線は y=20 にある。先に選んだので先に出る。
    Require(curves.front().Evaluate(0.0).y == 20.0, "選んだ順");
}

KACHA_V2_TEST(selection, 選んでいない線は取り出されない)
{
    SelectionSet selection;
    selection.entityIds.push_back(Ent(1));
    Require(SelectedCurves(selection, TwoLines()).size() == 1, "1本だけ");
}

KACHA_V2_TEST_MAIN("selection_tests")
