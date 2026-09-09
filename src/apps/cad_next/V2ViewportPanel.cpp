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

QPointF V2Viewport::NavigatorCenter() const
{
    // 操作板の中心。右上に置く。ここから先の場所は全部 core が決める。
    // 画面側で別に持つと、寄せたときにずれる。
    return QPointF(static_cast<double>(width()) - 110.0, 110.0);
}

kachakacha::v2::view::ViewGadgetLayout V2Viewport::ViewGadgets() const
{
    const QPointF center = NavigatorCenter();
    auto layout = kachakacha::v2::view::BuildViewGadgets(center.x(), center.y(),
        kachakacha::v2::view::kNavigatorScalePx, orientation_);
    // はみ出していたら寄せる。入らなければ空にして、出さない判断をここで済ませる。
    if (!kachakacha::v2::view::FitViewGadgetsIntoScreen(layout,
            static_cast<double>(width()), static_cast<double>(height()))) {
        return kachakacha::v2::view::ViewGadgetLayout{};
    }
    return layout;
}

std::optional<std::size_t> V2Viewport::ViewGadgetAt(const QPointF& position) const
{
    return kachakacha::v2::view::ViewGadgetAtScreen(ViewGadgets(), position.x(),
        position.y());
}

std::optional<std::size_t> V2Viewport::ViewButtonAt(const QPointF& position) const
{
    return kachakacha::v2::view::ViewButtonAtScreen(ViewGadgets(), position.x(),
        position.y());
}

std::optional<std::size_t> V2Viewport::ViewRingAt(const QPointF& position) const
{
    return kachakacha::v2::view::ViewRingAtScreen(ViewGadgets(), position.x(),
        position.y());
}

void V2Viewport::SetAlignSelectionCallback(std::function<void()> callback)
{
    alignSelectionCallback_ = std::move(callback);
}

bool V2Viewport::PressViewGadgetIndex(const QPointF& position,
    const std::optional<std::size_t>& index,
    kachakacha::v2::view::AxisArrowModifier modifier)
{
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

bool V2Viewport::PressViewGadget(const QPointF& position,
    kachakacha::v2::view::AxisArrowModifier modifier)
{
    return PressViewGadgetIndex(position, ViewGadgetAt(position), modifier);
}

bool V2Viewport::PressViewButton(const QPointF& position,
    kachakacha::v2::view::AxisArrowModifier modifier)
{
    return PressViewGadgetIndex(position, ViewButtonAt(position), modifier);
}

V2Viewport::ViewPress V2Viewport::PressViewNavigator(const QPointF& position,
    kachakacha::v2::view::AxisArrowModifier modifier)
{
    if (PressViewButton(position, modifier)) {
        return ViewPress::Button;
    }
    if (PressViewCube(position)) {
        return ViewPress::Cube;
    }
    if (PressViewRing(position, modifier)) {
        return ViewPress::Ring;
    }
    return ViewPress::None;
}

bool V2Viewport::PressViewRing(const QPointF& position,
    kachakacha::v2::view::AxisArrowModifier modifier)
{
    return PressViewGadgetIndex(position, ViewRingAt(position), modifier);
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
        request.mode = kachakacha::v2::view::RotationAxisMode::World;
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

//! その矢じりが、いま指されているか押されているか。
bool V2Viewport::IsRingHeadHot(kachakacha::v2::view::RotationAxis axis,
    bool positive) const
{
    const auto layout = ViewGadgets();
    const auto index = gadgetDrag_.has_value()
        ? std::optional<std::size_t>(gadgetDrag_->index)
        : gadgetHoverIndex_;
    if (!index.has_value() || *index >= layout.gadgets.size()) {
        return false;
    }
    const auto& gadget = layout.gadgets[*index];
    return gadget.kind == kachakacha::v2::view::ViewGadgetKind::AxisRing
        && gadget.axis == axis
        && (gadget.direction == kachakacha::v2::view::ViewGadgetDirection::Positive)
            == positive;
}

void V2Viewport::DrawViewRings(QPainter& painter,
    const kachakacha::v2::view::ViewGadgetLayout& layout) const
{
    // 輪はモデルの軸まわりなので、いつでも使える(V1と同じ)。
    const bool usable = true;
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
        // 矢じりの下に丸い座を敷く。掴めるところがどこか、目で分かるようにする。
        // **座の大きさは当たり判定と同じにする。**
        // 見えている丸より当たり判定が小さいと、押したのに反応しない。
        const double grab = kachakacha::v2::view::kViewRingHeadSizePx;
        const double headSize = grab * 0.52;
        const std::pair<kachakacha::v2::geometry::ScreenPoint,
            kachakacha::v2::geometry::ScreenPoint> heads[] = {
            {ring.positiveHead, ring.positiveTangent},
            {ring.negativeHead, ring.negativeTangent},
        };
        for (const auto& head : heads) {
            const QPointF at(head.first.x, head.first.y);
            const bool hot = IsRingHeadHot(ring.axis, &head == &heads[0]);
            QColor seat = palette_.background;
            seat.setAlpha(usable ? 225 : 140);
            painter.setBrush(seat);
            painter.setPen(QPen(hot ? palette_.selected : ink, hot ? 2.0 : 1.4));
            painter.drawEllipse(at, grab * 0.5, grab * 0.5);
            DrawArrowHead(painter, at, QPointF(head.second.x, head.second.y), headSize,
                ink);
        }
    }
}

void V2Viewport::DrawViewButtons(QPainter& painter,
    const kachakacha::v2::view::ViewGadgetLayout& layout) const
{
    using kachakacha::v2::view::ViewGadgetDirection;
    using kachakacha::v2::view::ViewGadgetKind;
    // 記号はボタンいっぱいに出す。小さいと、何のボタンか分からない。
    const QFont previous = painter.font();
    QFont label = previous;
    label.setPointSizeF(previous.pointSizeF() + 3.0);
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
        case ViewGadgetKind::AlignSelection: {
            // この文字だけは長いので、元の大きさに戻す。
            painter.setFont(previous);
            text = QStringLiteral("選択に正対");
            break;
        }
        default:
            break;
        }
        painter.drawText(cell, Qt::AlignCenter, text);
        if (gadget.kind == ViewGadgetKind::AlignSelection) {
            painter.setFont(label);
        }
    }
    painter.setFont(previous);
}

void V2Viewport::DrawViewGadgets(QPainter& painter) const
{
    // 輪だけを描く。キューブはこのあと、輪の上に描かれる。
    const auto layout = ViewGadgets();
    if (layout.gadgets.empty()) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    DrawViewRings(painter, layout);
    painter.restore();
}

void V2Viewport::DrawViewButtonsOnTop(QPainter& painter) const
{
    // ボタンはいちばん上。キューブとは重ならないので順は見た目だけの話だが、
    // 押す順(ボタンが最優先)と合わせておく。
    const auto layout = ViewGadgets();
    if (layout.gadgets.empty()) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    DrawViewButtons(painter, layout);
    painter.restore();
}

