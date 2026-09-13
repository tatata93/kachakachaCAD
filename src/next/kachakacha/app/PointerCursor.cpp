#include "kachakacha/app/PointerCursor.h"

namespace kachakacha::v2::app {

std::string_view CursorShapeNameJa(CursorShape shape) noexcept
{
    switch (shape) {
    case CursorShape::Arrow:
        return "矢印";
    case CursorShape::OpenHand:
        return "開いた手";
    case CursorShape::ClosedHand:
        return "閉じた手";
    case CursorShape::Rotate:
        return "回転";
    case CursorShape::Cross:
        return "十字";
    case CursorShape::Move:
        return "移動";
    case CursorShape::Forbidden:
        return "禁止";
    }
    return "矢印";
}

CursorShape ChooseCursorShape(const PointerCursorContext& context) noexcept
{
    // 1. いま手で動かしているものが最優先。動かしている最中に形が変わると、
    //    何を掴んでいるのか分からなくなる。
    if (context.orbiting) {
        return CursorShape::Rotate;
    }
    if (context.panning || context.draggingViewGadget) {
        return CursorShape::ClosedHand;
    }
    if (context.draggingBody || context.draggingControlPoint) {
        return CursorShape::Move;
    }
    // 2. 押せる操作板の上。ここは図形ではないので手で示す。
    if (context.overViewGadget) {
        return CursorShape::OpenHand;
    }
    // 3. 置けない・拾えない場所。道具の形より先に伝える。
    if (context.overForbidden) {
        return CursorShape::Forbidden;
    }
    // 4. 作図中は十字。矢印との違いで「いま描ける」と分かる。
    if (context.placingPoints) {
        return CursorShape::Cross;
    }
    // 5. それ以外は矢印。形の上でも指にしない(§5.1)。
    return CursorShape::Arrow;
}

} // namespace kachakacha::v2::app
