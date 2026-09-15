//! 押し出しの矢印ハンドル(オーナー指示 2026-09-14 §6)。
//!
//! 押し出しを始めると、輪郭の重心から押し出しの向きへ矢印が出る。
//! 掴んで引くと距離が変わり、矢印の横に「15.0 mm」と出る。
//! 右の欄の数字とは常に同じ値を出す ── 片方だけ動くと、
//! どちらが本当の距離なのか分からなくなる。
//!
//! 距離の計算は core(app/ExtrudeDrag)にある。ここは描くことと、
//! マウスの便りをそちらへ渡すことだけをする。

#include "V2Viewport.h"

#include "kachakacha/app/ExtrudeDrag.h"
#include "kachakacha/geometry/ScreenMapping.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QString>

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace {

//! 矢印の頭の長さ(画面 px)。
constexpr double kHeadPx = 14.0;
//! 矢印を掴める太さ(画面 px)。細すぎると掴めない。
constexpr double kGrabPx = 12.0;

//! 点と線分の距離(画面 px)。
[[nodiscard]] double DistanceToSegment(const QPointF& point, const QPointF& from,
    const QPointF& to)
{
    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1.0e-12) {
        return std::hypot(point.x() - from.x(), point.y() - from.y());
    }
    double t = ((point.x() - from.x()) * dx + (point.y() - from.y()) * dy) / lengthSquared;
    t = std::max(0.0, std::min(1.0, t));
    return std::hypot(point.x() - (from.x() + dx * t), point.y() - (from.y() + dy * t));
}

} // namespace

void V2Viewport::ShowExtrudeHandle(const kachakacha::v2::app::ExtrudeHandle& handle,
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> preview)
{
    extrudeHandle_.shown = true;
    extrudeHandle_.handle = handle;
    extrudeHandle_.preview = std::move(preview);
    extrudeHandle_.dragging = false;
    update();
}

void V2Viewport::SetExtrudePreviewFaces(
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> faces)
{
    extrudeHandle_.faces = std::move(faces);
    update();
}

void V2Viewport::HideExtrudeHandle()
{
    extrudeHandle_ = ExtrudeHandleState{};
    update();
}

void V2Viewport::ShowToolPreview(
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> lines)
{
    toolPreview_ = std::move(lines);
    update();
}

void V2Viewport::HideToolPreview()
{
    if (toolPreview_.empty()) {
        return;
    }
    toolPreview_.clear();
    update();
}

//! 道具の下見。押し出しの矢印と同じ描き方にする。
//! **出来上がりの線であって、文書の線ではない。**同じ見た目にすると取り違える。
void V2Viewport::DrawToolPreview(QPainter& painter) const
{
    if (toolPreview_.empty()) {
        return;
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(palette_.preview, 1.0, Qt::DashLine));
    for (const auto& line : toolPreview_) {
        QPolygonF path;
        bool complete = true;
        for (const auto& point : line) {
            const auto screen = ToScreen(point);
            if (!screen.has_value()) {
                complete = false;
                break;
            }
            path << *screen;
        }
        if (complete && path.size() >= 2) {
            painter.drawPolyline(path);
        }
    }
}

void V2Viewport::ShowToolRoleLabels(std::vector<PlacedRoleLabel> labels)
{
    toolRoleLabels_ = std::move(labels);
    update();
}

void V2Viewport::HideToolRoleLabels()
{
    if (toolRoleLabels_.empty()) {
        return;
    }
    toolRoleLabels_.clear();
    update();
}

//! 役割の札。白フチを下に敷いてから色を重ねる。どんな背景でも読める。
void V2Viewport::DrawToolRoleLabels(QPainter& painter) const
{
    for (const auto& label : toolRoleLabels_) {
        const auto screen = ToScreen(label.at);
        if (!screen.has_value()) {
            continue;
        }
        // 線の真上に置くと線に重なって読めない。少し上へ逃がす。
        const QPointF at(screen->x() + 6.0, screen->y() - 6.0);
        painter.setPen(QPen(QColor(255, 255, 255, 235), 3.0));
        painter.drawText(at, label.text);
        painter.setPen(QPen(palette_.selected, 1.0));
        painter.drawText(at, label.text);
    }
}

void V2Viewport::SetExtrudeDistanceCallback(std::function<void(double)> callback)
{
    extrudeDistanceChanged_ = std::move(callback);
}

void V2Viewport::SetExtrudeCallbacks(std::function<void()> confirm,
    std::function<void()> cancel)
{
    confirmExtrude_ = std::move(confirm);
    cancelExtrude_ = std::move(cancel);
}

