//! 画面に描く側(V2Viewport の描画の半分)。
//! 入力と状態は V2Viewport.cpp / V2ViewportInput.cpp、操作板は V2ViewportPanel.cpp にある。
//! 分けたのは、ファイルの長さの門(1500行)を守るため。

#include "V2Viewport.h"

#include "kachakacha/app/PlaneFocus.h"
#include "kachakacha/view/ShapeShading.h"

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

using kachakacha::v2::app::SemanticState;
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

//! 作業平面を描く(V1 の作図面と同じ出し方)。
//!
//! V1 は **すべての作図面** を、薄い塗りと破線の枠で出していた。
//! 作業中の1枚だけを、しかも塗らずに点線の枠で出していたので、
//! グリッドに紛れて「どこに描いているのか分からない」状態になっていた。
//!
//! 色は3通り。選んでいる面(橙)・作業中の面(青緑)・そのほか(灰)。
//! 原点から u(青緑)と v(茶)の短い線を出す。どちらが横でどちらが縦かが読める。
void V2Viewport::DrawWorkPlane(QPainter& painter) const
{
    if (!display_.workPlaneVisible) {
        return;
    }
    if (workPlaneViews_.empty()) {
        // 文書の面がまだ届いていないときも、いま描いている面だけは出す。
        // 何も出さないと、描く場所が画面から消える。
        DrawOneWorkPlane(painter, WorkPlaneView{{}, workPlane_, QString(), true});
        return;
    }
    for (const WorkPlaneView& plane : workPlaneViews_) {
        DrawOneWorkPlane(painter, plane);
    }
}

void V2Viewport::DrawOneWorkPlane(QPainter& painter, const WorkPlaneView& plane) const
{
    // 大きさは画面に合わせる。固定寸法だと、寄ると枠が画面の外へ出て見えなくなり、
    // 引くと点にしか見えない。
    const double half = std::clamp(visibleWidthMm_ * 0.3, 5.0, 5000.0);
    const Vector3 corners[4] = {
        plane.frame.PointAt(-half, -half),
        plane.frame.PointAt(half, -half),
        plane.frame.PointAt(half, half),
        plane.frame.PointAt(-half, half),
    };
    QPolygonF polygon;
    for (const Vector3& corner : corners) {
        const auto screen = ToScreen(corner);
        if (!screen.has_value()) {
            return;
        }
        polygon << *screen;
    }
    const bool selected = !plane.entityId.IsNil()
        && kachakacha::v2::app::IsSelected(selection_, plane.entityId);
    // 選んだ面の色はテーマの選択色にする。固定の橙だと、テーマを替えたときに
    // 線の選択色と面の選択色が食い違う。
    QColor selectedFill = SemanticColor(SemanticState::Selected);
    selectedFill.setAlpha(52);
    QColor fill = selected ? selectedFill
        : (plane.active ? QColor(0, 127, 120, 36) : QColor(69, 132, 142, 18));
    QColor edge = selected ? SemanticColor(SemanticState::Selected)
        : (plane.active ? QColor(0x00, 0x7f, 0x78) : QColor(0x7d, 0x9a, 0xa0));
    painter.setBrush(fill);
    painter.setPen(QPen(edge, selected || plane.active ? 2.2 : 1.0, Qt::DashLine));
    painter.drawPolygon(polygon);

    // 原点と u/v。どちらが横でどちらが縦かを、色で見分ける(V1 と同じ色)。
    const double tick = half * 0.22;
    const auto origin = ToScreen(plane.frame.origin);
    const auto uEnd = ToScreen(plane.frame.PointAt(tick, 0.0));
    const auto vEnd = ToScreen(plane.frame.PointAt(0.0, tick));
    if (origin.has_value() && uEnd.has_value() && vEnd.has_value()) {
        painter.setPen(QPen(QColor(0x25, 0x74, 0x7d), 1.7));
        painter.drawLine(*origin, *uEnd);
        painter.setPen(QPen(QColor(0x8b, 0x5a, 0x2b), 1.7));
        painter.drawLine(*origin, *vEnd);
    }
    if (plane.label.isEmpty() || polygon.isEmpty()) {
        return;
    }
    // 名前は左上の角へ。どの面に描いているのかを、数えずに読めるようにする。
    painter.setPen(QPen(edge, 1.0));
    painter.drawText(polygon[3] + QPointF(4.0, 14.0),
        plane.active ? plane.label + QStringLiteral("(作業中)") : plane.label);
}

void V2Viewport::SetWorkPlaneViews(std::vector<WorkPlaneView> planes)
{
    workPlaneViews_ = std::move(planes);
    update();
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
} // namespace

