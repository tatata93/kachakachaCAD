//! 画面に描く側(V2Viewport の描画の半分)。
//! 入力と状態は V2Viewport.cpp / V2ViewportInput.cpp、操作板は V2ViewportPanel.cpp にある。
//! 分けたのは、ファイルの長さの門(1500行)を守るため。

#include "V2Viewport.h"

#include "kachakacha/app/ControlPointPick.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/geometry/WireEdit.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <QPolygonF>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/Units.h"

using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::ScreenPoint;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::modeling::SnapKind;
using kachakacha::v2::modeling::SnapKindLabelJa;
using kachakacha::v2::modeling::WorkPlaneFrame;

void V2Viewport::DrawGrid(QPainter& painter) const
{
    const auto& grid = session_->Scene().grid;
    if (!grid.visible || !display_.gridVisible || gridSuppressedByMode_) {
        return;
    }
    const double pixelsPerMm = mapping_.PixelsPerMillimeterAt(workPlane_.origin);
    if (!(pixelsPerMm > 0.0)) {
        return;
    }
    // 間隔と副点は場面のグリッド(棚で決めたもの)。画面側で別の値を持たない。
    // 持っていたころ、「グリッド」で間隔を変えても画面が変わらなかった。
    const double majorMm = std::max(grid.majorSpacingMm, 0.001);
    // 画面で6px を下回る間隔は出さない(geometry-contract §6.3)。
    const double majorPx = majorMm * pixelsPerMm;
    const int reach = static_cast<int>(std::ceil(visibleWidthMm_ / majorMm));
    const int limited = std::min(reach, 400);

    const auto drawSet = [&](double spacingMm, const QColor& color) {
        if (spacingMm * pixelsPerMm < 6.0) {
            return;
        }
        painter.setPen(QPen(color, 1.0));
        const int count = std::min(limited,
            static_cast<int>(std::ceil(visibleWidthMm_ / spacingMm)) + 2);
        for (int index = -count; index <= count; ++index) {
            const double offset = index * spacingMm;
            const Vector3 a = grid.origin + grid.uDirection * offset
                - grid.vDirection * (count * spacingMm);
            const Vector3 b = grid.origin + grid.uDirection * offset
                + grid.vDirection * (count * spacingMm);
            const auto sa = ToScreen(a);
            const auto sb = ToScreen(b);
            if (sa && sb) {
                painter.drawLine(*sa, *sb);
            }
            const Vector3 c = grid.origin + grid.vDirection * offset
                - grid.uDirection * (count * spacingMm);
            const Vector3 d = grid.origin + grid.vDirection * offset
                + grid.uDirection * (count * spacingMm);
            const auto sc = ToScreen(c);
            const auto sd = ToScreen(d);
            if (sc && sd) {
                painter.drawLine(*sc, *sd);
            }
        }
    };
    if (majorPx >= 6.0) {
        // 副点: 0 = 主点のみ、2 = 1/2、3 = 1/3、4 = 1/4(V1 と同じ)。
        if (grid.subdivision >= 2) {
            drawSet(majorMm / grid.subdivision, palette_.gridMinor);
        }
        drawSet(majorMm, palette_.gridMajor);
    }
}

void V2Viewport::DrawAxes(QPainter& painter) const
{
    const double length = visibleWidthMm_ * 0.5;
    const std::pair<Vector3, QColor> axes[3] = {
        {Vector3{length, 0.0, 0.0}, palette_.axisX},
        {Vector3{0.0, length, 0.0}, palette_.axisY},
        {Vector3{0.0, 0.0, length}, palette_.axisZ},
    };
    const auto origin = ToScreen(Vector3{});
    if (!origin.has_value()) {
        return;
    }
    for (int index = 0; index < 3; ++index) {
        if (!axisVisible_[index]) {
            continue;
        }
        const auto& axis = axes[index];
        const auto end = ToScreen(axis.first);
        if (!end.has_value()) {
            continue;
        }
        painter.setPen(QPen(axis.second, 1.0, Qt::DashLine));
        painter.drawLine(*origin, *end);
    }
}

