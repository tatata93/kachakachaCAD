// クリックと引きずりの分け方(UI-P1-008)と、カーソルの形の決め方(§5.1)。
#include "kachakacha/app/PointerCursor.h"
#include "kachakacha/app/PointerGesture.h"
#include "kachakacha/base/TestHarness.h"

#include <set>
#include <string>

using kachakacha::v2::app::ChooseCursorShape;
using kachakacha::v2::app::CursorShape;
using kachakacha::v2::app::CursorShapeNameJa;
using kachakacha::v2::app::ExceedsDragThreshold;
using kachakacha::v2::app::kClickDragThresholdPx;
using kachakacha::v2::app::PointerCursorContext;
using kachakacha::v2::app::PointerGesture;
using kachakacha::v2::app::PointerGestureTracker;
using kachakacha::v2::geometry::ScreenPoint;
using kachakacha::v2::test::Require;

namespace {

constexpr CursorShape kAllShapes[] = {
    CursorShape::Arrow, CursorShape::OpenHand, CursorShape::ClosedHand,
    CursorShape::Rotate, CursorShape::Cross, CursorShape::Move, CursorShape::Forbidden,
};

} // namespace

KACHA_V2_TEST(pointer_gesture, 門は5pxで向きに寄らない)
{
    Require(kClickDragThresholdPx == 5.0, "5px");
    Require(!ExceedsDragThreshold(0.0, 0.0), "動いていない");
    Require(!ExceedsDragThreshold(4.9, 0.0), "5px 未満はクリック");
    Require(ExceedsDragThreshold(5.0, 0.0), "5px ちょうどはドラッグ");
    Require(ExceedsDragThreshold(0.0, -5.0), "上へでも同じ");
    Require(ExceedsDragThreshold(-3.6, 3.6), "斜めは長さで見る");
    Require(!ExceedsDragThreshold(3.0, 3.0), "斜め 4.24px はまだクリック");
}

KACHA_V2_TEST(pointer_gesture, 手の震えではドラッグにしない)
{
    // 選ぼうとして押しただけで物が動いた、というのがこれを作った理由である。
    PointerGestureTracker tracker;
    tracker.Begin(ScreenPoint{200.0, 200.0});
    for (const double jitter : {0.4, -0.7, 1.3, -1.9, 2.4, -2.0, 1.1}) {
        Require(!tracker.Update(ScreenPoint{200.0 + jitter, 200.0 - jitter}),
            "ドラッグへ入らない");
        Require(!tracker.IsDrag(), "まだクリック");
    }
    Require(tracker.Release(ScreenPoint{201.0, 199.0}) == PointerGesture::Click,
        "離してもクリック");
}

KACHA_V2_TEST(pointer_gesture, 門を越えた瞬間だけ知らせる)
{
    PointerGestureTracker tracker;
    tracker.Begin(ScreenPoint{0.0, 0.0});
    Require(!tracker.Update(ScreenPoint{2.0, 2.0}), "まだ");
    Require(tracker.Update(ScreenPoint{6.0, 0.0}), "ここで入った");
    Require(!tracker.Update(ScreenPoint{40.0, 40.0}), "入り直しではない");
    Require(tracker.IsDrag(), "ドラッグのまま");
}

KACHA_V2_TEST(pointer_gesture, 一度ドラッグになったら戻らない)
{
    // 矩形選択が「離した場所」で決めていたころ、引きずってから
    // 押した場所へ戻して離すと、引きずらなかったことになっていた。
    PointerGestureTracker tracker;
    tracker.Begin(ScreenPoint{300.0, 300.0});
    Require(tracker.Update(ScreenPoint{420.0, 300.0}), "大きく引いた");
    Require(!tracker.Update(ScreenPoint{300.0, 300.0}), "戻っても入り直さない");
    Require(tracker.IsDrag(), "ドラッグのまま");
    Require(tracker.Release(ScreenPoint{300.0, 300.0}) == PointerGesture::Drag,
        "元の場所で離してもドラッグ");
}

KACHA_V2_TEST(pointer_gesture, 押していないときは動かしても何も起きない)
{
    PointerGestureTracker tracker;
    Require(!tracker.Active(), "押していない");
    Require(!tracker.Update(ScreenPoint{999.0, 999.0}), "門に入らない");
    Require(tracker.Kind() == PointerGesture::Click, "既定はクリック");
}