double SemanticInkDistanceGate() noexcept
{
    // 64 は「明るさを1段変えただけ」では届かない幅である。
    // これより近い2色は、線が重なったところで見分けられなかった。
    return 64.0;
}

bool SemanticInksAreDistinct(const QColor& first, const QColor& second)
{
    const double dr = first.red() - second.red();
    const double dg = first.green() - second.green();
    const double db = first.blue() - second.blue();
    return std::sqrt(dr * dr + dg * dg + db * db) >= SemanticInkDistanceGate();
}

QColor V2Viewport::SemanticColor(kachakacha::v2::app::SemanticState state) const
{
    switch (state) {
    case SemanticState::Default:  return palette_.wire;
    case SemanticState::Hover:    return palette_.hover;
    case SemanticState::Selected: return palette_.selected;
    case SemanticState::Snap:     return palette_.snap;
    case SemanticState::Preview:  return palette_.preview;
    }
    return palette_.wire;
}

double V2Viewport::SemanticWidthPx(kachakacha::v2::app::SemanticState state) const
{
    switch (state) {
    case SemanticState::Default:
        return display_.wireWidthPx;
    case SemanticState::Hover:
        // 色だけでなく太さも変える。色が読めない画面でも、当たっている線が分かる。
        // ただし選択より必ず細くする。Hover は選択と違う軽い強調である(§3 規則1)。
        // 線の太さの設定がいくつでも、通常 < Hover < 選択 の順を崩さない。
        return display_.wireWidthPx + 0.6;
    case SemanticState::Selected:
        // 細い線の設定でも選択は必ず太くする。太さが同じだと色だけが頼りになる。
        return std::max(3.2, display_.wireWidthPx + 1.2);
    case SemanticState::Snap:
        return 1.8;
    case SemanticState::Preview:
        // 確定した線より必ず細くする。途中経過は確定済みより薄く見せる(§3 規則3)。
        // 下限を固定値にすると、細い線の設定(0.25px〜)で通常より太くなる。
        return std::max(display_.wireWidthPx * 0.5, display_.wireWidthPx - 0.6);
    }
    return display_.wireWidthPx;
}

QPen V2Viewport::PreviewPen() const
{
    // 確定した線より細く、半透明の破線にする(§3 規則3)。
    // 太さを固定値にすると、細い線の設定で確定した線より太くなる。
    QColor ink = SemanticColor(SemanticState::Preview);
    ink.setAlphaF(0.75);
    return QPen(ink, SemanticWidthPx(SemanticState::Preview), Qt::DashLine, Qt::RoundCap,
        Qt::RoundJoin);
}

