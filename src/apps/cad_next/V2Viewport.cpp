#include "V2Viewport.h"

#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/Units.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using kachakacha::v2::geometry::CurveKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::MakeOrthographicMapping;
using kachakacha::v2::geometry::ScreenMapping;
using kachakacha::v2::geometry::ScreenPoint;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::modeling::SnapKind;
using kachakacha::v2::modeling::SnapKindLabelJa;
using kachakacha::v2::modeling::WorkPlaneFrame;

const char* ViewDirectionNameJa(ViewDirection direction)
{
    switch (direction) {
    case ViewDirection::Top:       return "上から";
    case ViewDirection::Bottom:    return "下から";
    case ViewDirection::Front:     return "正面";
    case ViewDirection::Back:      return "背面";
    case ViewDirection::Left:      return "左";
    case ViewDirection::Right:     return "右";
    case ViewDirection::Isometric: return "等角";
    }
    return "不明";
}

ViewportPalette ViewportPalette::Dark()
{
    return ViewportPalette{};
}

ViewportPalette ViewportPalette::Win95()
{
    // Windows 95 のアプリケーション作業領域は白。線は黒。
    ViewportPalette palette;
    palette.background = QColor(0xFF, 0xFF, 0xFF);
    palette.gridMinor = QColor(0xE4, 0xE4, 0xE4);
    palette.gridMajor = QColor(0xC0, 0xC0, 0xC0);
    palette.axisX = QColor(0x80, 0x00, 0x00);
    palette.axisY = QColor(0x00, 0x80, 0x00);
    palette.axisZ = QColor(0x00, 0x00, 0x80);
    palette.wire = QColor(0x00, 0x00, 0x00);
    palette.construction = QColor(0x80, 0x80, 0x80);
    palette.selected = QColor(0x00, 0x00, 0x80);
    palette.preview = QColor(0x00, 0x00, 0xC0);
    palette.point = QColor(0x00, 0x00, 0x00);
    palette.snap = QColor(0xC0, 0x00, 0x00);
    palette.workPlane = QColor(0x80, 0x80, 0xC0);
    palette.text = QColor(0x00, 0x00, 0x00);
    return palette;
}

