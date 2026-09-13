// 意味状態(ui-ux-integrated-spec §3)。
//
// ここで確かめるのは「どの線がどの状態か」だけである。色は持たない。
// 色はテーマの持ち物で、Qt の要る画面側の試験(cad_next --self-test)で見る。
#include "kachakacha/app/ControlPointPick.h"
#include "kachakacha/app/SemanticState.h"
#include "kachakacha/base/TestHarness.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

using kachakacha::v2::app::ApplySelection;
using kachakacha::v2::app::ControlPointsForSelection;
using kachakacha::v2::app::CurveSemanticState;
using kachakacha::v2::app::IsCurveSelected;
using kachakacha::v2::app::PickCandidate;
using kachakacha::v2::app::PointSemanticState;
using kachakacha::v2::app::SelectionElementKind;
using kachakacha::v2::app::SelectionMode;
using kachakacha::v2::app::SelectionRef;
using kachakacha::v2::app::SelectionSet;
using kachakacha::v2::app::SemanticState;
using kachakacha::v2::app::SemanticStateNameJa;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::base::SegmentId;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

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

//! 線分1本を選んだ選択集合。クリックで選んだときと同じ形にする。
[[nodiscard]] SelectionSet SelectedSegment(EntityId entityId, SegmentId segmentId)
{
    PickCandidate candidate;
    candidate.entityId = entityId;
    candidate.segmentId = segmentId;
    candidate.kind = SelectionElementKind::Edge;
    return ApplySelection(SelectionSet{}, candidate, SelectionMode::Replace);
}

//! 物体まるごとを選んだ選択集合(一覧から選んだときと同じ形)。
[[nodiscard]] SelectionSet SelectedWholeObject(EntityId entityId)
{
    SelectionSet selection;
    selection.entityIds.push_back(entityId);
    return selection;
}

[[nodiscard]] std::string Name(SemanticState state)
{
    return SemanticStateNameJa(state);
}

//! 直線2本でできた折れ線の場面。線分は Seg(1) と Seg(2)。
[[nodiscard]] kachakacha::v2::modeling::SnapScene TwoSegmentWireScene(EntityId wire)
{
    using kachakacha::v2::geometry::CurveSegment;
    using kachakacha::v2::geometry::Vector3;
    kachakacha::v2::modeling::SnapScene scene;
    scene.curves.push_back({wire, Seg(1),
        CurveSegment::MakeLine(Vector3{0, 0, 0}, Vector3{10, 0, 0}).Value()});
    scene.curves.push_back({wire, Seg(2),
        CurveSegment::MakeLine(Vector3{10, 0, 0}, Vector3{10, 10, 0}).Value()});
    return scene;
}

[[nodiscard]] std::size_t HandlesOn(const kachakacha::v2::modeling::SnapScene& scene,
    const SelectionSet& selection, SegmentId segmentId)
{
    std::size_t count = 0;
    for (const auto& handle : ControlPointsForSelection(scene, selection)) {
        if (handle.segmentId == segmentId) {
            ++count;
        }
    }
    return count;
}

} // namespace

KACHA_V2_TEST(semantic_state, 線分を選ぶと同じワイヤーの他の線分は通常のまま)
{
    // 折れ線の1本だけを選んだのに全部が選択色になると、
    // 次の操作の相手がどれなのか画面から読めない(§4.1)。
    const auto wire = Ent(1);
    const auto selection = SelectedSegment(wire, Seg(1));
    RequireEqual(Name(CurveSemanticState(selection, wire, Seg(1), EntityId{}, SegmentId{})),
        "選択", "選んだ線分は選択");
    RequireEqual(Name(CurveSemanticState(selection, wire, Seg(2), EntityId{}, SegmentId{})),
        "通常", "同じワイヤーの別の線分は通常");
    Require(IsCurveSelected(selection, wire, Seg(1)), "選んだ線分は選択に入っている");
    Require(!IsCurveSelected(selection, wire, Seg(2)), "別の線分は選択に入っていない");
}

KACHA_V2_TEST(semantic_state, 物体ごと選べば全線分が選択になる)
{
    const auto wire = Ent(1);
    const auto selection = SelectedWholeObject(wire);
    Require(IsCurveSelected(selection, wire, Seg(1)), "1本目が選択");
    Require(IsCurveSelected(selection, wire, Seg(2)), "2本目も選択");
    Require(!IsCurveSelected(selection, Ent(2), Seg(1)), "別の物体は選択でない");
}

KACHA_V2_TEST(semantic_state, 線分を選ぶと制御点もその線分にだけ出る)
{
    // 制御点の四角は選択色で描き、掴めば動かせる。線の色と別の規則で出すと、
    // 選んでいない線分に選択色の四角が出て、そこまで掴めてしまう(§3)。
    const auto wire = Ent(1);
    const auto scene = TwoSegmentWireScene(wire);
    const auto selection = SelectedSegment(wire, Seg(1));
    Require(HandlesOn(scene, selection, Seg(1)) == 2,
        "選んだ線分には始点と終点の制御点が出る");
    Require(HandlesOn(scene, selection, Seg(2)) == 0,
        "同じワイヤーの別の線分には制御点を出さない");
}

