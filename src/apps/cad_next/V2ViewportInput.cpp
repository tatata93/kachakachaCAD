//! マウスとキーの土台(V1同等、docs/v2/v1-input-parity.md)。
//!
//! ここに集めたのは、V1にあって V2 に無かったものである。
//!   - 中ボタン(と右ボタン)のドラッグで画面を移動
//!   - Shift+中ボタンで軌道回転
//!   - Esc は、やりかけを1つ取り消してから選択道具へ戻り、選択も解除する
//!   - 作図中の Ctrl で吸着を一時停止、Shift で水平・垂直・正方形へ固定
//!   - 掴めるかどうかが分かるカーソル
//!
//! どれも「画面が無いと確かめられない」ものではない。
//! 判断は core(app/EscapeAction、modeling/DrawingConstraint)にある。
//! ここはそれを Qt へつなぐだけである。

#include "V2Viewport.h"

#include "kachakacha/modeling/DrawingConstraint.h"

#include <QCursor>

#include <cmath>
#include <utility>

namespace {

//! 1px あたり何度回すか。ビューキューブと同じにする。
//! 別の値にすると、同じ手つきなのに回り方が変わる。
constexpr double kOrbitDegreesPerPixel = 0.5;

} // namespace

void V2Viewport::PanByPixels(double dxPx, double dyPx)
{
    if (!(visibleWidthMm_ > 0.0) || width() <= 0) {
        return;
    }
    // 画面のpxをmmへ直す。倍率が変わっても、掴んだ点が指から離れない。
    const double millimetersPerPixel = visibleWidthMm_ / static_cast<double>(width());
    const auto right = kachakacha::v2::view::RightOf(orientation_);
    const auto up = kachakacha::v2::view::UpOf(orientation_);
    center_ = center_ - right * (dxPx * millimetersPerPixel)
        + up * (dyPx * millimetersPerPixel);
    RebuildMapping();
    update();
}

void V2Viewport::OrbitByPixels(double dxPx, double dyPx)
{
    using kachakacha::v2::view::RotateByScreenAxis;
    using kachakacha::v2::view::ViewGadgetDirection;
    // 横は画面の縦軸まわり、縦は画面の横軸まわり。操作板の矢印と同じ決め方。
    const auto turnedSide = RotateByScreenAxis(orientation_,
        dxPx >= 0.0 ? ViewGadgetDirection::Right : ViewGadgetDirection::Left,
        std::abs(dxPx) * kOrbitDegreesPerPixel);
    if (!turnedSide.HasValue()) {
        return;
    }
    const auto turned = RotateByScreenAxis(turnedSide.Value(),
        dyPx >= 0.0 ? ViewGadgetDirection::Down : ViewGadgetDirection::Up,
        std::abs(dyPx) * kOrbitDegreesPerPixel);
    if (!turned.HasValue()) {
        return;
    }
    orientation_ = turned.Value();
    RebuildMapping();
    update();
}

void V2Viewport::SetBackToSelectCallback(std::function<void()> callback)
{
    backToSelect_ = std::move(callback);
}

void V2Viewport::SetSnapSuppressedByKey(bool suppressed)
{
    if (snapSuppressedByKey_ == suppressed) {
        return;
    }
    snapSuppressedByKey_ = suppressed;
    ApplySnapSettings();
}

void V2Viewport::SetAxisConstraintByKey(bool constrained)
{
    if (axisConstrainedByKey_ == constrained) {
        return;
    }
    axisConstrainedByKey_ = constrained;
    // 拘束は吸着のあとに当てる。当て方は core が決める。
    if (constrained) {
        session_->SetPointAdjuster([this](const kachakacha::v2::geometry::Vector3& point) {
            return ConstrainedPoint(point);
        });
    } else {
        session_->SetPointAdjuster({});
    }
    update();
}

kachakacha::v2::geometry::Vector3 V2Viewport::ConstrainedPoint(
    const kachakacha::v2::geometry::Vector3& point) const
{
    if (!axisConstrainedByKey_ || session_->PlacedPointCount() == 0) {
        return point;
    }
    // 基準は、ポリラインとスプラインなら直前の点、それ以外は1点目。V1と同じ。
    return kachakacha::v2::modeling::ApplyAxisConstraint(session_->CurrentTool(),
        workPlane_, session_->ConstraintAnchor(), point);
}

