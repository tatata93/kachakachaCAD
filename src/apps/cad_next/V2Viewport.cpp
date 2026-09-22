#include "V2Viewport.h"

#include "kachakacha/app/CursorInput.h"
#include "kachakacha/app/GrabToMove.h"
#include "kachakacha/app/ToolTargeting.h"

#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/Units.h"
#include "kachakacha/modeling/ToolController.h"

#include <QColor>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QResizeEvent>
#include <QString>
#include <QWheelEvent>
#include <QWidget>


#include <algorithm>
#include <array>
#include <cmath>
#include <string>
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
    // 意味状態は16色から **色相の違う** ものを割り当てる。濃紺と青(旧 selected と
    // preview)のような明るさ違いは、白地では同じ色にしか見えなかった。
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
    palette.hover = QColor(0x00, 0x80, 0x80);
    palette.preview = QColor(0x80, 0x00, 0x80);
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
    // 場面が入れ替わったら、出している一時表示も捨てる。
    // 呼び口ごとに後始末を書くと、必ずどれかが抜ける。ここ1本にする。
    //
    // 綱は会員が持つ。この画面が先に消えれば綱も切れ、もう呼ばれない。
    // 素の関数を預けると、窓を閉じたあとの SetScene で消えた this を呼ぶ。
    sceneChanged_ = session_->OnSceneChanged([this] { OnSceneReplaced(); });
    // 吸着のあとに点を寄せる手立てを据える(Shift の拘束と直角スナップ)。
    InstallPointAdjuster();
}