void V2Viewport::DrawWorkPlane(QPainter& painter) const
{
    const double half = visibleWidthMm_ * 0.35;
    const Vector3 corners[4] = {
        workPlane_.PointAt(-half, -half),
        workPlane_.PointAt(half, -half),
        workPlane_.PointAt(half, half),
        workPlane_.PointAt(-half, half),
    };
    QPolygonF polygon;
    for (const Vector3& corner : corners) {
        const auto screen = ToScreen(corner);
        if (!screen.has_value()) {
            return;
        }
        polygon << *screen;
    }
    QColor edge = palette_.workPlane;
    edge.setAlpha(140);
    painter.setPen(QPen(edge, 1.0, Qt::DotLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(polygon);
}

namespace {

[[nodiscard]] Qt::PenStyle PenStyleOf(kachakacha::v2::app::LineStyle style)
{
    switch (style) {
    case kachakacha::v2::app::LineStyle::Solid:  return Qt::SolidLine;
    case kachakacha::v2::app::LineStyle::Dashed: return Qt::DashLine;
    case kachakacha::v2::app::LineStyle::Dotted: return Qt::DotLine;
    }
    return Qt::SolidLine;
}

//! その線が作業平面の上にあるか(両端と中央が面から浮いていない)。
[[nodiscard]] bool CurveOnPlane(const CurveSegment& segment, const WorkPlaneFrame& plane)
{
    constexpr double kTolerance = 1.0e-3;
    for (const double t : {0.0, 0.5, 1.0}) {
        const Vector3 point = segment.Evaluate(t);
        if (std::abs(Dot(point - plane.origin, plane.normal)) > kTolerance) {
            return false;
        }
    }
    return true;
}

} // namespace

void V2Viewport::DrawDocument(QPainter& painter) const
{
    const auto& scene = session_->Scene();
    // 作図中は、作図面の上にない線を薄くして、自分の線を見やすくする(V1 の #6)。
    const bool dimming = display_.dimOffPlaneLines
        && session_->CurrentTool() != kachakacha::v2::modeling::DrawingTool::Select;
    for (const auto& curve : scene.curves) {
        QPainterPath path;
        bool started = false;
        AppendCurve(path, curve.segment, started);
        if (!started) {
            continue;
        }
        if (curve.construction && !display_.constructionVisible) {
            continue;
        }
        const bool selected = kachakacha::v2::app::IsSelected(selection_, curve.entityId);
        if (display_.selectionOnly && !selected) {
            continue;   // 「選択だけ」。選んでいないものは出さない(消してはいない)。
        }
        QColor color = selected
            ? palette_.selected
            : (curve.construction ? palette_.construction : palette_.wire);
        if (dimming && !selected && !CurveOnPlane(curve.segment, workPlane_)) {
            color.setAlphaF(color.alphaF() * 0.24);
        }
        // 太さと様式は表示設定(既定は V1 と同じ: 線 2.0 実線、補助線 1.7 破線)。
        // 細い実線は高解像度の画面で点線に見えることがある。
        const double width = selected
            ? std::max(3.2, display_.wireWidthPx + 1.2)
            : (curve.construction ? display_.constructionWidthPx : display_.wireWidthPx);
        // 基準線は一点鎖線(V1 と同じ)。補助線は補助線の様式、ほかは線の様式。
        const Qt::PenStyle style = curve.datum
            ? Qt::DashDotLine
            : PenStyleOf(curve.construction ? display_.constructionStyle : display_.wireStyle);
        painter.setPen(QPen(color, width, style, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        if (selected && bodyDrag_.active && bodyDrag_.moved) {
            // 掴んでいる間の行き先を出す。元の線はそのまま残して、両方見せる。
            // 出さないと、離すまでどこへ行くのか分からない。
            QPainterPath ghost;
            bool ghostStarted = false;
            AppendCurve(ghost, kachakacha::v2::geometry::TranslateCurve(curve.segment,
                bodyDrag_.delta), ghostStarted);
            if (ghostStarted) {
                painter.setPen(QPen(palette_.preview, 1.6, Qt::DashLine));
                painter.drawPath(ghost);
            }
        }
    }
    painter.setPen(QPen(palette_.point, 1.0));
    painter.setBrush(palette_.point);
    for (const auto& point : scene.points) {
        const auto screen = ToScreen(point.position);
        if (!screen.has_value()) {
            continue;
        }
        if (display_.selectionOnly
            && !kachakacha::v2::app::IsSelected(selection_, point.entityId)) {
            continue;
        }
        painter.drawRect(QRectF(screen->x() - 2.0, screen->y() - 2.0, 4.0, 4.0));
    }
    DrawControlPoints(painter);
    DrawFoldPreview(painter);
}

void V2Viewport::SetFoldPreview(
    std::vector<std::vector<kachakacha::v2::geometry::Vector3>> rails)
{
    foldPreview_ = std::move(rails);
    update();
}

void V2Viewport::DrawFoldPreview(QPainter& painter) const
{
    if (foldPreview_.empty()) {
        return;
    }
    // 帯ごとに下レール・上レールの2本が並ぶ。帯を閉じた輪で描くと、
    // 曲げ具合を変えたときに帯が「板」として動くのが見える。
    painter.setPen(QPen(palette_.preview, 1.4, Qt::SolidLine));
    painter.setBrush(Qt::NoBrush);
    for (std::size_t index = 0; index + 1 < foldPreview_.size(); index += 2) {
        const auto& bottom = foldPreview_[index];
        const auto& top = foldPreview_[index + 1];
        QPainterPath path;
        bool started = false;
        const auto appendRail = [&](const std::vector<kachakacha::v2::geometry::Vector3>& rail,
                                    bool reverse) {
            const std::size_t count = rail.size();
            for (std::size_t at = 0; at < count; ++at) {
                const auto& point = rail[reverse ? count - 1 - at : at];
                const auto screen = ToScreen(point);
                if (!screen.has_value()) {
                    continue;
                }
                if (!started) {
                    path.moveTo(*screen);
                    started = true;
                } else {
                    path.lineTo(*screen);
                }
            }
        };
        appendRail(bottom, false);
        appendRail(top, true);
        if (started) {
            path.closeSubpath();
            painter.drawPath(path);
        }
    }
}

void V2Viewport::DrawControlPoints(QPainter& painter) const
{
    // 選んだワイヤーの制御点だけ出す。掴めることが見えていないと、
    // 掴めると思わない。四角は当たり判定(9px)より小さく描く。
    if (session_->CurrentTool() != kachakacha::v2::modeling::DrawingTool::Select) {
        return;
    }
    if (controlDrag_.active && controlDrag_.moved && controlDrag_.preview.has_value()) {
        QPainterPath ghost;
        bool started = false;
        AppendCurve(ghost, *controlDrag_.preview, started);
        if (started) {
            painter.setPen(QPen(palette_.preview, 1.6, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(ghost);
        }
    }
    painter.setPen(QPen(palette_.selected, 1.0));
    painter.setBrush(palette_.background);
    for (const auto& point : kachakacha::v2::app::ControlPointsForSelection(
             session_->Scene(), selection_)) {
        const auto screen = ToScreen(point.position);
        if (!screen.has_value()) {
            continue;
        }
        painter.drawRect(QRectF(screen->x() - 3.0, screen->y() - 3.0, 6.0, 6.0));
    }
}

void V2Viewport::DrawPreview(QPainter& painter) const
{
    if (hover_.preview.empty()) {
        return;
    }
    QPainterPath path;
    bool started = false;
    for (const CurveSegment& segment : hover_.preview) {
        AppendCurve(path, segment, started);
    }
    if (!started) {
        return;
    }
    // 引いている途中の線。V1 と同じく破線 2.4(確定した線は実線)。
    painter.setPen(QPen(palette_.preview, 2.4, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

void V2Viewport::DrawSnap(QPainter& painter) const
{
    if (!hover_.snap.has_value()) {
        return;
    }
    const auto screen = ToScreen(hover_.snap->position);
    if (!screen.has_value()) {
        return;
    }
    painter.setPen(QPen(palette_.snap, 1.5));
    painter.setBrush(Qt::NoBrush);
    const double size = 5.0;
    const SnapKind kind = hover_.snap->kind;
    switch (kind) {
    case SnapKind::Endpoint:
    case SnapKind::DrawingPoint:
        painter.drawRect(QRectF(screen->x() - size, screen->y() - size, size * 2, size * 2));
        break;
    case SnapKind::Midpoint:
        painter.drawPolygon(QPolygonF({QPointF(screen->x(), screen->y() - size),
            QPointF(screen->x() + size, screen->y() + size),
            QPointF(screen->x() - size, screen->y() + size)}));
        break;
    case SnapKind::Center:
    case SnapKind::Quadrant:
        painter.drawEllipse(*screen, size, size);
        break;
    case SnapKind::Intersection:
    case SnapKind::ScreenIntersection:
        painter.drawLine(QPointF(screen->x() - size, screen->y() - size),
            QPointF(screen->x() + size, screen->y() + size));
        painter.drawLine(QPointF(screen->x() - size, screen->y() + size),
            QPointF(screen->x() + size, screen->y() - size));
        break;
    default:
        painter.drawEllipse(*screen, size * 0.7, size * 0.7);
        break;
    }
    // 何に吸着したかを、記号だけでなく言葉でも出す(PRD-061)。
    painter.setPen(QPen(palette_.text, 1.0));
    painter.drawText(QPointF(screen->x() + size + 4.0, screen->y() - size),
        QString::fromUtf8(std::string(SnapKindLabelJa(kind)).c_str()));
}

void V2Viewport::DrawScaleBar(QPainter& painter) const
{
    const double pixelsPerMm = mapping_.PixelsPerMillimeterAt(center_);
    if (!(pixelsPerMm > 0.0)) {
        return;
    }
    // 60px 前後に収まる、切りのよい長さを選ぶ。
    static const double kNice[] = {0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500,
        1000, 2000, 5000};
    double chosen = kNice[0];
    for (const double candidate : kNice) {
        if (candidate * pixelsPerMm <= 90.0) {
            chosen = candidate;
        }
    }
    const double lengthPx = chosen * pixelsPerMm;
    const double baseX = 16.0;
    const double baseY = height() - 20.0;
    painter.setPen(QPen(palette_.text, 1.0));
    painter.drawLine(QPointF(baseX, baseY), QPointF(baseX + lengthPx, baseY));
    painter.drawLine(QPointF(baseX, baseY - 4.0), QPointF(baseX, baseY + 4.0));
    painter.drawLine(QPointF(baseX + lengthPx, baseY - 4.0),
        QPointF(baseX + lengthPx, baseY + 4.0));
    painter.drawText(QPointF(baseX, baseY - 8.0),
        QStringLiteral("%1 mm").arg(chosen, 0, 'g', 4));
}

