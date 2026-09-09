//! 視点の操作板(V1同等)。V2Viewport の一部だが、ファイルを分ける。
//!
//! 1ファイル1500行の決まりがあり、Viewport 本体はもう一杯である。
//! 操作板は「輪と矢印を並べて、押されたら core へ渡す」だけの塊なので、
//! ここへ切り出しても本体の読みやすさは落ちない。

#include "V2Viewport.h"

#include <QFont>
#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace {

//! これ以上動かしたら「引きずった」とみなす。V2Viewport.cpp と同じ値にする。
//! 別の値にすると、キューブと操作板で押した感じが変わってしまう。
constexpr double kGadgetDragThresholdPx = 3.0;

} // namespace

kachakacha::v2::view::ViewGadgetLayout V2Viewport::ViewGadgets() const
{
    const QRectF box = ViewCubeRect();
    return kachakacha::v2::view::BuildViewGadgets(box.left(), box.top(), box.width(),
        orientation_);
}

std::optional<std::size_t> V2Viewport::ViewGadgetAt(const QPointF& position) const
{
    return kachakacha::v2::view::ViewGadgetAtScreen(ViewGadgets(), position.x(),
        position.y());
}

void V2Viewport::SetRingMode(kachakacha::v2::view::RotationAxisMode mode)
{
    ringMode_ = mode;
    update();
}

void V2Viewport::SetAlignSelectionCallback(std::function<void()> callback)
{
    alignSelectionCallback_ = std::move(callback);
}

bool V2Viewport::PressViewGadget(const QPointF& position,
    kachakacha::v2::view::AxisArrowModifier modifier)
{
    const auto index = ViewGadgetAt(position);
    if (!index.has_value()) {
        return false;
    }
    ViewGadgetDrag drag;
    drag.index = *index;
    drag.pressPosition = position;
    drag.orientationAtPress = orientation_;
    drag.modifier = modifier;
    gadgetDrag_ = drag;
    return true;
}

void V2Viewport::DragViewGadget(const QPointF& position)
{
    if (!gadgetDrag_.has_value()) {
        return;
    }
    const auto layout = ViewGadgets();
    if (gadgetDrag_->index >= layout.gadgets.size()) {
        return;
    }
    const kachakacha::v2::view::ViewGadget& gadget = layout.gadgets[gadgetDrag_->index];
    // 引きずって回せるのは、回すための部品だけである。
    // 家や「選択に正対」は引きずっても何も起きない。
    if (gadget.kind != kachakacha::v2::view::ViewGadgetKind::AxisRing
        && gadget.kind != kachakacha::v2::view::ViewGadgetKind::Orbit
        && gadget.kind != kachakacha::v2::view::ViewGadgetKind::Roll) {
        return;
    }
    const double dx = position.x() - gadgetDrag_->pressPosition.x();
    const double dy = position.y() - gadgetDrag_->pressPosition.y();
    // 上下の部品は縦の動きで、それ以外は横の動きで回す。
    const bool vertical = gadget.kind == kachakacha::v2::view::ViewGadgetKind::Orbit
        && (gadget.direction == kachakacha::v2::view::ViewGadgetDirection::Up
            || gadget.direction == kachakacha::v2::view::ViewGadgetDirection::Down);
    const double dragPx = vertical ? dy : dx;
    if (std::abs(dragPx) <= kGadgetDragThresholdPx && !gadgetDrag_->moved) {
        return;
    }
    gadgetDrag_->moved = true;
    ApplyGadgetRotation(gadget, dragPx
            * kachakacha::v2::view::AxisArrowDegreesPerPixel(gadgetDrag_->modifier),
        gadgetDrag_->orientationAtPress);
}