KACHA_V2_TEST(pointer_gesture, やめたら押していないことになる)
{
    PointerGestureTracker tracker;
    tracker.Begin(ScreenPoint{10.0, 10.0});
    (void)tracker.Update(ScreenPoint{100.0, 100.0});
    Require(tracker.IsDrag(), "ドラッグ中");
    tracker.Reset();
    Require(!tracker.Active(), "押していない");
    Require(!tracker.IsDrag(), "ドラッグでもない");
}

KACHA_V2_TEST(pointer_gesture, 押した場所を覚えている)
{
    PointerGestureTracker tracker;
    tracker.Begin(ScreenPoint{12.5, -3.25});
    Require(tracker.Origin().x == 12.5 && tracker.Origin().y == -3.25, "押した場所");
    Require(tracker.Active(), "押している");
}

KACHA_V2_TEST(pointer_cursor, 形の上では指にしない)
{
    // §5.1「クリック可能な通常形状に指カーソルを使わない」。
    // 指はここでは一切出さない。出せる形の一覧にも入れていない。
    Require(ChooseCursorShape(PointerCursorContext{}) == CursorShape::Arrow, "既定は矢印");
    PointerCursorContext hovering;
    Require(ChooseCursorShape(hovering) == CursorShape::Arrow, "形の上でも矢印");
}

KACHA_V2_TEST(pointer_cursor, カメラ操作で形が変わる)
{
    PointerCursorContext panning;
    panning.panning = true;
    Require(ChooseCursorShape(panning) == CursorShape::ClosedHand, "パンは閉じた手");

    PointerCursorContext orbiting;
    orbiting.orbiting = true;
    Require(ChooseCursorShape(orbiting) == CursorShape::Rotate, "回転は回転");

    PointerCursorContext gadget;
    gadget.overViewGadget = true;
    Require(ChooseCursorShape(gadget) == CursorShape::OpenHand, "操作板は開いた手");

    PointerCursorContext held;
    held.overViewGadget = true;
    held.draggingViewGadget = true;
    Require(ChooseCursorShape(held) == CursorShape::ClosedHand, "掴んだら閉じる");
}

KACHA_V2_TEST(pointer_cursor, 作図中は十字で掴んでいる間は移動)
{
    PointerCursorContext drawing;
    drawing.placingPoints = true;
    Require(ChooseCursorShape(drawing) == CursorShape::Cross, "十字");

    PointerCursorContext moving;
    moving.draggingBody = true;
    Require(ChooseCursorShape(moving) == CursorShape::Move, "移動");

    PointerCursorContext control;
    control.draggingControlPoint = true;
    Require(ChooseCursorShape(control) == CursorShape::Move, "制御点も移動");
}

KACHA_V2_TEST(pointer_cursor, 拾えない場所は禁止)
{
    PointerCursorContext forbidden;
    forbidden.overForbidden = true;
    Require(ChooseCursorShape(forbidden) == CursorShape::Forbidden, "禁止");

    // 作図中でも、置けない場所なら禁止が勝つ。
    forbidden.placingPoints = true;
    Require(ChooseCursorShape(forbidden) == CursorShape::Forbidden, "道具より強い");
}

KACHA_V2_TEST(pointer_cursor, 動かしている最中は形が戻らない)
{
    // 掴んで動かしている途中で別の線の上を通っても、移動のままにする。
    PointerCursorContext dragging;
    dragging.draggingBody = true;
    dragging.overViewGadget = true;
    dragging.overForbidden = true;
    dragging.placingPoints = true;
    Require(ChooseCursorShape(dragging) == CursorShape::Move, "移動のまま");

    PointerCursorContext camera;
    camera.orbiting = true;
    camera.draggingBody = true;
    Require(ChooseCursorShape(camera) == CursorShape::Rotate, "カメラが一番強い");
}

KACHA_V2_TEST(pointer_cursor, すべての形に名前があり重ならない)
{
    std::set<std::string> names;
    for (const CursorShape shape : kAllShapes) {
        const std::string name(CursorShapeNameJa(shape));
        Require(!name.empty(), "名前が空でない");
        names.insert(name);
    }
    Require(names.size() == std::size(kAllShapes), "全部ちがう名前");
}

KACHA_V2_TEST_MAIN("pointer_gesture_tests")