kachakacha::v2::app::SemanticState V2Viewport::CurveStateOf(
    kachakacha::v2::base::EntityId entityId,
    kachakacha::v2::base::SegmentId segmentId) const
{
    return kachakacha::v2::app::CurveSemanticState(selection_, entityId, segmentId,
        hoveredEntityId_, hoveredSegmentId_);
}

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
        // 「選択だけ」と、掴んで動かす影は **物体単位** で見る。
        // 線分1本を選んだだけで折れ線の残りが消えたり、片方だけ動いて見えたりすると、
        // 実際に動くもの(物体まるごと)と画面が食い違う。
        const bool entitySelected = kachakacha::v2::app::IsSelected(selection_,
            curve.entityId);
        if (display_.selectionOnly && !entitySelected) {
            continue;   // 「選択だけ」。選んでいないものは出さない(消してはいない)。
        }
        // 意味状態は core が決める(app/SemanticState)。
        // 選んだ線分だけが Selected になり、同じワイヤーの残りは通常表示のままになる。
        const SemanticState state = CurveStateOf(curve.entityId, curve.segmentId);
        const bool plain = state == SemanticState::Default;
        // 通常表示のときだけデータ種類(補助線)の色と太さを使う。
        // 状態が付いた線は状態の色にする(§3「見た目はデータ種類より状態を優先する」)。
        QColor color = plain && curve.construction ? palette_.construction
                                                   : SemanticColor(state);
        // 薄くするかどうかの判断は core にある(app/PlaneFocus)。
        // 掴めるかどうかと同じところから出さないと、薄いのに掴める、が起きる。
        if (kachakacha::v2::app::DimsOffPlaneCurve(dimming, true, entitySelected,
                kachakacha::v2::app::CurveLiesOnPlane(curve.segment, workPlane_))) {
            color.setAlphaF(color.alphaF() * 0.24);
        }
        // 太さと様式は表示設定(既定は V1 と同じ: 線 2.0 実線、補助線 1.7 破線)。
        // 細い実線は高解像度の画面で点線に見えることがある。
        const double width = plain && curve.construction ? display_.constructionWidthPx
                                                         : SemanticWidthPx(state);
        // 基準線は一点鎖線(V1 と同じ)。補助線は補助線の様式、ほかは線の様式。
        const Qt::PenStyle style = curve.datum
            ? Qt::DashDotLine
            : PenStyleOf(curve.construction ? display_.constructionStyle : display_.wireStyle);
        painter.setPen(QPen(color, width, style, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        if (entitySelected && bodyDrag_.active && bodyDrag_.moved) {
            // 掴んでいる間の行き先を出す。元の線はそのまま残して、両方見せる。
            // 出さないと、離すまでどこへ行くのか分からない。
            QPainterPath ghost;
            bool ghostStarted = false;
            AppendCurve(ghost, kachakacha::v2::geometry::TranslateCurve(curve.segment,
                bodyDrag_.delta), ghostStarted);
            if (ghostStarted) {
                // 行き先は確定した形ではない。Preview のペンで出す(§3 規則3)。
                painter.setPen(PreviewPen());
                painter.drawPath(ghost);
            }
        }
    }
    for (const auto& point : scene.points) {
        const auto screen = ToScreen(point.position);
        if (!screen.has_value()) {
            continue;
        }
        const SemanticState state = kachakacha::v2::app::PointSemanticState(selection_,
            point.entityId, hoveredEntityId_);
        if (display_.selectionOnly && state != SemanticState::Selected) {
            continue;
        }
        // 点も状態で描き分ける。選んでも見た目が変わらないと、
        // 押して選べたのかどうかが画面から読めない。
        const bool plain = state == SemanticState::Default;
        const QColor ink = plain ? palette_.point : SemanticColor(state);
        // 大きさは画面上の px で決める(§3 規則6)。寄っても引いても見失わない。
        const double half = plain ? 2.0 : 3.5;
        painter.setPen(QPen(ink, 1.0));
        painter.setBrush(ink);
        painter.drawRect(QRectF(screen->x() - half, screen->y() - half, half * 2.0,
            half * 2.0));
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
    // 帯はまだ確定した形ではない。ほかの途中経過と同じ Preview のペンで出す(§3 規則3)。
    painter.setPen(PreviewPen());
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
            painter.setPen(PreviewPen());
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(ghost);
        }
    }
    painter.setPen(QPen(SemanticColor(SemanticState::Selected), 1.0));
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
    // 引いている途中の線。V1 と同じく破線(確定した線は実線)。
    // 破線・色・半透明で分ける。確定した線より薄く見せる(§3 規則3)。
    painter.setPen(PreviewPen());
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
    const double size = 6.0;
    const SnapKind kind = hover_.snap->kind;
    const auto glyph = [&] {
        switch (kind) {
        case SnapKind::Endpoint:
        case SnapKind::DrawingPoint:
            painter.drawRect(QRectF(screen->x() - size, screen->y() - size,
                size * 2, size * 2));
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
    };
    // 白フチを下に敷いてから色を重ねる(V1 と同じ)。
    // 1本線だけだと、線の上に来たときに記号が背景へ溶けて読めない。
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255, 225), 4.0));
    glyph();
    painter.setPen(QPen(SemanticColor(SemanticState::Snap),
        SemanticWidthPx(SemanticState::Snap)));
    glyph();
    // 何に吸着したかを、記号だけでなく言葉でも出す(PRD-061)。
    painter.setPen(QPen(palette_.text, 1.0));
    painter.drawText(QPointF(screen->x() + size + 4.0, screen->y() - size),
        QString::fromUtf8(std::string(SnapKindLabelJa(kind)).c_str()));
}