void V2Viewport::ReleaseViewGadget(const QPointF& position)
{
    if (!gadgetDrag_.has_value()) {
        return;
    }
    const auto layout = ViewGadgets();
    const bool moved = gadgetDrag_->moved;
    const std::size_t index = gadgetDrag_->index;
    const auto orientationAtPress = gadgetDrag_->orientationAtPress;
    gadgetDrag_.reset();
    (void)position;
    if (moved || index >= layout.gadgets.size()) {
        return; // 引きずったぶんはもう回してある。離した所では何も足さない。
    }
    const kachakacha::v2::view::ViewGadget& gadget = layout.gadgets[index];
    switch (gadget.kind) {
    case kachakacha::v2::view::ViewGadgetKind::Home:
        SetViewDirection(ViewDirection::Isometric);
        viewMessage_ = "既定の視点へ戻しました。";
        break;
    case kachakacha::v2::view::ViewGadgetKind::ModeToggle:
        SetRingMode(ringMode_ == kachakacha::v2::view::RotationAxisMode::World
                ? kachakacha::v2::view::RotationAxisMode::Relative
                : kachakacha::v2::view::RotationAxisMode::World);
        viewMessage_ = std::string("輪の軸を ")
            + std::string(kachakacha::v2::view::RotationAxisModeNameJa(ringMode_))
            + " にしました。";
        break;
    case kachakacha::v2::view::ViewGadgetKind::AlignSelection:
        if (alignSelectionCallback_) {
            alignSelectionCallback_();
        }
        break;
    default:
        // 回すものは15度だけ回る。90度へは飛ばない。
        ApplyGadgetRotation(gadget, kachakacha::v2::view::kAxisArrowClickDegrees,
            orientationAtPress);
        break;
    }
    if (statusCallback_ && !viewMessage_.empty()) {
        statusCallback_(viewMessage_);
    }
    update();
}

void V2Viewport::ApplyGadgetRotation(const kachakacha::v2::view::ViewGadget& gadget,
    double degrees, const kachakacha::v2::view::Quaternion& from)
{
    using kachakacha::v2::view::ViewGadgetDirection;
    using kachakacha::v2::view::ViewGadgetKind;
    kachakacha::v2::base::Result<kachakacha::v2::view::Quaternion> rotated =
        kachakacha::v2::base::Result<kachakacha::v2::view::Quaternion>::Success(from);
    if (gadget.kind == ViewGadgetKind::AxisRing) {
        kachakacha::v2::view::AxisArrowRequest request;
        request.orientation = from;
        request.mode = ringMode_;
        request.axis = gadget.axis;
        request.hasSelectionFrame = selectionFrame_.has_value();
        if (selectionFrame_.has_value()) {
            request.selectionFrame = *selectionFrame_;
        }
        rotated = kachakacha::v2::view::RotateByAxisAngle(request,
            gadget.direction == ViewGadgetDirection::Positive ? degrees : -degrees);
    } else {
        rotated = kachakacha::v2::view::RotateByScreenAxis(from, gadget.direction, degrees);
    }
    if (!rotated.HasValue()) {
        viewMessage_ = rotated.Diagnostics().front().summaryJa;
        if (statusCallback_) {
            statusCallback_(viewMessage_);
        }
        return;
    }
    viewMessage_.clear();
    SetOrientation(rotated.Value());
}

//! 矢じり1つ。置く点と、そこでの進む向きから三角を作る。
void V2Viewport::DrawArrowHead(QPainter& painter, const QPointF& head,
    const QPointF& tangent, double sizePx, const QColor& ink)
{
    const double normalX = -tangent.y();
    const double normalY = tangent.x();
    QPolygonF triangle;
    triangle << QPointF(head.x() + tangent.x() * sizePx, head.y() + tangent.y() * sizePx)
             << QPointF(head.x() + normalX * sizePx * 0.6, head.y() + normalY * sizePx * 0.6)
             << QPointF(head.x() - normalX * sizePx * 0.6, head.y() - normalY * sizePx * 0.6);
    painter.setBrush(ink);
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(triangle);
}