namespace {

struct ViewFrame {
    Vector3 forward;
    Vector3 up;
};

[[nodiscard]] ViewFrame FrameFor(ViewDirection direction)
{
    switch (direction) {
    case ViewDirection::Top:    return {{0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}};
    case ViewDirection::Bottom: return {{0.0, 0.0, 1.0}, {0.0, -1.0, 0.0}};
    case ViewDirection::Front:  return {{0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    case ViewDirection::Back:   return {{0.0, -1.0, 0.0}, {0.0, 0.0, 1.0}};
    case ViewDirection::Left:   return {{1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
    case ViewDirection::Right:  return {{-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
    case ViewDirection::Isometric:
        break;
    }
    return {{-1.0, -1.0, -1.0}, {0.0, 0.0, 1.0}};
}

//! 円弧を QPainterPath へ描くときの、画面上での中心・半径・角度。
//! 平行投影なので、画面に対して正対している円弧は楕円ではなく円になる。
//! 正対していないときは、細かく分けて折れ線にせず、3次ベジェで近似する。
//! どちらも「折れ線で描いてしまう」ことは避ける。
constexpr int kBezierPerQuarterTurn = 1;

} // namespace

V2Viewport::V2Viewport(kachakacha::v2::app::DrawingSession& session, QWidget* parent)
    : QWidget(parent)
    , session_(&session)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    RebuildMapping();
}

void V2Viewport::SetPalette(const ViewportPalette& palette)
{
    palette_ = palette;
    update();
}

void V2Viewport::SetViewDirection(ViewDirection direction)
{
    direction_ = direction;
    RebuildMapping();
    update();
}

void V2Viewport::SetWorkPlane(const WorkPlaneFrame& plane)
{
    workPlane_ = plane;
    kachakacha::v2::modeling::SnapScene scene = session_->Scene();
    scene.workPlane.active = true;
    scene.workPlane.origin = plane.origin;
    scene.workPlane.normal = plane.normal;
    session_->SetScene(std::move(scene));
    update();
}

void V2Viewport::SetVisibleWidthMm(double value)
{
    visibleWidthMm_ = std::clamp(value, 0.05, 1.0e6);
    RebuildMapping();
    update();
}

void V2Viewport::SetViewCenter(const Vector3& center)
{
    center_ = center;
    RebuildMapping();
    update();
}

void V2Viewport::SetGridSpacingMm(double value)
{
    gridSpacingMm_ = std::max(value, 0.001);
    update();
}

void V2Viewport::SetStatusCallback(std::function<void(const std::string&)> callback)
{
    statusCallback_ = std::move(callback);
}

void V2Viewport::SetDocumentChangedCallback(std::function<void()> callback)
{
    documentChangedCallback_ = std::move(callback);
}

ScreenMapping V2Viewport::Mapping() const
{
    return mapping_;
}

void V2Viewport::RebuildMapping()
{
    const ViewFrame frame = FrameFor(direction_);
    mapping_ = MakeOrthographicMapping(center_, frame.forward, frame.up, visibleWidthMm_,
        std::max(1, width()), std::max(1, height()));
    session_->SetMapping(mapping_);
}

void V2Viewport::FitToDocument()
{
    const auto& scene = session_->Scene();
    bool any = false;
    Vector3 minimum{};
    Vector3 maximum{};
    const auto include = [&](const Vector3& point) {
        if (!any) {
            minimum = point;
            maximum = point;
            any = true;
            return;
        }
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    };
    for (const auto& curve : scene.curves) {
        for (const Vector3& point :
            kachakacha::v2::geometry::SampleChain({curve.segment}, 0.05)) {
            include(point);
        }
    }
    for (const auto& point : scene.points) {
        include(point.position);
    }
    if (!any) {
        center_ = Vector3{};
        visibleWidthMm_ = 200.0;
    } else {
        center_ = (minimum + maximum) * 0.5;
        const Vector3 span = maximum - minimum;
        const double largest = std::max({span.x, span.y, span.z, 1.0});
        visibleWidthMm_ = largest * 1.6;
    }
    RebuildMapping();
    update();
}

std::optional<QPointF> V2Viewport::ToScreen(const Vector3& world) const
{
    const auto projected = mapping_.Project(world);
    if (!projected.has_value()) {
        return std::nullopt;
    }
    return QPointF(projected->x, projected->y);
}

void V2Viewport::AppendLine(QPainterPath& path, const CurveSegment& segment,
    bool& started) const
{
    const auto start = ToScreen(segment.StartPoint());
    const auto end = ToScreen(segment.EndPoint());
    if (!start.has_value() || !end.has_value()) {
        return;
    }
    if (!started) {
        path.moveTo(*start);
        started = true;
    } else if ((path.currentPosition() - *start).manhattanLength() > 0.5) {
        path.moveTo(*start);
    }
    path.lineTo(*end);
}

void V2Viewport::AppendArc(QPainterPath& path, const CurveSegment& segment,
    bool& started) const
{
    // 円弧は折れ線にしない。90度ごとに3次ベジェで表す。
    // 平行投影では円が楕円になるので、始点・終点・接線を投影して繋ぐ。
    const double sweep = segment.Kind() == CurveKind::Circle
        ? 2.0 * kPi
        : segment.SweepAngleRad();
    const int pieces = std::max(1,
        static_cast<int>(std::ceil(std::abs(sweep) / (kPi / 2.0)))
            * kBezierPerQuarterTurn);
    const double step = sweep / pieces;
    const double alpha = (4.0 / 3.0) * std::tan(step / 4.0);
    const double startAngle = segment.Kind() == CurveKind::Circle
        ? 0.0
        : segment.StartAngleRad();
    const Vector3 center = segment.Center();
    const Vector3 reference = segment.ReferenceDirection();
    const Vector3 normal = segment.Normal();
    const Vector3 other = kachakacha::v2::geometry::Cross(normal, reference);
    const double radius = segment.Radius();
    const auto pointAt = [&](double angle) {
        return center + reference * (radius * std::cos(angle))
            + other * (radius * std::sin(angle));
    };
    const auto tangentAt = [&](double angle) {
        return reference * (-radius * std::sin(angle))
            + other * (radius * std::cos(angle));
    };
    for (int index = 0; index < pieces; ++index) {
        const double from = startAngle + step * index;
        const double to = from + step;
        const auto s0 = ToScreen(pointAt(from));
        const auto s1 = ToScreen(pointAt(from) + tangentAt(from) * alpha);
        const auto s2 = ToScreen(pointAt(to) - tangentAt(to) * alpha);
        const auto s3 = ToScreen(pointAt(to));
        if (!s0 || !s1 || !s2 || !s3) {
            continue;
        }
        if (!started || index == 0) {
            path.moveTo(*s0);
            started = true;
        }
        path.cubicTo(*s1, *s2, *s3);
    }
}

void V2Viewport::AppendBezier(QPainterPath& path, const CurveSegment& segment,
    bool& started) const
{
    const auto& control = segment.ControlPoints();
    if (control.size() != 4) {
        return;
    }
    const auto s0 = ToScreen(control[0]);
    const auto s1 = ToScreen(control[1]);
    const auto s2 = ToScreen(control[2]);
    const auto s3 = ToScreen(control[3]);
    if (!s0 || !s1 || !s2 || !s3) {
        return;
    }
    path.moveTo(*s0);
    started = true;
    path.cubicTo(*s1, *s2, *s3);
}

void V2Viewport::AppendSpline(QPainterPath& path, const CurveSegment& segment,
    bool& started) const
{
    // 3次B-splineは、区間ごとに3次ベジェへ直せる。折れ線にしない。
    const auto& control = segment.ControlPoints();
    if (control.size() < 4) {
        return;
    }
    for (std::size_t index = 0; index + 3 < control.size(); ++index) {
        const Vector3& a = control[index];
        const Vector3& b = control[index + 1];
        const Vector3& c = control[index + 2];
        const Vector3& d = control[index + 3];
        const auto s0 = ToScreen((a + b * 4.0 + c) * (1.0 / 6.0));
        const auto s1 = ToScreen((b * 2.0 + c) * (1.0 / 3.0));
        const auto s2 = ToScreen((b + c * 2.0) * (1.0 / 3.0));
        const auto s3 = ToScreen((b + c * 4.0 + d) * (1.0 / 6.0));
        if (!s0 || !s1 || !s2 || !s3) {
            continue;
        }
        if (!started || index == 0) {
            path.moveTo(*s0);
            started = true;
        }
        path.cubicTo(*s1, *s2, *s3);
    }
}

void V2Viewport::AppendCurve(QPainterPath& path, const CurveSegment& segment,
    bool& started) const
{
    switch (segment.Kind()) {
    case CurveKind::Line:
        AppendLine(path, segment, started);
        return;
    case CurveKind::Circle:
    case CurveKind::CircularArc:
        AppendArc(path, segment, started);
        return;
    case CurveKind::CubicBezier:
        AppendBezier(path, segment, started);
        return;
    case CurveKind::CubicBSpline:
        AppendSpline(path, segment, started);
        return;
    }
}

void V2Viewport::DrawGrid(QPainter& painter) const
{
    const auto& grid = session_->Scene().grid;
    if (!grid.visible) {
        return;
    }
    const double pixelsPerMm = mapping_.PixelsPerMillimeterAt(workPlane_.origin);
    if (!(pixelsPerMm > 0.0)) {
        return;
    }
    // 画面で6px を下回る間隔は出さない(geometry-contract §6.3)。
    const double majorPx = gridSpacingMm_ * pixelsPerMm;
    const int reach = static_cast<int>(
        std::ceil(visibleWidthMm_ / std::max(gridSpacingMm_, 1.0e-6)));
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
        drawSet(gridSpacingMm_ * 0.5, palette_.gridMinor);
        drawSet(gridSpacingMm_, palette_.gridMajor);
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
    for (const auto& axis : axes) {
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

void V2Viewport::DrawDocument(QPainter& painter) const
{
    const auto& scene = session_->Scene();
    for (const auto& curve : scene.curves) {
        QPainterPath path;
        bool started = false;
        AppendCurve(path, curve.segment, started);
        if (!started) {
            continue;
        }
        const QColor color = curve.construction ? palette_.construction : palette_.wire;
        painter.setPen(QPen(color, curve.construction ? 1.0 : 1.6,
            curve.construction ? Qt::DashLine : Qt::SolidLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
    painter.setPen(QPen(palette_.point, 1.0));
    painter.setBrush(palette_.point);
    for (const auto& point : scene.points) {
        const auto screen = ToScreen(point.position);
        if (!screen.has_value()) {
            continue;
        }
        painter.drawRect(QRectF(screen->x() - 2.0, screen->y() - 2.0, 4.0, 4.0));
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
    painter.setPen(QPen(palette_.preview, 1.4, Qt::DashLine));
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

void V2Viewport::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), palette_.background);
    DrawGrid(painter);
    DrawWorkPlane(painter);
    DrawAxes(painter);
    DrawDocument(painter);
    DrawPreview(painter);
    DrawSnap(painter);
    DrawScaleBar(painter);
}

void V2Viewport::HoverAt(const QPointF& position)
{
    hover_ = session_->Hover(ScreenPoint{position.x(), position.y()});
    status_ = hover_.messageJa;
    if (statusCallback_) {
        statusCallback_(status_);
    }
    update();
}

void V2Viewport::ClickAt(const QPointF& position)
{
    const auto result = session_->Click(ScreenPoint{position.x(), position.y()});
    if (!result.diagnostics.empty()) {
        status_ = result.diagnostics.front().summaryJa;
    } else if (result.committed) {
        status_ = result.commandLabel;
    }
    if (statusCallback_) {
        statusCallback_(status_);
    }
    if (result.committed && documentChangedCallback_) {
        documentChangedCallback_();
    }
    hover_ = session_->Hover(ScreenPoint{position.x(), position.y()});
    update();
}

void V2Viewport::FinishTool()
{
    const auto result = session_->FinishTool();
    if (!result.diagnostics.empty()) {
        status_ = result.diagnostics.front().summaryJa;
    } else if (result.committed) {
        status_ = result.commandLabel;
    }
    if (statusCallback_) {
        statusCallback_(status_);
    }
    if (result.committed && documentChangedCallback_) {
        documentChangedCallback_();
    }
    update();
}

void V2Viewport::CancelTool()
{
    session_->CancelTool();
    hover_.preview.clear();
    status_ = "取り消しました。";
    if (statusCallback_) {
        statusCallback_(status_);
    }
    update();
}

void V2Viewport::mouseMoveEvent(QMouseEvent* event)
{
    HoverAt(event->position());
}

void V2Viewport::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        FinishTool();
        return;
    }
    ClickAt(event->position());
}

void V2Viewport::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (std::abs(steps) < 1.0e-9) {
        return;
    }
    SetVisibleWidthMm(visibleWidthMm_ * std::pow(0.85, steps));
}

void V2Viewport::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        CancelTool();
        return;
    }
    if (event->key() == Qt::Key_Backspace) {
        if (session_->UndoLastPoint()) {
            status_ = "1点戻しました。";
            if (statusCallback_) {
                statusCallback_(status_);
            }
            update();
        }
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        FinishTool();
        return;
    }
    QWidget::keyPressEvent(event);
}

void V2Viewport::resizeEvent(QResizeEvent* /*event*/)
{
    RebuildMapping();
}
