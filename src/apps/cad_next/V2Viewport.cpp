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

#include <QPolygonF>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

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

//! 6面+等角は、ビューキューブの区画として持つ。姿勢は core が作る。
[[nodiscard]] kachakacha::v2::view::ViewCubeZone ZoneFor(ViewDirection direction)
{
    switch (direction) {
    case ViewDirection::Top:    return {0, 0, 1};
    case ViewDirection::Bottom: return {0, 0, -1};
    case ViewDirection::Front:  return {0, -1, 0};
    case ViewDirection::Back:   return {0, 1, 0};
    case ViewDirection::Left:   return {-1, 0, 0};
    case ViewDirection::Right:  return {1, 0, 0};
    case ViewDirection::Isometric:
        break;
    }
    return {1, 1, 1};
}

//! キューブの大きさと余白(px)。
constexpr double kViewCubeSizePx = 88.0;
constexpr double kViewCubeMarginPx = 10.0;
//! これ以上動いたらクリックではなくドラッグとみなす。
constexpr double kViewCubeDragThresholdPx = 3.0;
//! 面と辺の境目の帯。0.28 なら中央 44% が面になる。
constexpr double kViewCubeEdgeBandRatio = 0.28;

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
    SetViewDirection(direction_);
}

void V2Viewport::SetPalette(const ViewportPalette& palette)
{
    palette_ = palette;
    update();
}

void V2Viewport::SetViewDirection(ViewDirection direction)
{
    direction_ = direction;
    const auto oriented = kachakacha::v2::view::OrientationForZone(ZoneFor(direction));
    if (!oriented.HasValue()) {
        viewMessage_ = oriented.Diagnostics().front().summaryJa;
        return;
    }
    SetOrientation(oriented.Value());
}

void V2Viewport::SetOrientation(const kachakacha::v2::view::Quaternion& orientation)
{
    if (!orientation.IsFinite() || orientation.Norm() <= 0.0) {
        viewMessage_ = "視点の値に数値でないものが入っています。";
        return;
    }
    orientation_ = kachakacha::v2::view::Normalized(orientation);
    RebuildMapping();
    update();
}

