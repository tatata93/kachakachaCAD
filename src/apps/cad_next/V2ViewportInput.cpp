//! マウスとキーの土台(V1同等、docs/v2/v1-input-parity.md)。
//!
//! ここに集めたのは、V1にあって V2 に無かったものである。
//!   - 中ボタン(と右ボタン)のドラッグで画面を移動
//!   - Shift+中ボタンで軌道回転
//!   - Esc は、やりかけを1つ取り消してから選択道具へ戻り、選択も解除する
//!   - 作図中の Ctrl で吸着を一時停止、Shift で水平・垂直・正方形へ固定
//!   - 掴めるかどうかが分かるカーソル
//!   - 重なった候補を Tab で送り、Alt+クリックで奥を選ぶ(ui-ux-integrated-spec §4.2)
//!
//! どれも「画面が無いと確かめられない」ものではない。
//! 判断は core(app/EscapeAction、modeling/DrawingConstraint)にある。
//! ここはそれを Qt へつなぐだけである。

#include "V2Viewport.h"
#include <QCursor>
#include <QPainter>
#include <QPen>
#include <QPixmap>

#include "kachakacha/app/ControlPointPick.h"
#include "kachakacha/app/GrabToMove.h"
#include "kachakacha/modeling/DrawingConstraint.h"
#include "kachakacha/modeling/MeshPick.h"

#include <QCursor>

#include <cmath>
#include <iterator>
#include <string>
#include <utility>

namespace {

//! 1px あたり何度回すか。ビューキューブと同じにする。
//! 別の値にすると、同じ手つきなのに回り方が変わる。
constexpr double kOrbitDegreesPerPixel = 0.5;

//! 候補の並びが前と同じか。同じなら Tab の番号を持ち越せる。
//!
//! 位置(距離)は比べない。カーソルが1px動くたびに距離は変わるので、
//! 距離まで見ると同じ場所でも毎回「別の並び」になってしまう。
[[nodiscard]] bool SameCandidateOrder(
    const std::vector<kachakacha::v2::app::PickCandidate>& before,
    const std::vector<kachakacha::v2::app::PickCandidate>& after)
{
    if (before.size() != after.size()) {
        return false;
    }
    for (std::size_t index = 0; index < before.size(); ++index) {
        if (before[index].entityId != after[index].entityId
            || before[index].segmentId != after[index].segmentId
            || before[index].kind != after[index].kind) {
            return false;
        }
    }
    return true;
}

//! 候補の種類を一言で。帯に「何を出しているか」を書くために使う。
[[nodiscard]] const char* CandidateKindNameJa(kachakacha::v2::app::SelectionElementKind kind)
{
    using kachakacha::v2::app::SelectionElementKind;
    switch (kind) {
    case SelectionElementKind::Object:       return "形";
    case SelectionElementKind::Vertex:       return "点";
    case SelectionElementKind::Edge:         return "線";
    case SelectionElementKind::Face:         return "面";
    case SelectionElementKind::ControlPoint: return "制御点";
    case SelectionElementKind::WorkPlane:    return "作業平面";
    }
    return "不明";
}

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

//! 作図中の十字カーソル(V1 の DrawingCrossCursor)。
//!
//! 既定の Qt::CrossCursor は細く小さく、矢印との違いが手元で読めない。
//! V1 は白フチ付きの大きめの十字を自前で描いていた。作図中かどうかが
//! ひと目で分かるのは、この形の違いのおかげである。
//!
//! QCursor を関数 static の **値** で持つと、QApplication を畳んだ後の
//! デストラクタで落ちる。V1 と同じく、わざとポインタで持って解放しない。
QCursor V2Viewport::DrawingCrossCursor()
{
    static const QCursor* cursor = [] {
        constexpr int kSize = 33;
        constexpr int kCenter = kSize / 2;
        QPixmap pixmap(kSize, kSize);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(QColor(255, 255, 255, 235), 3.0));
        painter.drawLine(kCenter, 0, kCenter, kSize - 1);
        painter.drawLine(0, kCenter, kSize - 1, kCenter);
        painter.setPen(QPen(QColor(20, 46, 56, 255), 1.0));
        painter.drawLine(kCenter, 0, kCenter, kSize - 1);
        painter.drawLine(0, kCenter, kSize - 1, kCenter);
        // 中心は小さく開ける。開けないと、狙っている点が十字の下に隠れる。
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(kCenter - 2, kCenter - 2, 5, 5, QColor(Qt::transparent));
        painter.end();
        return new QCursor(pixmap, kCenter, kCenter);
    }();
    return *cursor;
}