bool V2Viewport::BeginExtrudeDrag(const QPointF& position)
{
    using kachakacha::v2::app::ExtrudeHandleTip;
    if (!extrudeHandle_.shown) {
        return false;
    }
    const auto base = ToScreen(extrudeHandle_.handle.origin);
    const auto tip = ToScreen(ExtrudeHandleTip(extrudeHandle_.handle));
    if (!base.has_value() || !tip.has_value()) {
        return false;
    }
    // 距離が 0 のときは矢印が点になるので、根元の近くを掴めるようにする。
    const double distance = DistanceToSegment(position, *base, *tip);
    if (distance > kGrabPx) {
        return false;
    }
    extrudeHandle_.dragging = true;
    extrudeHandle_.pressedPx = position;
    extrudeHandle_.distanceAtPressMm = extrudeHandle_.handle.distanceMm;
    return true;
}

void V2Viewport::DragExtrude(const QPointF& position)
{
    using kachakacha::v2::geometry::ScreenPoint;
    if (!extrudeHandle_.dragging) {
        return;
    }
    // 掴んだ時点の距離を基準にする。掴んだ瞬間に飛ばないため。
    kachakacha::v2::app::ExtrudeHandle atPress = extrudeHandle_.handle;
    atPress.distanceMm = extrudeHandle_.distanceAtPressMm;
    const double distance = kachakacha::v2::app::ExtrudeDistanceAfterDrag(atPress, mapping_,
        ScreenPoint{extrudeHandle_.pressedPx.x(), extrudeHandle_.pressedPx.y()},
        ScreenPoint{position.x(), position.y()});
    if (distance == extrudeHandle_.handle.distanceMm) {
        return;
    }
    extrudeHandle_.handle.distanceMm = distance;
    // 右の欄へ知らせる。片方だけ動くと、どちらが本当の距離か分からなくなる。
    if (extrudeDistanceChanged_) {
        extrudeDistanceChanged_(distance);
    }
    update();
}

void V2Viewport::DrawExtrudeHandle(QPainter& painter) const
{
    using kachakacha::v2::app::ExtrudeDistanceLabel;
    using kachakacha::v2::app::ExtrudeHandleTip;
    if (!extrudeHandle_.shown) {
        return;
    }
    const auto base = ToScreen(extrudeHandle_.handle.origin);
    const auto tip = ToScreen(ExtrudeHandleTip(extrudeHandle_.handle));
    if (!base.has_value() || !tip.has_value()) {
        return;
    }
    // うすい面を一番下に敷く(§8)。線だけだと厚みがついたのか読めない。
    // 濃く塗ると元の図が沈むので、透かして塗る。縁は描かない(線が別に出る)。
    if (!extrudeHandle_.faces.empty()) {
        QColor fill = palette_.preview;
        fill.setAlpha(46);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        for (const auto& face : extrudeHandle_.faces) {
            QPolygonF path;
            bool complete = true;
            for (const auto& point : face) {
                const auto screen = ToScreen(point);
                if (!screen.has_value()) {
                    complete = false;
                    break;
                }
                path << *screen;
            }
            if (complete && path.size() >= 3) {
                painter.drawPolygon(path);
            }
        }
    }

    // 出来上がる形を先に、細い破線で出す。矢印はその上に描く。
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(palette_.preview, 1.0, Qt::DashLine));
    for (const auto& loop : extrudeHandle_.preview) {
        QPolygonF path;
        bool complete = true;
        for (const auto& point : loop) {
            const auto screen = ToScreen(point);
            if (!screen.has_value()) {
                complete = false;
                break;
            }
            path << *screen;
        }
        if (complete && path.size() >= 2) {
            painter.drawPolyline(path);
        }
    }

    // 矢印。白フチを下に敷いてから色を重ねる(V1 と同じ)。どんな背景でも見える。
    const double dx = tip->x() - base->x();
    const double dy = tip->y() - base->y();
    const double length = std::hypot(dx, dy);
    for (int pass = 0; pass < 2; ++pass) {
        const QColor color = pass == 0 ? QColor(255, 255, 255, 220) : palette_.selected;
        painter.setPen(QPen(color, pass == 0 ? 5.0 : 2.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(*base, *tip);
        if (length < 1.0e-6) {
            continue;   // まだ距離が 0。頭は描けない。
        }
        const double ux = dx / length;
        const double uy = dy / length;
        const double head = std::min(kHeadPx, length);
        const QPointF wing(-uy * head * 0.4, ux * head * 0.4);
        const QPointF back(tip->x() - ux * head, tip->y() - uy * head);
        painter.setBrush(color);
        const QPointF arrow[3] = {*tip, back + wing, back - wing};
        painter.drawPolygon(arrow, 3);
    }

    // 距離。矢印の先の少し外へ出す。矢印に重ねると読めない。
    const QString label =
        QString::fromStdString(ExtrudeDistanceLabel(extrudeHandle_.handle.distanceMm));
    const QPointF at(tip->x() + 12.0, tip->y() - 8.0);
    painter.setPen(QPen(QColor(255, 255, 255, 235), 3.0));
    painter.drawText(at, label);
    painter.setPen(QPen(palette_.selected, 1.0));
    painter.drawText(at, label);
}