void V2Viewport::SetSelectionFrame(
    const std::optional<kachakacha::v2::view::Quaternion>& frame)
{
    selectionFrame_ = frame;
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
    mapping_ = MakeOrthographicMapping(center_, kachakacha::v2::view::ForwardOf(orientation_),
        kachakacha::v2::view::UpOf(orientation_), visibleWidthMm_, std::max(1, width()),
        std::max(1, height()));
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
        const bool selected = kachakacha::v2::app::IsSelected(selection_, curve.entityId);
        const QColor color = selected
            ? palette_.selected
            : (curve.construction ? palette_.construction : palette_.wire);
        painter.setPen(QPen(color, selected ? 2.4 : (curve.construction ? 1.0 : 1.6),
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

QRectF V2Viewport::ViewCubeRect() const
{
    // V1(ADR 0023)と同じ置き方。中心は右端から84px、上から74px。
    // キューブは一辺2(-1..+1)を kNavigatorScalePx で写すので、見た目の一辺はその2倍。
    const double scale = kachakacha::v2::view::kNavigatorScalePx;
    const double size = scale * 2.0;
    const double centerX = static_cast<double>(width()) - 84.0;
    const double centerY = 74.0;
    return QRectF(centerX - size * 0.5, centerY - size * 0.5, size, size);
}

std::optional<kachakacha::v2::view::ViewCubeZone> V2Viewport::ViewCubeZoneAtScreen(
    const QPointF& position) const
{
    const QRectF box = ViewCubeRect();
    if (!box.contains(position)) {
        return std::nullopt;
    }
    // キューブの中では、画面の右がカメラの右、画面の上がカメラの上になる。
    // 立方体は一辺2(-1..+1)なので、外接球の半径 sqrt(3) が入る大きさで写す。
    const double scale = box.width() * 0.5 / 1.7320508075688772;
    const QPointF center = box.center();
    const double sx = (position.x() - center.x()) / scale;
    const double sy = (center.y() - position.y()) / scale;
    const Vector3 right = kachakacha::v2::view::RightOf(orientation_);
    const Vector3 up = kachakacha::v2::view::UpOf(orientation_);
    const Vector3 forward = kachakacha::v2::view::ForwardOf(orientation_);
    const Vector3 origin = right * sx + up * sy + forward * -4.0;

    // 視線と立方体の交わりを、面ごとの区間で求める(スラブ法)。
    const std::array<double, 3> start{origin.x, origin.y, origin.z};
    const std::array<double, 3> step{forward.x, forward.y, forward.z};
    double enter = -1.0e30;
    double leave = 1.0e30;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (std::abs(step[axis]) < 1.0e-12) {
            if (start[axis] < -1.0 || start[axis] > 1.0) {
                return std::nullopt;
            }
            continue;
        }
        double entryT = (-1.0 - start[axis]) / step[axis];
        double exitT = (1.0 - start[axis]) / step[axis];
        if (entryT > exitT) {
            std::swap(entryT, exitT);
        }
        enter = std::max(enter, entryT);
        leave = std::min(leave, exitT);
    }
    if (enter > leave) {
        return std::nullopt;
    }
    const Vector3 hit = origin + forward * enter;
    const auto zone = kachakacha::v2::view::ViewCubeZoneAt(hit, kViewCubeEdgeBandRatio);
    if (!zone.HasValue()) {
        return std::nullopt;
    }
    return zone.Value();
}

bool V2Viewport::PressViewCube(const QPointF& position)
{
    if (!ViewCubeRect().contains(position)) {
        return false;
    }
    const auto begun = kachakacha::v2::view::BeginViewCubeDrag(orientation_);
    if (!begun.HasValue()) {
        viewMessage_ = begun.Diagnostics().front().summaryJa;
        return false;
    }
    cubeDrag_ = begun.Value();
    cubePressPosition_ = position;
    cubeMoved_ = false;
    return true;
}

void V2Viewport::DragViewCube(const QPointF& position)
{
    if (!cubeDrag_.active) {
        return;
    }
    const double dx = position.x() - cubePressPosition_.x();
    const double dy = position.y() - cubePressPosition_.y();
    if (std::hypot(dx, dy) > kViewCubeDragThresholdPx) {
        cubeMoved_ = true;
    }
    if (!cubeMoved_) {
        return;
    }
    const auto rotated = kachakacha::v2::view::UpdateViewCubeDrag(cubeDrag_, dx, dy,
        kachakacha::v2::view::kViewCubeDegreesPerPixel);
    if (!rotated.HasValue()) {
        viewMessage_ = rotated.Diagnostics().front().summaryJa;
        return;
    }
    // ドラッグ中は姿勢だけを入れ替える。吸着も慣性もしない。
    orientation_ = rotated.Value();
    RebuildMapping();
    update();
}

void V2Viewport::ReleaseViewCube(const QPointF& position)
{
    if (!cubeDrag_.active) {
        return;
    }
    if (!cubeMoved_) {
        // 動かしていないならクリック。ここだけが離散の向きを使う。
        const auto zone = ViewCubeZoneAtScreen(position);
        (void)kachakacha::v2::view::EndViewCubeDrag(cubeDrag_, orientation_);
        if (zone.has_value()) {
            const auto oriented = kachakacha::v2::view::OrientationForZone(*zone);
            if (oriented.HasValue()) {
                viewMessage_ = kachakacha::v2::view::ViewCubeZoneLabelJa(*zone) + "へ正対しました。";
                SetOrientation(oriented.Value());
            }
        }
        return;
    }
    const auto released = kachakacha::v2::view::EndViewCubeDrag(cubeDrag_, orientation_);
    if (released.HasValue()) {
        orientation_ = released.Value();
        RebuildMapping();
        update();
    }
}

bool V2Viewport::RotateByArrow(kachakacha::v2::view::RotationAxis axis,
    kachakacha::v2::view::RotationAxisMode mode,
    kachakacha::v2::view::AxisArrowModifier modifier, double dragPx)
{
    kachakacha::v2::view::AxisArrowRequest request;
    request.orientation = orientation_;
    request.mode = mode;
    request.axis = axis;
    request.modifier = modifier;
    request.hasSelectionFrame = selectionFrame_.has_value();
    if (selectionFrame_.has_value()) {
        request.selectionFrame = *selectionFrame_;
    }
    const auto rotated = kachakacha::v2::view::RotateByAxisArrowDrag(request, dragPx);
    if (!rotated.HasValue()) {
        viewMessage_ = rotated.Diagnostics().front().summaryJa;
        return false;
    }
    viewMessage_.clear();
    SetOrientation(rotated.Value());
    return true;
}

void V2Viewport::DrawViewCubeFace(QPainter& painter, int faceAxis, int faceSign,
    const QPointF& center, double scale) const
{
    const Vector3 right = kachakacha::v2::view::RightOf(orientation_);
    const Vector3 up = kachakacha::v2::view::UpOf(orientation_);
    const Vector3 forward = kachakacha::v2::view::ForwardOf(orientation_);

    Vector3 normal{};
    std::array<double*, 3> normalSlots{&normal.x, &normal.y, &normal.z};
    *normalSlots[static_cast<std::size_t>(faceAxis)] = static_cast<double>(faceSign);
    const double facing = normal.x * forward.x + normal.y * forward.y + normal.z * forward.z;
    if (facing > -0.02) {
        return; // 裏を向いている面は描かない。
    }

    const std::size_t axisA = static_cast<std::size_t>((faceAxis + 1) % 3);
    const std::size_t axisB = static_cast<std::size_t>((faceAxis + 2) % 3);
    QPolygonF polygon;
    const std::array<std::pair<double, double>, 4> corners{
        std::pair{-1.0, -1.0}, std::pair{1.0, -1.0}, std::pair{1.0, 1.0}, std::pair{-1.0, 1.0}};
    for (const auto& corner : corners) {
        Vector3 point = normal;
        std::array<double*, 3> pointSlots{&point.x, &point.y, &point.z};
        *pointSlots[axisA] = corner.first;
        *pointSlots[axisB] = corner.second;
        const double sx = point.x * right.x + point.y * right.y + point.z * right.z;
        const double sy = point.x * up.x + point.y * up.y + point.z * up.z;
        polygon << QPointF(center.x() + sx * scale, center.y() - sy * scale);
    }
    QColor fill = palette_.workPlane;
    fill.setAlpha(static_cast<int>(90 + 110 * std::min(1.0, -facing)));
    painter.setBrush(fill);
    painter.setPen(QPen(palette_.text, 1.0));
    painter.drawPolygon(polygon);

    kachakacha::v2::view::ViewCubeZone zone{0, 0, 0};
    std::array<int*, 3> zoneSlots{&zone.x, &zone.y, &zone.z};
    *zoneSlots[static_cast<std::size_t>(faceAxis)] = faceSign;
    const double cx = normal.x * right.x + normal.y * right.y + normal.z * right.z;
    const double cy = normal.x * up.x + normal.y * up.y + normal.z * up.z;
    painter.setPen(QPen(palette_.text, 1.0));
    painter.drawText(QRectF(center.x() + cx * scale - 26.0, center.y() - cy * scale - 8.0,
                         52.0, 16.0),
        Qt::AlignCenter,
        QString::fromStdString(kachakacha::v2::view::ViewCubeZoneLabelJa(zone)));
}

void V2Viewport::DrawViewCube(QPainter& painter) const
{
    const QRectF box = ViewCubeRect();
    if (box.width() < 24.0 || box.right() > width() || box.bottom() > height()) {
        return;
    }
    const double scale = box.width() * 0.5 / 1.7320508075688772;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor backing = palette_.background;
    backing.setAlpha(200);
    painter.setBrush(backing);
    painter.setPen(QPen(palette_.gridMajor, 1.0));
    painter.drawRect(box);
    for (int axis = 0; axis < 3; ++axis) {
        for (int sign = -1; sign <= 1; sign += 2) {
            DrawViewCubeFace(painter, axis, sign, box.center(), scale);
        }
    }
    if (cubeHoverZone_.has_value()) {
        painter.setPen(QPen(palette_.selected, 1.0));
        painter.drawText(QRectF(box.left(), box.bottom() - 14.0, box.width(), 14.0),
            Qt::AlignCenter,
            QString::fromStdString(
                kachakacha::v2::view::ViewCubeZoneLabelJa(*cubeHoverZone_)));
    }
    painter.restore();
}

namespace {

//! 入力列の見た目の大きさ。欄の数で高さが決まる。
constexpr double kCursorRowHeightPx = 18.0;
constexpr double kCursorPanelWidthPx = 210.0;
constexpr double kCursorPanelPaddingPx = 6.0;

} // namespace

bool V2Viewport::OpenCursorInput()
{
    const auto begun = kachakacha::v2::app::BeginCursorInput(session_->CurrentTool(),
        workPlane_.normal.LengthSquared() > 0.0);
    if (!begun.HasValue()) {
        viewMessage_ = begun.Diagnostics().front().summaryJa;
        return false;
    }
    cursorPanel_ = begun.Value();
    cursorAnchor_ = Vector3{};
    update();
    return true;
}

void V2Viewport::CloseCursorInput()
{
    cursorPanel_ = kachakacha::v2::app::CancelCursorInput(cursorPanel_);
    update();
}

bool V2Viewport::FocusNextCursorField(bool backward)
{
    const auto moved = kachakacha::v2::app::FocusNextField(cursorPanel_, backward);
    if (!moved.HasValue()) {
        viewMessage_ = moved.Diagnostics().front().summaryJa;
        return false;
    }
    cursorPanel_ = moved.Value();
    update();
    return true;
}

bool V2Viewport::TypeIntoCursorField(const QString& text)
{
    const auto typed = kachakacha::v2::app::SetFieldText(cursorPanel_,
        cursorPanel_.focusedIndex, text.toStdString());
    if (!typed.HasValue()) {
        viewMessage_ = typed.Diagnostics().front().summaryJa;
        return false;
    }
    cursorPanel_ = typed.Value();
    update();
    return true;
}

bool V2Viewport::CommitCursorField()
{
    const auto committed = kachakacha::v2::app::CommitFocusedField(cursorPanel_,
        cursorAnchor_);
    if (!committed.HasValue()) {
        // 断られたら、その欄を赤くして理由を出す。入力列は閉じない。
        const std::size_t at = cursorPanel_.focusedIndex;
        if (at < cursorPanel_.states.size()) {
            cursorPanel_.states[at].error = true;
            cursorPanel_.states[at].messageJa =
                committed.Diagnostics().front().summaryJa;
        }
        viewMessage_ = committed.Diagnostics().front().summaryJa;
        if (statusCallback_) {
            statusCallback_(viewMessage_);
        }
        update();
        return false;
    }
    cursorPanel_ = committed.Value().panel;
    viewMessage_.clear();
    update();
    return true;
}

QRectF V2Viewport::CursorPanelRect() const
{
    const double rows = static_cast<double>(std::max<std::size_t>(1,
        cursorPanel_.fields.size()));
    const double panelHeight = kCursorPanelPaddingPx * 2.0 + kCursorRowHeightPx * rows;
    const auto placement = kachakacha::v2::app::PlaceCursorPanel(cursorPosition_.x(),
        cursorPosition_.y(), kCursorPanelWidthPx, panelHeight,
        static_cast<double>(std::max(1, width())),
        static_cast<double>(std::max(1, height())));
    return QRectF(placement.xPx, placement.yPx, kCursorPanelWidthPx, panelHeight);
}

void V2Viewport::DrawCursorInput(QPainter& painter) const
{
    if (!cursorPanel_.active || cursorPanel_.fields.empty()) {
        return;
    }
    const QRectF box = CursorPanelRect();
    painter.save();
    QColor backing = palette_.background;
    backing.setAlpha(230);
    painter.setBrush(backing);
    painter.setPen(QPen(palette_.gridMajor, 1.0));
    painter.drawRect(box);
    for (std::size_t index = 0; index < cursorPanel_.fields.size(); ++index) {
        const auto& field = cursorPanel_.fields[index];
        const auto& state = cursorPanel_.states[index];
        const double top = box.top() + kCursorPanelPaddingPx
            + kCursorRowHeightPx * static_cast<double>(index);
        const QRectF row(box.left() + kCursorPanelPaddingPx, top,
            box.width() - kCursorPanelPaddingPx * 2.0, kCursorRowHeightPx);
        if (index == cursorPanel_.focusedIndex) {
            QColor focus = palette_.selected;
            focus.setAlpha(60);
            painter.fillRect(row, focus);
        }
        // 赤表示は「その欄が合っていない」印。入力列は消さない。
        painter.setPen(QPen(state.error ? QColor(0xE0, 0x40, 0x40)
                                        : (state.locked ? palette_.selected : palette_.text),
            1.0));
        painter.drawText(row, Qt::AlignLeft | Qt::AlignVCenter,
            QString::fromStdString(field.labelJa));
        painter.drawText(row, Qt::AlignRight | Qt::AlignVCenter,
            QString::fromStdString(
                kachakacha::v2::app::FieldDisplayJa(field, state)));
    }
    painter.restore();
}

void V2Viewport::SetGuideTableRows(
    const std::vector<kachakacha::v2::modeling::GuideTableRowView>& rows)
{
    guideRows_ = rows;
    update();
}

void V2Viewport::DrawGuideRows(QPainter& painter) const
{
    if (guideRows_.empty()) {
        return;
    }
    painter.save();
    for (const auto& row : guideRows_) {
        const QColor color(row.color.red, row.color.green, row.color.blue);
        const auto from = ToScreen(row.startPoint);
        const auto to = ToScreen(row.endPoint);
        if (!from.has_value() || !to.has_value()) {
            continue;
        }
        painter.setPen(QPen(color, 2.0));
        painter.setBrush(color);
        painter.drawLine(*from, *to);
        // 端点。始点は塗り、終点は矢印にする。どちらが先かが目で分かる。
        painter.drawEllipse(*from, 3.0, 3.0);
        const double dx = to->x() - from->x();
        const double dy = to->y() - from->y();
        const double length = std::hypot(dx, dy);
        if (length < 1.0e-6) {
            continue;
        }
        const double ux = dx / length;
        const double uy = dy / length;
        const double head = 9.0;
        QPolygonF arrow;
        arrow << *to
              << QPointF(to->x() - ux * head - uy * head * 0.45,
                     to->y() - uy * head + ux * head * 0.45)
              << QPointF(to->x() - ux * head + uy * head * 0.45,
                     to->y() - uy * head - ux * head * 0.45);
        painter.drawPolygon(arrow);
        // 行の名前を線の真ん中へ。表のどの行かがすぐ分かる。
        painter.drawText(QPointF((from->x() + to->x()) * 0.5 + 6.0,
                             (from->y() + to->y()) * 0.5 - 4.0),
            QString::fromStdString(row.roleLabelJa) + QString::number(row.number));
    }
    painter.restore();
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
    DrawGuideRows(painter);
    DrawPreview(painter);
    DrawSnap(painter);
    DrawScaleBar(painter);
    DrawViewCube(painter);
    DrawViewGadgets(painter);
    DrawCursorInput(painter);
}

void V2Viewport::HoverAt(const QPointF& position)
{
    cursorPosition_ = position;
    hover_ = session_->Hover(ScreenPoint{position.x(), position.y()});
    if (cursorPanel_.active) {
        // 入力中もマウスでプレビューは動く。ロックした欄だけは動かない。
        const auto point = mapping_.UnprojectOntoPlane(
            ScreenPoint{position.x(), position.y()}, workPlane_.origin, workPlane_.normal);
        if (point.has_value()) {
            const Vector3 world = *point - workPlane_.origin;
            cursorAnchor_ = Vector3{workPlane_.CoordinateU(*point),
                workPlane_.CoordinateV(*point), 0.0};
            (void)world;
            const auto moved = kachakacha::v2::app::UpdateFromPointer(cursorPanel_,
                cursorAnchor_);
            if (moved.HasValue()) {
                cursorPanel_ = moved.Value();
            }
        }
    }
    status_ = hover_.messageJa;
    if (statusCallback_) {
        statusCallback_(status_);
    }
    update();
}

void V2Viewport::SetSelectionChangedCallback(std::function<void()> callback)
{
    selectionChangedCallback_ = std::move(callback);
}

void V2Viewport::SetSelection(kachakacha::v2::app::SelectionSet selection)
{
    selection_ = std::move(selection);
    if (selectionChangedCallback_) {
        selectionChangedCallback_();
    }
    update();
}

void V2Viewport::PruneSelection()
{
    // 消えたものを選んだままにしない。無いものを選んでいることになる。
    SetSelection(kachakacha::v2::app::PruneSelection(selection_,
        session_->GetDocument().Snapshot()));
}

void V2Viewport::SelectAt(const QPointF& position, Qt::KeyboardModifiers modifiers)
{
    using kachakacha::v2::app::SelectionMode;
    SelectionMode mode = SelectionMode::Replace;
    if ((modifiers & Qt::ShiftModifier) != 0) {
        mode = SelectionMode::Add;
    } else if ((modifiers & Qt::ControlModifier) != 0) {
        mode = SelectionMode::Toggle;
    } else if ((modifiers & Qt::AltModifier) != 0) {
        mode = SelectionMode::Subtract;
    }
    const auto picked = kachakacha::v2::app::PickCurve(session_->Scene(), mapping_,
        ScreenPoint{position.x(), position.y()},
        session_->GetDocument().Snapshot().settings.tolerance);
    SetSelection(kachakacha::v2::app::ApplySelection(selection_, picked, mode));
    status_ = selection_.entityIds.empty()
        ? std::string("選んでいるものはありません。")
        : std::string("選んでいるもの: ") + std::to_string(selection_.entityIds.size())
            + " 件";
    if (statusCallback_) {
        statusCallback_(status_);
    }
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
    if (gadgetDrag_.has_value()) {
        DragViewGadget(event->position());
        return;
    }
    if (cubeDrag_.active) {
        DragViewCube(event->position());
        return;
    }
    // 操作板の上に来たら光らせる。押せる場所が目で分かるようにする。
    const auto gadget = ViewGadgetAt(event->position());
    if (gadget.has_value() != gadgetHoverIndex_.has_value()
        || (gadget.has_value() && *gadget != *gadgetHoverIndex_)) {
        gadgetHoverIndex_ = gadget;
        update();
    }
    if (gadget.has_value()) {
        const auto layout = ViewGadgets();
        if (*gadget < layout.gadgets.size() && statusCallback_) {
            statusCallback_(kachakacha::v2::view::ViewGadgetTooltipJa(
                layout.gadgets[*gadget]));
        }
        return; // 操作板の上ではスナップを探さない。
    }
    const auto zone = ViewCubeZoneAtScreen(event->position());
    if (zone.has_value() != cubeHoverZone_.has_value()
        || (zone.has_value() && *zone != *cubeHoverZone_)) {
        cubeHoverZone_ = zone;
        update();
    }
    if (zone.has_value()) {
        return; // キューブの上ではスナップを探さない。
    }
    HoverAt(event->position());
}

void V2Viewport::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        FinishTool();
        return;
    }
    kachakacha::v2::view::AxisArrowModifier modifier =
        kachakacha::v2::view::AxisArrowModifier::None;
    if ((event->modifiers() & Qt::ShiftModifier) != 0) {
        modifier = kachakacha::v2::view::AxisArrowModifier::Fine;
    } else if ((event->modifiers() & Qt::ControlModifier) != 0) {
        modifier = kachakacha::v2::view::AxisArrowModifier::Coarse;
    }
    // V1(ADR 0023)と同じ順で見る。ボタン → キューブ → 輪。
    // 輪を先に見ると、キューブの面が輪の線に隠れて押せなくなる。
    if (PressViewButton(event->position(), modifier)) {
        return;
    }
    if (PressViewCube(event->position())) {
        return;
    }
    if (PressViewRing(event->position(), modifier)) {
        return;
    }
    if (session_->CurrentTool() == kachakacha::v2::modeling::DrawingTool::Select) {
        SelectAt(event->position(), event->modifiers());
        return;
    }
    ClickAt(event->position());
}