void V2Viewport::RefreshCursorShape()
{
    // 掴めるかどうかが手元で分かるようにする。V1と同じ使い分け。
    drawingCursor_ = false;
    if (panning_) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (orbiting_ || gadgetDrag_.has_value() || cubeDrag_.active) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (controlDrag_.active || bodyDrag_.active) {
        setCursor(Qt::SizeAllCursor);
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
    const auto tool = session_->CurrentTool();
    if (tool == kachakacha::v2::modeling::DrawingTool::Select) {
        // 拾えるものの上では指にする(V1 と同じ)。押せる場所が手元で分かる。
        setCursor(hover_.snap.has_value() || !hoveredEntityId_.IsNil()
                ? Qt::PointingHandCursor
                : Qt::ArrowCursor);
        return;
    }
    if (tool == kachakacha::v2::modeling::DrawingTool::Measure) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    // 作図中はいつも十字。矢印との違いで「いま描ける」と分かる。
    drawingCursor_ = true;
    setCursor(DrawingCrossCursor());
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
    if (tool == DrawingTool::Measure) {
        // V1と同じ。測定の右クリックは「測ったものを消す」。道具は抜けない。
        // 測る相手は選択と押した点なので、両方を空にする。
        SetSelection(kachakacha::v2::app::SelectionSet{});
        ClearMeasurePicks();
        status_ = "測定を消しました。";
        if (statusCallback_) {
            statusCallback_(status_);
        }
        update();
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

bool V2Viewport::BeginBodyDrag(const QPointF& position)
{
    using kachakacha::v2::geometry::ScreenPoint;
    bodyDrag_ = BodyDrag{};
    if (session_->CurrentTool() != kachakacha::v2::modeling::DrawingTool::Select) {
        return false;
    }
    const ScreenPoint pointer{position.x(), position.y()};
    const auto& tolerance = session_->GetDocument().Snapshot().settings.tolerance;
    if (!kachakacha::v2::app::PointerGrabsSelection(session_->Scene(), selection_,
            mapping_, pointer, tolerance)) {
        return false;
    }
    // 掴んだ位置は作業平面の上で読む。画面の px のままだと、
    // 視点を回したときにどれだけ動いたのかが決まらない。
    const auto onPlane = mapping_.UnprojectOntoPlane(pointer, workPlane_.origin,
        workPlane_.normal);
    if (!onPlane.has_value()) {
        return false;
    }
    bodyDrag_.active = true;
    bodyDrag_.startPx = position;
    bodyDrag_.startPoint = *onPlane;
    RefreshCursorShape();
    return true;
}

void V2Viewport::DragBody(const QPointF& position)
{
    using kachakacha::v2::geometry::ScreenPoint;
    if (!bodyDrag_.active) {
        return;
    }
    if (kachakacha::v2::app::DragIsFarEnough(position.x() - bodyDrag_.startPx.x(),
            position.y() - bodyDrag_.startPx.y())) {
        bodyDrag_.moved = true;
    }
    const auto onPlane = mapping_.UnprojectOntoPlane(
        ScreenPoint{position.x(), position.y()}, workPlane_.origin, workPlane_.normal);
    if (!onPlane.has_value()) {
        return;
    }
    kachakacha::v2::geometry::Vector3 target = *onPlane;
    if (axisConstrainedByKey_) {
        // Shift を押していれば、掴んだ場所から水平・垂直へ寄せる。作図と同じ扱い。
        target = kachakacha::v2::modeling::ApplyAxisConstraint(
            kachakacha::v2::modeling::DrawingTool::Move, workPlane_,
            bodyDrag_.startPoint, target);
    }
    bodyDrag_.delta = kachakacha::v2::app::DragDelta(bodyDrag_.startPoint, target);
    if (statusCallback_) {
        status_ = "移動中: " + std::to_string(
            std::round(bodyDrag_.delta.Length() * 100.0) / 100.0) + "mm";
        statusCallback_(status_);
    }
    update();
}

bool V2Viewport::ReleaseBodyDrag(const QPointF& position)
{
    if (!bodyDrag_.active) {
        return false;
    }
    DragBody(position);
    const bool moved = bodyDrag_.moved;
    const auto delta = bodyDrag_.delta;
    bodyDrag_ = BodyDrag{};
    RefreshCursorShape();
    update();
    if (!moved) {
        // 押しただけ。0mm 動かしたことにはしない。
        return false;
    }
    if (!transform_) {
        return false;
    }
    kachakacha::v2::modeling::TransformPlan plan;
    plan.kind = kachakacha::v2::modeling::TransformKind::Move;
    plan.vectorArgument = delta;
    plan.keepsSource = false;
    plan.summaryJa = "移動: " + std::to_string(
        std::round(delta.Length() * 100.0) / 100.0) + "mm";
    transform_(plan);
    return true;
}

void V2Viewport::SetControlPointCallback(
    std::function<void(kachakacha::v2::base::EntityId, kachakacha::v2::base::SegmentId,
        const kachakacha::v2::geometry::CurveSegment&)> callback)
{
    controlPointChanged_ = std::move(callback);
}

bool V2Viewport::BeginControlPointDrag(const QPointF& position)
{
    using kachakacha::v2::geometry::ScreenPoint;
    controlDrag_ = ControlDrag{};
    if (session_->CurrentTool() != kachakacha::v2::modeling::DrawingTool::Select) {
        return false;
    }
    const auto found = kachakacha::v2::app::PickControlPoint(session_->Scene(),
        selection_, mapping_, ScreenPoint{position.x(), position.y()});
    if (!found.has_value()) {
        return false;
    }
    controlDrag_.active = true;
    controlDrag_.startPx = position;
    controlDrag_.handle = *found;
    status_ = std::string(found->labelJa) + " を掴みました。";
    if (statusCallback_) {
        statusCallback_(status_);
    }
    RefreshCursorShape();
    return true;
}

void V2Viewport::DragControlPoint(const QPointF& position)
{
    using kachakacha::v2::geometry::ScreenPoint;
    if (!controlDrag_.active) {
        return;
    }
    if (kachakacha::v2::app::DragIsFarEnough(position.x() - controlDrag_.startPx.x(),
            position.y() - controlDrag_.startPx.y())) {
        controlDrag_.moved = true;
    }
    const auto onPlane = mapping_.UnprojectOntoPlane(
        ScreenPoint{position.x(), position.y()}, workPlane_.origin, workPlane_.normal);
    if (!onPlane.has_value()) {
        return;
    }
    // 掴んでいる曲線を場面から探し直す。持ち歩くと、途中で文書が変わったときに古くなる。
    for (const auto& curve : session_->Scene().curves) {
        if (curve.segmentId != controlDrag_.handle.segmentId) {
            continue;
        }
        auto moved = kachakacha::v2::geometry::WithControlPointMoved(curve.segment,
            controlDrag_.handle.index, *onPlane);
        if (moved.HasValue()) {
            controlDrag_.preview = moved.Value();
        } else if (!moved.Diagnostics().empty()) {
            // つぶれる位置では形を作らない。前の形のまま出して、理由を言う。
            status_ = moved.Diagnostics().front().summaryJa;
            if (statusCallback_) {
                statusCallback_(status_);
            }
        }
        break;
    }
    update();
}

bool V2Viewport::ReleaseControlPointDrag(const QPointF& position)
{
    if (!controlDrag_.active) {
        return false;
    }
    DragControlPoint(position);
    const bool moved = controlDrag_.moved;
    const auto handle = controlDrag_.handle;
    const auto preview = controlDrag_.preview;
    controlDrag_ = ControlDrag{};
    RefreshCursorShape();
    update();
    if (!moved || !preview.has_value() || !controlPointChanged_) {
        // 掴んだだけ。形が変わっていないなら文書へ入れない。
        return false;
    }
    controlPointChanged_(handle.entityId, handle.segmentId, *preview);
    return true;
}

//! 塗った形(立体・面)を画面の点で、手前から順に拾う(棚卸し A-2)。
//!
//! 見えているのに掴めない、をなくすためのもの。判断は core(modeling/CollectMeshHits)。
//! 手前から並ぶので、Alt+クリックと Tab はこの並びのまま奥へ進める。
std::vector<kachakacha::v2::app::PickCandidate> V2Viewport::CollectShapeCandidatesAt(
    const QPointF& position) const
{
    using kachakacha::v2::geometry::ScreenPoint;
    std::vector<kachakacha::v2::app::PickCandidate> candidates;
    if (pickMeshes_.empty() || !display_.shapesVisible) {
        return candidates;
    }
    const auto ray = mapping_.RayThrough(ScreenPoint{position.x(), position.y()});
    if (!ray.has_value()) {
        return candidates;
    }
    const auto hits = kachakacha::v2::modeling::CollectMeshHits(pickMeshes_, ray->origin,
        ray->direction);
    candidates.reserve(hits.size());
    for (const auto& hit : hits) {
        if (hit.shapeIndex >= shapeViews_.size()) {
            continue;
        }
        kachakacha::v2::app::PickCandidate candidate;
        candidate.entityId = shapeViews_[hit.shapeIndex].entityId;
        candidate.kind = kachakacha::v2::app::SelectionElementKind::Object;
        candidate.hitPoint = hit.point;
        // 形には線の番号が無い。距離は画面上の px ではなく目からの mm である。
        // 形どうしの前後を決めるためだけに使い、線の px と比べない。
        candidate.distancePx = hit.distanceMm;
        candidates.push_back(candidate);
    }
    return candidates;
}

std::optional<kachakacha::v2::app::PickCandidate> V2Viewport::PickShapeAt(
    const QPointF& position) const
{
    const auto candidates = CollectShapeCandidatesAt(position);
    return candidates.empty() ? std::nullopt
                              : std::optional<kachakacha::v2::app::PickCandidate>{
                                    candidates.front()};
}

//! 画面の1点で拾えるものを、優先順位の順に全部並べる。
//!
//! 順は ui-ux-integrated-spec §4.3 のとおり **点 → 線 → 形** とする。
//! 点と線の中の並びは core が決める(同順位は画面上の距離が近い順)。
//! 形をいちばん後ろに置くのは、線の上を押したのに塗りが勝つと、
//! 面の内側にある線が永久に掴めなくなるためである。
std::vector<kachakacha::v2::app::PickCandidate> V2Viewport::CollectCandidatesAt(
    const QPointF& position) const
{
    using kachakacha::v2::geometry::ScreenPoint;
    auto candidates = kachakacha::v2::app::CollectPickCandidates(session_->Scene(), mapping_,
        ScreenPoint{position.x(), position.y()},
        session_->GetDocument().Snapshot().settings.tolerance, PickFocusNow());
    auto shapes = CollectShapeCandidatesAt(position);
    candidates.insert(candidates.end(), std::make_move_iterator(shapes.begin()),
        std::make_move_iterator(shapes.end()));
    return candidates;
}

std::optional<kachakacha::v2::app::PickCandidate> V2Viewport::CurrentCandidate() const
{
    if (cycle_.candidates.empty() || cycle_.index >= cycle_.candidates.size()) {
        return std::nullopt;
    }
    return cycle_.candidates[cycle_.index];
}

void V2Viewport::RefreshPickCycle(const QPointF& position)
{
    auto collected = CollectCandidatesAt(position);
    // 番号を持ち越すのは「同じ場所の、同じ並び」のときだけ。
    // どちらかが崩れたら先頭へ戻す。こうしないと、いくつ目を出しているのかが
    // カーソルを動かすたびに変わり、Tab の結果が読めなくなる。
    const double resetPx = session_->GetDocument().Snapshot().settings.tolerance
                               .displayPickPx;
    const bool sameSpot = cycle_.valid
        && std::hypot(position.x() - cycle_.anchorPx.x(),
               position.y() - cycle_.anchorPx.y()) <= resetPx;
    if (!sameSpot || !SameCandidateOrder(cycle_.candidates, collected)) {
        cycle_.anchorPx = position;
        cycle_.index = 0;
    }
    cycle_.candidates = std::move(collected);
    cycle_.valid = true;
    if (cycle_.index >= cycle_.candidates.size()) {
        cycle_.index = 0;
    }
}

void V2Viewport::ForgetPickCycle()
{
    cycle_ = PickCycle{};
}

void V2Viewport::AdvanceCandidate(bool backward)
{
    const std::size_t count = cycle_.candidates.size();
    if (count == 0) {
        return;
    }
    cycle_.index = backward ? (cycle_.index + count - 1) % count
                            : (cycle_.index + 1) % count;
}

void V2Viewport::SyncHoverWithCandidate()
{
    const auto current = CurrentCandidate();
    const auto previous = hoveredEntityId_;
    const auto previousSegment = hoveredSegmentId_;
    hoveredEntityId_ = current.has_value() ? current->entityId
                                           : kachakacha::v2::base::EntityId{};
    hoveredSegmentId_ = current.has_value() ? current->segmentId
                                            : kachakacha::v2::base::SegmentId{};
    if (previous != hoveredEntityId_ || previousSegment != hoveredSegmentId_) {
        RefreshCursorShape();
    }
}

bool V2Viewport::CycleCandidate(bool backward)
{
    // 送る相手は、いまカーソルがあるところの候補。押していなくても送れる。
    RefreshPickCycle(cursorPosition_);
    if (cycle_.candidates.size() < 2) {
        return false;   // 重なっていない。送る先が無い。
    }
    AdvanceCandidate(backward);
    SyncHoverWithCandidate();
    // どれを出しているかを言う。強調だけでは、何個のうちの何番目かが読めない。
    const auto current = CurrentCandidate();
    status_ = std::string("候補 ") + std::to_string(cycle_.index + 1) + "/"
        + std::to_string(cycle_.candidates.size()) + "("
        + (current.has_value() ? CandidateKindNameJa(current->kind) : "なし")
        + ")。クリックで決まります。";
    if (statusCallback_) {
        statusCallback_(status_);
    }
    update();
    return true;
}