void V2Viewport::DrawViewRings(QPainter& painter,
    const kachakacha::v2::view::ViewGadgetLayout& layout) const
{
    // 相対の輪は、部品を1つ選んでいないと使えない。薄く出す。消さない。
    const bool usable = ringMode_ == kachakacha::v2::view::RotationAxisMode::World
        || selectionFrame_.has_value();
    for (const kachakacha::v2::view::ViewAxisRing& ring : layout.rings) {
        QColor ink = palette_.axisX;
        switch (ring.axis) {
        case kachakacha::v2::view::RotationAxis::X: ink = palette_.axisX; break;
        case kachakacha::v2::view::RotationAxis::Y: ink = palette_.axisY; break;
        case kachakacha::v2::view::RotationAxis::Z: ink = palette_.axisZ; break;
        }
        if (!usable) {
            ink = palette_.construction;
        }
        QPolygonF path;
        for (const auto& point : ring.points) {
            path << QPointF(point.x, point.y);
        }
        QColor line = ink;
        line.setAlpha(usable ? 170 : 90);
        painter.setPen(QPen(line, 1.6));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(path);
        const double headSize = std::max(4.0, ViewCubeRect().width() * 0.09);
        DrawArrowHead(painter, QPointF(ring.positiveHead.x, ring.positiveHead.y),
            QPointF(ring.positiveTangent.x, ring.positiveTangent.y), headSize, ink);
        DrawArrowHead(painter, QPointF(ring.negativeHead.x, ring.negativeHead.y),
            QPointF(ring.negativeTangent.x, ring.negativeTangent.y), headSize, ink);
    }
}

void V2Viewport::DrawViewButtons(QPainter& painter,
    const kachakacha::v2::view::ViewGadgetLayout& layout) const
{
    using kachakacha::v2::view::ViewGadgetDirection;
    using kachakacha::v2::view::ViewGadgetKind;
    const QFont previous = painter.font();
    QFont label = previous;
    label.setPointSizeF(std::max(6.0, previous.pointSizeF() - 1.0));
    painter.setFont(label);
    for (std::size_t index = 0; index < layout.gadgets.size(); ++index) {
        const kachakacha::v2::view::ViewGadget& gadget = layout.gadgets[index];
        if (gadget.kind == ViewGadgetKind::AxisRing) {
            continue; // 輪の矢じりは DrawViewRings が描いている。
        }
        const QRectF cell(gadget.xPx, gadget.yPx, gadget.widthPx, gadget.heightPx);
        const bool usable = gadget.kind != ViewGadgetKind::AlignSelection
            || selectionFrame_.has_value();
        const bool hot = (gadgetDrag_.has_value() && gadgetDrag_->index == index)
            || (gadgetHoverIndex_.has_value() && *gadgetHoverIndex_ == index);
        QColor face = palette_.background;
        face.setAlpha(usable ? 210 : 120);
        painter.setBrush(face);
        painter.setPen(QPen(hot ? palette_.selected : palette_.gridMajor, hot ? 1.6 : 1.0));
        painter.drawRoundedRect(cell, 3.0, 3.0);
        const QColor ink = usable ? palette_.text : palette_.construction;
        painter.setPen(QPen(ink, 1.4));
        painter.setBrush(Qt::NoBrush);
        QString text;
        switch (gadget.kind) {
        case ViewGadgetKind::Home:
            text = QStringLiteral("⌂");
            break;
        case ViewGadgetKind::Roll:
            text = gadget.direction == ViewGadgetDirection::Positive
                ? QStringLiteral("↺")
                : QStringLiteral("↻");
            break;
        case ViewGadgetKind::Orbit:
            switch (gadget.direction) {
            case ViewGadgetDirection::Up:    text = QStringLiteral("▲"); break;
            case ViewGadgetDirection::Down:  text = QStringLiteral("▼"); break;
            case ViewGadgetDirection::Left:  text = QStringLiteral("◀"); break;
            default:                         text = QStringLiteral("▶"); break;
            }
            break;
        case ViewGadgetKind::ModeToggle:
            text = QString::fromUtf8(std::string(
                kachakacha::v2::view::RotationAxisModeNameJa(ringMode_)).c_str());
            break;
        case ViewGadgetKind::AlignSelection:
            text = QStringLiteral("選択に正対");
            break;
        default:
            break;
        }
        painter.drawText(cell, Qt::AlignCenter, text);
    }
    painter.setFont(previous);
}

void V2Viewport::DrawViewGadgets(QPainter& painter) const
{
    const auto layout = ViewGadgets();
    if (layout.gadgets.empty()) {
        return;
    }
    // 画面に入らないなら出さない。はみ出したものは押せない。
    if (layout.xPx < 0.0 || layout.yPx < 0.0 || layout.xPx + layout.widthPx > width()
        || layout.yPx + layout.heightPx > height()) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    DrawViewRings(painter, layout);
    DrawViewButtons(painter, layout);
    painter.restore();
}