KACHA_V2_TEST(semantic_state, 物体ごと選べば全線分に制御点が出る)
{
    const auto wire = Ent(1);
    const auto scene = TwoSegmentWireScene(wire);
    const auto selection = SelectedWholeObject(wire);
    Require(HandlesOn(scene, selection, Seg(1)) == 2, "1本目に出る");
    Require(HandlesOn(scene, selection, Seg(2)) == 2, "2本目にも出る");
    Require(ControlPointsForSelection(scene, SelectedWholeObject(Ent(2))).empty(),
        "別の物体を選んでもこのワイヤーには出ない");
}

KACHA_V2_TEST(semantic_state, 点や面を選んでも線は選択にならない)
{
    // 端点を選んだだけで線全体が選択色になると、
    // 「端点を選んだ」のか「線を選んだ」のかが見分けられない。
    const auto wire = Ent(1);
    SelectionSet selection;
    SelectionRef vertex;
    vertex.entityId = wire;
    vertex.kind = SelectionElementKind::Vertex;
    selection.ordered.push_back(vertex);
    Require(!IsCurveSelected(selection, wire, Seg(1)), "端点の選択は線を選択にしない");
    RequireEqual(Name(CurveSemanticState(selection, wire, Seg(1), EntityId{}, SegmentId{})),
        "通常", "端点の選択では線は通常");
}

KACHA_V2_TEST(semantic_state, カーソルの下の線分だけがHoverになる)
{
    const auto wire = Ent(1);
    RequireEqual(Name(CurveSemanticState(SelectionSet{}, wire, Seg(1), wire, Seg(1))),
        "カーソルの下", "指している線分はHover");
    RequireEqual(Name(CurveSemanticState(SelectionSet{}, wire, Seg(2), wire, Seg(1))),
        "通常", "同じワイヤーの別の線分は通常");
    RequireEqual(Name(CurveSemanticState(SelectionSet{}, Ent(2), Seg(1), wire, Seg(1))),
        "通常", "別の物体は通常");
}

KACHA_V2_TEST(semantic_state, 線分が分からないHoverは物体全体を指す)
{
    // 塗った形のように線分を持たない候補を指しているときは、
    // その物体の線をまとめて Hover にする。どこにも印が出ないと、
    // 何に当たっているのか分からない。
    const auto wire = Ent(1);
    RequireEqual(Name(CurveSemanticState(SelectionSet{}, wire, Seg(1), wire, SegmentId{})),
        "カーソルの下", "1本目がHover");
    RequireEqual(Name(CurveSemanticState(SelectionSet{}, wire, Seg(2), wire, SegmentId{})),
        "カーソルの下", "2本目もHover");
}

KACHA_V2_TEST(semantic_state, 選択はHoverより強い)
{
    // Hover だけで選択を変えない(§3 規則1)以上、
    // 選んだ線へカーソルを載せて色が Hover へ落ちてはならない。
    // 落ちると「載せたら選択が外れた」ように見える。
    const auto wire = Ent(1);
    const auto selection = SelectedSegment(wire, Seg(1));
    RequireEqual(Name(CurveSemanticState(selection, wire, Seg(1), wire, Seg(1))),
        "選択", "選んだ線分はカーソルを載せても選択");
    RequireEqual(Name(CurveSemanticState(selection, wire, Seg(2), wire, Seg(2))),
        "カーソルの下", "選んでいない線分はHover");
}

KACHA_V2_TEST(semantic_state, 作図点も選択とHoverを描き分ける)
{
    const auto point = Ent(3);
    RequireEqual(Name(PointSemanticState(SelectionSet{}, point, EntityId{})), "通常",
        "何もしていなければ通常");
    RequireEqual(Name(PointSemanticState(SelectionSet{}, point, point)), "カーソルの下",
        "指していればHover");
    RequireEqual(Name(PointSemanticState(SelectedWholeObject(point), point, point)), "選択",
        "選んでいれば選択");
    RequireEqual(Name(PointSemanticState(SelectedWholeObject(point), Ent(4), EntityId{})),
        "通常", "別の点は通常");
}

KACHA_V2_TEST(semantic_state, 状態の名前がすべて違う)
{
    // 帯や診断へ出す名前が重なっていると、どの状態の話か読めない。
    const SemanticState all[] = {SemanticState::Default, SemanticState::Hover,
        SemanticState::Selected, SemanticState::Snap, SemanticState::Preview};
    for (const SemanticState first : all) {
        for (const SemanticState second : all) {
            if (first == second) {
                continue;
            }
            Require(Name(first) != Name(second),
                "状態の名前が重なっていない: " + Name(first));
        }
    }
}

KACHA_V2_TEST_MAIN("semantic_state_tests")