//! 場面が入れ替わった。文書を開く、Undo/Redo、作業平面やグリッドの変更で通る。
//!
//! ここで **取り直さない** 。取り直すと、菜単から替えたときのように
//! ポインタが画面の外にあるときでも、そこに何か出ることになる。
//! 消しておいて、次にポインタが動いたときに出し直す。
void V2Viewport::OnSceneReplaced()
{
    // 条件を付けない。場面が替わったら前の場面の一時表示は全部捨てる、という
    // 決めごとにする。「リングが出ているときだけ」にすると、位置や案内文だけが
    // 残る場合に前の場面のものが生き延びる。
    DiscardHoverState();
    RebuildProfileRegions();
    update();
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
        viewMessage_ = oriented.FirstSummaryJa();
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

void V2Viewport::SetStatusCallback(std::function<void(const std::string&)> callback)
{
    statusCallback_ = std::move(callback);
}

void V2Viewport::SetDocumentChangedCallback(std::function<void()> callback)
{
    documentChangedCallback_ = std::move(callback);
}

void V2Viewport::SetTransformCallback(
    std::function<void(const kachakacha::v2::modeling::TransformPlan&)> callback)
{
    transform_ = std::move(callback);
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

void V2Viewport::SetDisplaySettings(const kachakacha::v2::app::DisplaySettings& settings)
{
    // 見え方だけを変える。文書には何も書かない。
    display_ = settings;
    update();
}

QRectF V2Viewport::ViewCubeRect() const
{
    // キューブの場所は操作板が持っている。ここで別に計算しない。
    // 別に計算していたころ、操作板が画面へ入るように寄せられると
    // キューブだけ動かず、見えている場所と押せる場所がずれた。
    const auto layout = ViewGadgets();
    if (!(layout.cubeSizePx > 0.0)) {
        return QRectF();
    }
    return QRectF(layout.cubeXPx, layout.cubeYPx, layout.cubeSizePx, layout.cubeSizePx);
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
        viewMessage_ = begun.FirstSummaryJa();
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
        viewMessage_ = rotated.FirstSummaryJa();
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
        viewMessage_ = rotated.FirstSummaryJa();
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
    // 枠は面の大きさに合わせる。52pxのままだと隣の面へはみ出して重なる。
    const double box = scale * 0.9;
    painter.drawText(QRectF(center.x() + cx * scale - box * 0.5,
                         center.y() - cy * scale - box * 0.5, box, box),
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
    // 掴める四角をそのまま座として描く。ここが当たり判定と同じ大きさである。
    // 指しているときは縁を光らせる。掴める物だと目で分かるようにする。
    QColor backing = palette_.background;
    backing.setAlpha(215);
    painter.setBrush(backing);
    const bool hot = cubeDrag_.active || cubeHoverZone_.has_value();
    painter.setPen(QPen(hot ? palette_.selected : palette_.gridMajor, hot ? 2.0 : 1.2));
    painter.drawRoundedRect(box, 4.0, 4.0);
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

void V2Viewport::SetAxisVisible(int axis, bool visible)
{
    if (axis < 0 || axis >= 3) {
        return;
    }
    axisVisible_[axis] = visible;
    update();
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
    // 立体と面を先に塗ってから線を描く。逆にすると、線が面の下に隠れる。
    // 線はこの道具の主役なので、必ず上に出す。
    DrawShapes(painter);
    DrawProfileRegions(painter);
    DrawDocument(painter);
    DrawGuideRows(painter);
    DrawPreview(painter);
    DrawSnap(painter);
    // 道具の下見(面を作る)は、文書の線の上・矢印の下に出す。
    DrawToolPreview(painter);
    // 押し出しの矢印は選択の印より上に出す。掴む相手だからである。
    DrawExtrudeHandle(painter);
    // 役割の札は一番上。線や矢印に隠れると読めない。
    DrawToolRoleLabels(painter);
    DrawBoxSelect(painter);
    DrawScaleBar(painter);
    DrawHud(painter);
    // 輪 → キューブ → ボタン の順で描く。
    // キューブを先に描くと、真横を向いた輪がキューブの上を横切って、
    // キューブが押せないように見える。押すときはキューブが勝つので、
    // 描く順も合わせる。
    DrawViewGadgets(painter);
    DrawViewCube(painter);
    DrawViewButtonsOnTop(painter);
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
            // 欄に出すのは「直前に置いた点から見た」ずれ。絶対位置ではない。
            const Vector3 anchor = session_->ConstraintAnchor();
            cursorDelta_ = cursorPanel_.onWorkPlane
                ? Vector3{workPlane_.CoordinateU(*point) - workPlane_.CoordinateU(anchor),
                      workPlane_.CoordinateV(*point) - workPlane_.CoordinateV(anchor), 0.0}
                : *point - anchor;
            const auto moved = kachakacha::v2::app::UpdateFromPointer(cursorPanel_,
                cursorDelta_);
            if (moved.HasValue()) {
                cursorPanel_ = moved.Value();
            }
        }
    }
    status_ = hover_.messageJa;
    hoveredProfileRegion_ = ProfileRegionAt(position);
    if (!SelectionHasPart() && PickShapeAt(position).has_value()) {
        hoveredProfileRegion_.reset();
    }
    if (hoveredProfileRegion_.has_value()) {
        status_ = ProfileRegionSelected(*hoveredProfileRegion_)
            ? "選択済みの輪郭領域です。クリックすると入力から外します。"
            : "閉じた輪郭の内側です。クリックすると入力へ追加します。";
    }
    if (statusCallback_) {
        statusCallback_(status_);
    }
    // カーソルの下の候補を覚える。覚えないと、押すまで「どれに当たるか」が分からない。
    // V1 は当たっている線を太く出していた。同じにする。
    // 重なっているときは1件目だけでなく全部を持つ。持たないと Tab で送れない。
    RefreshPickCycle(position);
    SyncHoverWithCandidate();
    // 掴めないものの上に来たら、カーソルでそう言う(§5.1 禁止対象)。
    RefreshForbiddenHover(position);
    RefreshCursorShape();
    update();
}

void V2Viewport::SetSelectionChangedCallback(std::function<void()> callback)
{
    selectionChangedCallback_ = std::move(callback);
}

void V2Viewport::SetPendingCommandCallbacks(std::function<void()> confirm,
    std::function<void()> cancel)
{
    confirmPending_ = std::move(confirm);
    cancelPending_ = std::move(cancel);
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
    // 文書が変わった。覚えていた候補は、もう無いものを指しているかもしれない。
    ForgetPickCycle();
    // 引きかけの矩形も捨てる。覚えている「押した時点の選択」には、
    // もう文書に無いものが入っているかもしれない。
    CancelBoxSelect();
    // 消えたものを選んだままにしない。無いものを選んでいることになる。
    SetSelection(kachakacha::v2::app::PruneSelection(selection_,
        session_->GetDocument().Snapshot()));
}

void V2Viewport::SelectAt(const QPointF& position, Qt::KeyboardModifiers modifiers)
{
    if (ToggleProfileRegionAt(position)) {
        return;
    }
    // 修飾キーの読み方は矩形選択と同じ一箇所(SelectionModeFor)から取る。
    // 二重に書くと、クリックと矩形で Ctrl の意味が食い違う。
    const auto mode = SelectionModeFor(modifiers);
    // 候補は Hover と同じ一箇所(cycle_)から取る。別に拾い直すと、
    // 画面に出している候補と、押して選ばれるものが食い違う。
    // 押した場所が Hover と同じなら、Tab で送った番号もそのまま残る。
    RefreshPickCycle(position);
    if ((modifiers & Qt::AltModifier) != 0) {
        // Alt は「もう1つ奥」。いま出している候補の次を選ぶ(§4.2)。
        // 候補は 点 → 線 → 手前の形 → 奥の形 の順なので、次が奥側になる。
        AdvanceCandidate(false);
    }
    auto picked = CurrentCandidate();
    // 選んだものは、そのまま押した先の候補として出しておく。
    SyncHoverWithCandidate();
    // 輪郭がもう入っているなら、拾った面は **相手の立体** を指している(§5)。
    // そのまま面として受けると「面と輪郭の両方」で止まり、理由が分からない。
    picked = AsSolidIfProfileTaken(picked);
    // 押すたび入れる/外す の最中は、押した当人を窓へ伝える(選択の差分では移し替えが読めない)。
    lastToolPick_ = (toolPickToggle_ && picked.has_value())
        ? std::optional<kachakacha::v2::base::EntityId>{picked->entityId}
        : std::nullopt;
    lastToolPickPoint_ = (toolPickToggle_ && picked.has_value())
        ? std::optional<kachakacha::v2::geometry::Vector3>{picked->hitPoint}
        : std::nullopt;
    // 道具が入力を待っている間は、役割が違うものを足す(§5)。
    // **Ctrl を知らなくても、立体と輪郭の両方を選べる。**
    SetSelection(kachakacha::v2::app::ApplySelection(selection_, picked,
        ModeForTogglePick(picked, ModeForToolPick(picked, mode))));
    ReportSelectionCount();
}

void V2Viewport::ReportSelectionCount()
{
    const std::size_t selectedItems = kachakacha::v2::app::SelectionItemCount(selection_);
    status_ = selectedItems == 0
        ? std::string("選んでいるものはありません。")
        : std::string("選んでいるもの: ") + std::to_string(selectedItems)
            + " 件";
    if (statusCallback_) {
        statusCallback_(status_);
    }
}

void V2Viewport::BeginPointPick(std::function<void(const PickedPoint&)> handler,
    const std::string& promptJa)
{
    pickHandler_ = std::move(handler);
    // 聞いていることを言う。黙って待つと、何も起きないように見える。
    status_ = promptJa;
    if (statusCallback_) {
        statusCallback_(status_);
    }
    update();
}

void V2Viewport::CancelPointPick()
{
    if (!pickHandler_) {
        return;
    }
    pickHandler_ = nullptr;
    status_ = "拾うのをやめました。";
    if (statusCallback_) {
        statusCallback_(status_);
    }
    update();
}

void V2Viewport::ClickAt(const QPointF& position)
{
    if (pickHandler_) {
        // 1回だけ拾う。拾ったら道具へ戻す。押しっぱなしにしない。
        PickedPoint picked;
        const auto onPlane = mapping_.UnprojectOntoPlane(
            ScreenPoint{position.x(), position.y()}, workPlane_.origin,
            workPlane_.normal);
        if (onPlane.has_value()) {
            picked.point = *onPlane;
        }
        // 絞りは Hover と同じものを使う。使わないと、カーソルが「掴めない」と
        // 言っている薄い線を、押すと拾えてしまう。手と目が食い違う。
        picked.curve = kachakacha::v2::app::PickCurve(session_->Scene(), mapping_,
            ScreenPoint{position.x(), position.y()},
            session_->GetDocument().Snapshot().settings.tolerance, PickFocusNow());
        auto handler = pickHandler_;
        pickHandler_ = nullptr;
        handler(picked);
        update();
        return;
    }
    if (session_->CurrentTool() == kachakacha::v2::modeling::DrawingTool::Measure) {
        // 測定は点を集めるだけ。形は作らない。吸着した位置と、吸着した線を覚える。
        const auto hovered = session_->Hover(ScreenPoint{position.x(), position.y()});
        if (!hovered.position.has_value()) {
            status_ = "その場所では点を取れません。";
            if (statusCallback_) {
                statusCallback_(status_);
            }
            return;
        }
        MeasurePick pick;
        pick.point = *hovered.position;
        if (hovered.snap.has_value()) {
            pick.entityId = hovered.snap->entityId;
        }
        measurePicks_.push_back(pick);
        if (measurePicksChanged_) {
            measurePicksChanged_();
        }
        update();
        return;
    }
    // 面取り/丸めの道具は点を置かない。押した線を A・B として拾う(もう一度押すと外れる)。
    // 判断は core(ClickPicksPairMember)にある。
    if (kachakacha::v2::app::ClickPicksPairMember(session_->CurrentTool())) {
        const bool wasToggle = toolPickToggle_;
        toolPickToggle_ = true;
        SelectAt(position, Qt::NoModifier);
        toolPickToggle_ = wasToggle;
        return;
    }
    // 動かす道具(移動・複製・鏡映・回転)は、相手が決まっていないと点に意味がない。
    // V2 は2点を押した **後** に「先に動かす線を選んでください」と断っていた。
    // 1回目の押しで相手を選ぶ(オーナー指摘 2026-09-11)。判断は core にある。
    if (kachakacha::v2::app::ClickPicksTarget(session_->CurrentTool(),
            !selection_.entityIds.empty(),
            session_->PlacedPointCount())) {
        // ここも Hover と同じ絞りで拾う。動かす相手も、
        // 画面で掴めると見えているものだけにする。
        const auto picked = kachakacha::v2::app::PickEntity(session_->Scene(), mapping_,
            ScreenPoint{position.x(), position.y()},
            session_->GetDocument().Snapshot().settings.tolerance, PickFocusNow());
        if (!picked.has_value()) {
            status_ = "動かすものを押してください。";
            if (statusCallback_) {
                statusCallback_(status_);
            }
            return;
        }
        SetSelection(kachakacha::v2::app::ApplySelection(selection_, picked,
            kachakacha::v2::app::SelectionMode::Replace));
        status_ = "動かすものを選びました。次に、動かす元の点を押してください。";
        if (statusCallback_) {
            statusCallback_(status_);
        }
        update();
        return;
    }
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
    if (result.transform.has_value() && transform_) {
        transform_(*result.transform);
    }
    SyncCursorInputWithTool(result.placedPoint, result.committed);
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
    if (result.transform.has_value() && transform_) {
        transform_(*result.transform);
    }
    // 締められたら点は残らない。途中経過を残すと確定した線の上に破線が重なる。
    // 締められなかったとき(点の数が決まった道具など)は点が残り、まだ引いている途中である。
    if (session_->PlacedPointCount() == 0) {
        hover_.preview.clear();
    }
    SyncCursorInputWithTool(false, result.committed);
    update();
}

void V2Viewport::CancelTool()
{
    if (pickHandler_) {
        CancelPointPick();
        return;
    }
    session_->CancelTool();
    // core は持ち越しを捨てている(DrawingSession::CancelTool)。画面も同じ所まで戻す。
    // preview だけ消すと、取り消したのに前のリングと候補送りが残り、
    // 診断情報が session の中身と食い違う。
    DiscardHoverState();
    RefreshHoverInPlace();
    status_ = "取り消しました。";
    if (statusCallback_) {
        statusCallback_(status_);
    }
    update();
}

void V2Viewport::wheelEvent(QWheelEvent* event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (std::abs(steps) < 1.0e-9) {
        return;
    }
    SetVisibleWidthMm(visibleWidthMm_ * std::pow(0.85, steps));
}

void V2Viewport::keyReleaseEvent(QKeyEvent* event)
{
    // S と Shift は「押している間だけ」効く。離したら元へ戻す。
    SetAxisConstraintByKey((event->modifiers() & Qt::ShiftModifier) != 0);
    if (event->key() == Qt::Key_S) {
        // 押しっぱなしの自動反復でも離した知らせが来る。まだ押しているので解除しない。
        if (!event->isAutoRepeat()) {
            SetSnapSuppressedByKey(false);
        }
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

bool V2Viewport::event(QEvent* event)
{
    if (cursorPanel_.active && event->type() == QEvent::ShortcutOverride) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

bool V2Viewport::EditCursorFieldForKey(int key)
{
    if (!cursorPanel_.active || (key != Qt::Key_Backspace && key != Qt::Key_Delete)) {
        return false;
    }
    const std::size_t at = cursorPanel_.focusedIndex;
    if (at < cursorPanel_.states.size()) {
        QString edited = QString::fromStdString(cursorPanel_.states[at].text);
        if (key == Qt::Key_Delete) {
            edited.clear();
        } else if (!edited.isEmpty()) {
            edited.chop(1);
        }
        (void)TypeIntoCursorField(edited);
    }
    return true;
}

void V2Viewport::keyPressEvent(QKeyEvent* event)
{
    // S で吸着を止め、Shift で水平・垂直へ寄せる。
    // 押しっぱなしのあいだ効くので、押した瞬間と離した瞬間の両方で見る。
    SetAxisConstraintByKey((event->modifiers() & Qt::ShiftModifier) != 0);
    if (event->key() == Qt::Key_S) {
        SetSnapSuppressedByKey(true);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (extrudeHandle_.shown) {
            // 押し出しの下見が出ているなら、まずそれをやめる。
            // 手をつけている一時の操作を、いちばん先に片付ける。
            if (cancelExtrude_) {
                cancelExtrude_();
            }
            return;
        }
        if (boxSelect_.active) {
            // 引いている途中の矩形が先。Esc は「やりかけを1つ」やめる。
            // これは押している間だけの手つきで、文書へ入りかけた入力ではないので、
            // core の EscapeAction の段には置かない。
            CancelBoxSelect();
            return;
        }
        // 構えている命令があれば、まずそれを解く。やりかけの点より先に、
        // 「待っているもの」をやめるのが素直である。
        if (cancelPending_) {
            cancelPending_();
        }
        // V1と同じ。やりかけを1つ取り消してから、選択道具へ戻り、選択も解除する。
        // 何をするかは core(app/EscapeAction)が決める。
        (void)PressEscape();
        return;
    }
    if (cursorPanel_.active
        && (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab)) {
        (void)FocusNextCursorField(event->key() == Qt::Key_Backtab
            || (event->modifiers() & Qt::ShiftModifier) != 0);
        return;
    }
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        // 数値入力中でなければ、Tab は重なった候補の送りになる(§4.2)。
        // 選択は変えない。変えるのはクリックだけである。
        if (CycleCandidate(event->key() == Qt::Key_Backtab
                || (event->modifiers() & Qt::ShiftModifier) != 0)) {
            event->accept();
            return;
        }
    }
    if (cursorPanel_.active
        && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        // 何も打っていない Enter は、点をいくつでも受ける道具(折れ線)を締める(右クリックと同じ)。
        // 欄が Enter を食べていたので、Enter でも棚の「確定 Enter」でも折れ線が締まらなかった
        // (PC 自己試験 HP-PF-03)。打った値があれば、これまでどおりその値で次の点を置く。
        if (!kachakacha::v2::app::AnyCursorFieldTouched(cursorPanel_)
            && kachakacha::v2::modeling::TakesAnyNumberOfPoints(session_->CurrentTool())) {
            FinishTool();
            return;
        }
        (void)PressEnterInCursorInput();
        return;
    }
    if (EditCursorFieldForKey(event->key())) { event->accept(); return; }
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
        if (extrudeHandle_.shown) {
            // 出ている下見を確定する。空白を押して確定、はしない。
            if (confirmExtrude_) {
                confirmExtrude_();
            }
            return;
        }
        // 構えている命令があれば「これで」と言う。命令は窓が持っているので、
        // 画面は伝えるだけにする。
        if (confirmPending_) {
            confirmPending_();
        }
        FinishTool();
        return;
    }
    QWidget::keyPressEvent(event);
}

void V2Viewport::resizeEvent(QResizeEvent* /*event*/)
{
    RebuildMapping();
}