void V2Viewport::DrawBoxSelect(QPainter& painter) const
{
    const auto kind = BoxSelectKind();
    if (!kind.has_value()) {
        // まだ矩形として扱わない大きさ。枠を出すと「効いている」と誤解させる。
        return;
    }
    // 取り方を線の形で見せる。完全包含は実線、交差は破線。
    // 色だけで分けると、どちらが厳しい取り方なのかを覚えていないと読めない。
    const bool contained = *kind == kachakacha::v2::app::BoxSelectionKind::Contained;
    const QColor ink = SemanticColor(SemanticState::Selected);
    painter.setPen(QPen(ink, 1.4, contained ? Qt::SolidLine : Qt::DashLine));
    QColor fill = ink;
    fill.setAlphaF(0.10);
    painter.setBrush(fill);
    painter.drawRect(BoxSelectRect());
    painter.setBrush(Qt::NoBrush);
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


void V2Viewport::SetShapeViews(std::vector<ShapeView> shapes)
{
    shapeViews_ = std::move(shapes);
    // 当たり判定へ渡す網を作り直す。カーソルが動くたびに三角形を写さないため、
    // ここで1度だけ並べておく。並びは shapeViews_ と同じ(索引で引き当てる)。
    pickMeshes_.clear();
    pickMeshes_.reserve(shapeViews_.size());
    for (const ShapeView& shape : shapeViews_) {
        pickMeshes_.push_back(shape.mesh);
    }
    // 形が入れ替わったので、覚えていた候補は捨てる。
    ForgetPickCycle();
    update();
}

int V2Viewport::ShapeTriangleCount() const noexcept
{
    int count = 0;
    for (const ShapeView& shape : shapeViews_) {
        count += static_cast<int>(shape.mesh.triangles.size());
    }
    return count;
}

//! 立体と面を描く(棚卸し A-1)。
//!
//! QPainter には深度バッファが無いので、**形をまたいで** 奥から手前へ塗る。
//! 形ごとに塗ると、2つの部品が食い込んでいるところで前後が入れ替わる。
void V2Viewport::DrawShapes(QPainter& painter) const
{
    if (shapeViews_.empty() || !display_.shapesVisible) {
        return;
    }
    for (const ShapeView& shape : shapeViews_) {
        DrawOneShape(painter, shape);
    }
}

void V2Viewport::DrawOneShape(QPainter& painter, const ShapeView& shape) const
{
    using kachakacha::v2::view::BackFacing;
    using kachakacha::v2::view::LambertShade;
    using kachakacha::v2::view::PainterOrder;
    using kachakacha::v2::view::StandardLightDirection;

    const bool selected = !shape.entityId.IsNil()
        && kachakacha::v2::app::IsSelected(selection_, shape.entityId);
    const bool hovered = !selected && !shape.entityId.IsNil()
        && shape.entityId == hoveredEntityId_;
    const Vector3 forward = kachakacha::v2::view::ForwardOf(orientation_);
    const Vector3 light = StandardLightDirection();
    // 選んでいるものはテーマの選択色、面は青緑、立体は灰。
    // 選択色を固定で書くと、テーマを替えたときに稜線(テーマの色)と塗りが食い違う。
    const QColor base = selected ? SemanticColor(SemanticState::Selected)
        : (shape.surface ? QColor(0x45, 0x84, 0x8e) : QColor(0x9a, 0xa5, 0xad));
    // 面は薄く。奥の線が透けて見えないと、面の裏に何があるか分からない。
    const int alpha = shape.surface ? (selected ? 150 : 105) : (selected ? 225 : 200);

    painter.setPen(Qt::NoPen);
    const auto order = PainterOrder(shape.mesh.triangles, forward);
    for (const std::size_t index : order) {
        const auto& triangle = shape.mesh.triangles[index];
        if (triangle.normal == Vector3{}) {
            continue;   // 潰れた三角形。塗っても線にしかならない。
        }
        // 閉じた立体では裏を飛ばす。開いた面は裏から見ることがあるので飛ばさない。
        if (shape.mesh.closed && BackFacing(triangle, forward)) {
            continue;
        }
        QPolygonF polygon;
        bool ok = true;
        for (const Vector3& point : triangle.points) {
            const auto screen = ToScreen(point);
            if (!screen.has_value()) {
                ok = false;
                break;
            }
            polygon << *screen;
        }
        if (!ok) {
            continue;
        }
        const double shade = LambertShade(triangle.normal, light);
        QColor color = base.lighter(static_cast<int>(60.0 + shade * 80.0));
        color.setAlpha(alpha);
        painter.setBrush(color);
        painter.drawPolygon(polygon);
    }
    // 稜線を上から重ねる。三角形の網だけだと継ぎ目が全部見えて形が読めない。
    // 選択と Hover の色はテーマが持つものを使う。ここで固定の色を書くと、
    // Windows 95 テーマへ替えたときに2つの状態が同じ色になる。
    QColor edge(0x3a, 0x44, 0x4a);
    if (selected) {
        // 塗りより濃くする。同じ色だと稜線が塗りに溶けて、形の境目が読めない。
        edge = SemanticColor(SemanticState::Selected).darker(125);
    } else if (hovered) {
        edge = SemanticColor(SemanticState::Hover);
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(edge, selected || hovered ? 2.0 : 1.1, Qt::SolidLine, Qt::RoundCap,
        Qt::RoundJoin));
    for (const auto& line : shape.mesh.edges) {
        QPolygonF path;
        bool ok = true;
        for (const Vector3& point : line) {
            const auto screen = ToScreen(point);
            if (!screen.has_value()) {
                ok = false;
                break;
            }
            path << *screen;
        }
        if (ok && path.size() >= 2) {
            painter.drawPolyline(path);
        }
    }
}