void V2Viewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (gadgetDrag_.has_value()) {
        ReleaseViewGadget(event->position());
        if (statusCallback_ && !viewMessage_.empty()) {
            statusCallback_(viewMessage_);
        }
        return;
    }
    if (cubeDrag_.active) {
        ReleaseViewCube(event->position());
        if (statusCallback_ && !viewMessage_.empty()) {
            statusCallback_(viewMessage_);
        }
        return;
    }
    QWidget::mouseReleaseEvent(event);
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
        // Esc は1回だけ。入力列を閉じて、道具も取り消す。別の処理を2段目に作らない。
        CloseCursorInput();
        CancelTool();
        return;
    }
    if (cursorPanel_.active
        && (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab)) {
        (void)FocusNextCursorField(event->key() == Qt::Key_Backtab
            || (event->modifiers() & Qt::ShiftModifier) != 0);
        return;
    }
    if (cursorPanel_.active
        && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        if (CommitCursorField()) {
            FinishTool();
        }
        return;
    }
    if (cursorPanel_.active && !event->text().isEmpty()) {
        const std::size_t at = cursorPanel_.focusedIndex;
        const QString grown = QString::fromStdString(cursorPanel_.states[at].text)
            + event->text();
        if (TypeIntoCursorField(grown)) {
            return;
        }
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