std::vector<kachakacha::v2::app::EscapeStep> V2Viewport::PressEscape()
{
    kachakacha::v2::app::EscapeContext context;
    context.draggingGadget = gadgetDrag_.has_value();
    context.draggingCube = cubeDrag_.active;
    context.waitingForPick = PickPending();
    context.cursorInputOpen = cursorPanel_.active;
    context.toolHasPoints = session_->HasPlacedPoints();
    context.hasSelection = !selection_.entityIds.empty();
    context.toolIsSelect =
        session_->CurrentTool() == kachakacha::v2::modeling::DrawingTool::Select;
    const auto steps = kachakacha::v2::app::PlanEscape(context);
    for (const kachakacha::v2::app::EscapeStep step : steps) {
        switch (step) {
        case kachakacha::v2::app::EscapeStep::CancelGadgetDrag:
            gadgetDrag_.reset();
            break;
        case kachakacha::v2::app::EscapeStep::CancelCubeDrag:
            cubeDrag_ = kachakacha::v2::view::ViewCubeDrag{};
            cubeMoved_ = false;
            break;
        case kachakacha::v2::app::EscapeStep::CancelPick:
            CancelPointPick();
            break;
        case kachakacha::v2::app::EscapeStep::CloseCursorInput:
            CloseCursorInput();
            break;
        case kachakacha::v2::app::EscapeStep::CancelDrawing:
            session_->CancelTool();
            hover_.preview.clear();
            break;
        case kachakacha::v2::app::EscapeStep::ClearSelection:
            SetSelection(kachakacha::v2::app::SelectionSet{});
            break;
        case kachakacha::v2::app::EscapeStep::BackToSelectTool:
            if (backToSelect_) {
                backToSelect_();
            }
            break;
        }
    }
    const std::string_view message = kachakacha::v2::app::EscapeMessageJa(steps);
    if (!message.empty()) {
        status_ = std::string(message);
        if (statusCallback_) {
            statusCallback_(status_);
        }
    }
    RefreshCursorShape();
    update();
    return steps;
}

void V2Viewport::ApplySnapSettings()
{
    kachakacha::v2::modeling::SnapSettings settings;
    // 道具として切ってあるか、Ctrl を押している間は吸着しない。
    settings.suppressed = snapSuppressedBySetting_ || snapSuppressedByKey_;
    session_->SetSnapSettings(settings);
}

void V2Viewport::SetSnapSuppressed(bool suppressed)
{
    snapSuppressedBySetting_ = suppressed;
    ApplySnapSettings();
}

void V2Viewport::RefreshCursorShape()
{
    // 掴めるかどうかが手元で分かるようにする。V1と同じ使い分け。
    if (panning_) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (orbiting_ || gadgetDrag_.has_value() || cubeDrag_.active) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (PickPending()) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    if (gadgetHoverIndex_.has_value() || cubeHoverZone_.has_value()) {
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if (session_->CurrentTool() == kachakacha::v2::modeling::DrawingTool::Select) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    setCursor(Qt::CrossCursor);
}

void V2Viewport::SetContextMenuCallback(std::function<void(const QPoint&)> callback)
{
    contextMenu_ = std::move(callback);
}

void V2Viewport::PressRightWithoutMoving()
{
    using kachakacha::v2::modeling::DrawingTool;
    const DrawingTool tool = session_->CurrentTool();
    if (tool == DrawingTool::Select) {
        // V1と同じ。選択道具のときだけ、右クリックでメニューを出す。
        if (contextMenu_) {
            contextMenu_(QCursor::pos());
        }
        return;
    }
    // ポリラインとスプラインは、右クリックが「ここで確定」である。
    // 点をいくつ置くか決まっていないので、終わりを伝える手立てが要る。
    const std::size_t placed = session_->PlacedPointCount();
    const bool canFinish = (tool == DrawingTool::Polyline && placed >= 2)
        || (tool == DrawingTool::Spline && placed >= 4);
    if (canFinish) {
        FinishTool();
        return;
    }
    if (placed > 0) {
        // 途中なら取り消す。全部消えるので、Esc と同じ言い方をする。
        session_->CancelTool();
        hover_.preview.clear();
        status_ = "作図をやめました。";
        if (statusCallback_) {
            statusCallback_(status_);
        }
        update();
        return;
    }
    // 1点も置いていない道具の右クリックは、道具を抜けて選択へ戻す。
    // V1 は「近くの点から引き始める」に使っていたが、
    // それは吸着の拾い方が違うので、まだ同じにはできない。
    // できないことを、できたことにしない。
    if (backToSelect_) {
        backToSelect_();
    }
    status_ = "選択道具に戻りました。";
    if (statusCallback_) {
        statusCallback_(status_);
    }
    RefreshCursorShape();
    update();
}
