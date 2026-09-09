// 選んだ物を掴んで動かす(v1-input-parity.md §1-2 の 11)。
//
// 「掴んだかどうか」の判断はここで押さえる。画面が無くても確かめられる形にした。
#include "kachakacha/app/GrabToMove.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/geometry/CurveSegment.h"

#include <cmath>

using kachakacha::v2::app::DragDelta;
using kachakacha::v2::app::DragIsFarEnough;
using kachakacha::v2::app::PointerGrabsSelection;
using kachakacha::v2::app::SelectionSet;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::MakeOrthographicMapping;
using kachakacha::v2::geometry::ScreenPoint;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::SnapScene;
using kachakacha::v2::test::Require;

namespace {

//! 真上から見る。1000px で 100mm。1mm = 10px。
[[nodiscard]] kachakacha::v2::geometry::ScreenMapping TopView()
{
    return MakeOrthographicMapping(Vector3{0.0, 0.0, 0.0}, Vector3{0.0, 0.0, -1.0},
        Vector3{0.0, 1.0, 0.0}, 100.0, 1000.0, 1000.0);
}

//! X軸に沿った線を1本だけ置いた場面。
[[nodiscard]] SnapScene SceneWithOneLine(kachakacha::v2::base::EntityId entityId,
    kachakacha::v2::base::SegmentId segmentId)
{
    SnapScene scene;
    const auto made = kachakacha::v2::geometry::CurveSegment::MakeLine(
        Vector3{-20.0, 0.0, 0.0}, Vector3{20.0, 0.0, 0.0});
    scene.curves.push_back({entityId, segmentId, made.Value(), false});
    return scene;
}

} // namespace

KACHA_V2_TEST(grab_to_move, 選んでいる線の上なら掴める)
{
    kachakacha::v2::base::DeterministicIdGenerator ids;
    const auto entityId = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    const auto segmentId = ids.NextTyped<kachakacha::v2::base::IdKind::Segment>();
    const SnapScene scene = SceneWithOneLine(entityId, segmentId);
    SelectionSet selection;
    selection.entityIds.push_back(entityId);
    // 線の真上(画面の中心)を押す。
    Require(PointerGrabsSelection(scene, selection, TopView(), ScreenPoint{500.0, 500.0},
                GeometryTolerance::Default()),
        "選んでいる線の上は掴める");
}

KACHA_V2_TEST(grab_to_move, 選んでいない線の上は掴めない)
{
    // 掴みにしてしまうと、隣の線を選び直せなくなる。
    kachakacha::v2::base::DeterministicIdGenerator ids;
    const auto entityId = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    const auto other = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    const auto segmentId = ids.NextTyped<kachakacha::v2::base::IdKind::Segment>();
    const SnapScene scene = SceneWithOneLine(entityId, segmentId);
    SelectionSet selection;
    selection.entityIds.push_back(other);
    Require(!PointerGrabsSelection(scene, selection, TopView(), ScreenPoint{500.0, 500.0},
                GeometryTolerance::Default()),
        "選んでいない線の上は掴まない");
}

KACHA_V2_TEST(grab_to_move, 何も選んでいなければ掴めない)
{
    kachakacha::v2::base::DeterministicIdGenerator ids;
    const auto entityId = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    const auto segmentId = ids.NextTyped<kachakacha::v2::base::IdKind::Segment>();
    const SnapScene scene = SceneWithOneLine(entityId, segmentId);
    Require(!PointerGrabsSelection(scene, SelectionSet{}, TopView(),
                ScreenPoint{500.0, 500.0}, GeometryTolerance::Default()),
        "掴むものが無い");
}

KACHA_V2_TEST(grab_to_move, 線から離れたところは掴めない)
{
    kachakacha::v2::base::DeterministicIdGenerator ids;
    const auto entityId = ids.NextTyped<kachakacha::v2::base::IdKind::Entity>();
    const auto segmentId = ids.NextTyped<kachakacha::v2::base::IdKind::Segment>();
    const SnapScene scene = SceneWithOneLine(entityId, segmentId);
    SelectionSet selection;
    selection.entityIds.push_back(entityId);
    // 線から 200px 離す。当たり判定は displayPickPx(8px)なので届かない。
    Require(!PointerGrabsSelection(scene, selection, TopView(), ScreenPoint{500.0, 300.0},
                GeometryTolerance::Default()),
        "遠いところは掴まない");
}

KACHA_V2_TEST(grab_to_move, 押しただけは動かしたことにしない)
{
    // ここを 0px にすると、選び直すたびに 0mm の移動が文書へ入ってしまう。
    Require(!DragIsFarEnough(0.0, 0.0), "動かしていない");
    Require(!DragIsFarEnough(2.0, 2.0), "手の震えぶんは動かしていない");
    Require(DragIsFarEnough(4.0, 0.0), "4px は動かした");
    Require(DragIsFarEnough(0.0, -10.0), "向きは関係ない");
}

KACHA_V2_TEST(grab_to_move, 引きずった量は掴んだ場所からの差)
{
    const Vector3 delta = DragDelta(Vector3{10.0, 5.0, 0.0}, Vector3{30.0, 5.0, 0.0});
    Require(std::abs(delta.x - 20.0) < 1e-9, "20mm");
    Require(std::abs(delta.y) < 1e-9, "横へは動かない");
}

KACHA_V2_TEST_MAIN("grab_to_move_tests")
