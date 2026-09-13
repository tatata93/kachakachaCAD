//! マウスとキーの土台(V1同等、docs/v2/v1-input-parity.md)。
//!
//! ここに集めたのは、V1にあって V2 に無かったものである。
//!   - 中ボタン(と右ボタン)のドラッグで画面を移動
//!   - Shift+中ボタンで軌道回転
//!   - Esc は、やりかけを1つ取り消してから選択道具へ戻り、選択も解除する
//!   - 作図中の Ctrl で吸着を一時停止、Shift で水平・垂直・正方形へ固定
//!   - 掴めるかどうかが分かるカーソル
//!   - 重なった候補を Tab で送り、Alt+クリックで奥を選ぶ(ui-ux-integrated-spec §4.2)
//!   - 左ドラッグの矩形選択。左から右は完全包含、右から左は交差(同 §4.2)
//!
//! どれも「画面が無いと確かめられない」ものではない。
//! 判断は core(app/EscapeAction、modeling/DrawingConstraint)にある。
//! ここはそれを Qt へつなぐだけである。

#include "V2Viewport.h"
#include <QCursor>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QString>

#include "kachakacha/app/ControlPointPick.h"
#include "kachakacha/app/GrabToMove.h"
#include "kachakacha/modeling/DrawingConstraint.h"
#include "kachakacha/modeling/MeshPick.h"

#include <QCursor>

#include <cmath>
#include <iterator>
#include <map>
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

//! 回転中のカーソル(§5.1「回転: 回転を示すカーソル」)。
//!
//! Qt には回転を示す形が無い。閉じた手で代用していたので、
//! パンと軌道回転が手元で見分けられなかった。V1 の十字と同じやり方で描く。
//! QApplication を畳んだ後に落ちないよう、わざとポインタで持って解放しない。
QCursor V2Viewport::RotateCursor()
{
    static const QCursor* cursor = [] {
        constexpr int kSize = 32;
        constexpr double kCenter = kSize / 2.0;
        QPixmap pixmap(kSize, kSize);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        // 白フチ → 本体の順で2度描く。どんな背景でも輪郭が残る。
        for (int pass = 0; pass < 2; ++pass) {
            const QColor color = pass == 0 ? QColor(255, 255, 255, 235)
                                           : QColor(20, 46, 56, 255);
            painter.setPen(QPen(color, pass == 0 ? 4.0 : 2.0));
            painter.setBrush(Qt::NoBrush);
            // 三方だけの円。開けておかないと、ただの丸に見える。
            painter.drawArc(QRectF(5.0, 5.0, kSize - 10.0, kSize - 10.0), 30 * 16,
                280 * 16);
            // 矢の頭。円の終わりに付ける。
            painter.setBrush(QBrush(color));
            const QPointF tip[3] = {
                QPointF(kCenter + 9.0, 3.0),
                QPointF(kCenter + 2.0, 9.0),
                QPointF(kCenter + 11.0, 11.0),
            };
            painter.drawPolygon(tip, 3);
        }
        painter.end();
        return new QCursor(pixmap, static_cast<int>(kCenter), static_cast<int>(kCenter));
    }();
    return *cursor;
}

//! いまの様子を core へ渡す形にまとめる。
kachakacha::v2::app::PointerCursorContext V2Viewport::CursorContextNow() const
{
    kachakacha::v2::app::PointerCursorContext context;
    context.panning = panning_ && !orbiting_;
    context.orbiting = orbiting_;
    context.draggingBody = bodyDrag_.active;
    context.draggingControlPoint = controlDrag_.active;
    context.draggingViewGadget = gadgetDrag_.has_value() || cubeDrag_.active;
    context.overViewGadget = gadgetHoverIndex_.has_value() || cubeHoverZone_.has_value();
    // 薄く出ている(作業平面の外の)線の上は、押しても拾えない。
    // 拾えないことは、押してみるまで分からないと困る(§5.1「禁止対象」)。
    context.overForbidden = hoverOffPlane_;
    const auto tool = session_->CurrentTool();
    // 押すと作図点が置かれる道具かどうか。選択と測るは点を置かない。
    context.placingPoints = tool != kachakacha::v2::modeling::DrawingTool::Select
        && tool != kachakacha::v2::modeling::DrawingTool::Measure;
    return context;
}

void V2Viewport::RefreshCursorShape()
{
    // 形の決め方は core(app/PointerCursor)にある。ここは Qt へ写すだけ。
    // §5.1「クリック可能な通常形状に指カーソルを使わない」。
    // 以前は線や形の上で指にしていた。指は「別の場所へ行く」印であって、
    // 図形の印ではない。図形の上に来たことは Hover の強調で伝える。
    using kachakacha::v2::app::CursorShape;
    const CursorShape shape = kachakacha::v2::app::ChooseCursorShape(CursorContextNow());
    drawingCursor_ = shape == CursorShape::Cross;
    switch (shape) {
    case CursorShape::ClosedHand:
        setCursor(Qt::ClosedHandCursor);
        return;
    case CursorShape::OpenHand:
        setCursor(Qt::OpenHandCursor);
        return;
    case CursorShape::Rotate:
        setCursor(RotateCursor());
        return;
    case CursorShape::Move:
        setCursor(Qt::SizeAllCursor);
        return;
    case CursorShape::Forbidden:
        setCursor(Qt::ForbiddenCursor);
        return;
    case CursorShape::Cross:
        setCursor(DrawingCrossCursor());
        return;
    case CursorShape::Arrow:
        break;
    }
    setCursor(Qt::ArrowCursor);
}

void V2Viewport::SetContextMenuCallback(
    std::function<std::optional<int>(const QPoint&, const std::vector<QString>&)> callback)
{
    contextMenu_ = std::move(callback);
}

std::vector<QString> V2Viewport::CandidateLabels() const
{
    const auto& snapshot = session_->GetDocument().Snapshot();
    std::vector<QString> labels;
    labels.reserve(cycle_.candidates.size());
    for (const auto& candidate : cycle_.candidates) {
        // 名前は文書のものをそのまま出す。画面で付け直すと、一覧と食い違う。
        const auto* entity = session_->GetDocument().FindEntity(candidate.entityId);
        const QString name = (entity != nullptr && !entity->displayName.empty())
            ? QString::fromStdString(entity->displayName)
            : QStringLiteral("名前のないもの");
        QString label = name + QStringLiteral(" / ")
            + QString::fromUtf8(CandidateKindNameJa(candidate.kind));
        // 所属するまとまりも出す(ui-workflows.md §3.2)。
        // 同じ名前の物が別のまとまりにあるとき、これが無いと見分けられない。
        if (entity != nullptr && entity->groupId.has_value()) {
            for (const auto& group : snapshot.groups) {
                if (group.id == *entity->groupId && !group.displayName.empty()) {
                    label += QStringLiteral(" (")
                        + QString::fromStdString(group.displayName) + QStringLiteral(")");
                    break;
                }
            }
        }
        labels.push_back(label);
    }
    // 同じ見出しになったものへ通し番号を足す。同じ名前の物体も、
    // 同じ物体の別の線分・別の面も、番号が無いと一覧の上で区別できない。
    std::map<QString, int> total;
    for (const QString& label : labels) {
        ++total[label];
    }
    std::map<QString, int> seen;
    for (QString& label : labels) {
        if (total[label] < 2) {
            continue;
        }
        const int order = ++seen[label];
        label += QStringLiteral(" #") + QString::number(order);
    }
    return labels;
}

bool V2Viewport::SelectCandidate(std::size_t index)
{
    if (index >= cycle_.candidates.size()) {
        return false;
    }
    // 出している候補もそこへ移す。移さないと、献立で選んだものと
    // 次のクリックで選ばれるものが食い違う。
    cycle_.index = index;
    SyncHoverWithCandidate();
    SetSelection(kachakacha::v2::app::ApplySelection(selection_, CurrentCandidate(),
        kachakacha::v2::app::SelectionMode::Replace));
    ReportSelectionCount();
    update();
    return true;
}

void V2Viewport::PressRightWithoutMoving()
{
    // 場所を渡されなければ、最後にカーソルがあった場所で同じことをする。
    PressRightWithoutMoving(cursorPosition_);
}

void V2Viewport::PressRightWithoutMoving(const QPointF& position)
{
    using kachakacha::v2::modeling::DrawingTool;
    const DrawingTool tool = session_->CurrentTool();
    if (tool == DrawingTool::Select) {
        // V1と同じ。選択道具のときだけ、右クリックでメニューを出す。
        if (!contextMenu_) {
            return;
        }
        // 出す候補は Hover や Tab と同じ一箇所(cycle_)から取る。
        // 別に拾い直すと、献立に並ぶものと画面に出ているものが食い違う。
        RefreshPickCycle(position);
        // 重なっているときだけ一覧を出す。1件以下では選び分ける相手がいない。
        std::vector<QString> labels;
        if (cycle_.candidates.size() >= 2) {
            labels = CandidateLabels();
        }
        const std::optional<int> chosen = contextMenu_(mapToGlobal(position.toPoint()),
            labels);
        // 候補を選ばなかった(台帳のコマンドを選んだ・閉じた)なら選択は動かさない。
        // 空白の右クリックで選んでいたものが消えては、次の操作の相手がいなくなる。
        if (chosen.has_value() && *chosen >= 0) {
            (void)SelectCandidate(static_cast<std::size_t>(*chosen));
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
    bodyDrag_.gesture.Begin(pointer);
    RefreshCursorShape();
    return true;
}

void V2Viewport::DragBody(const QPointF& position)
{
    using kachakacha::v2::geometry::ScreenPoint;
    if (!bodyDrag_.active) {
        return;
    }
    (void)bodyDrag_.gesture.Update(ScreenPoint{position.x(), position.y()});
    if (!bodyDrag_.gesture.IsDrag()) {
        // まだ「押しただけ」である。ここで先に進むと、選ぼうとして
        // 1px 手が揺れただけで、掴んだ物が画面の上を滑って見える。
        // 門を越えるまでは、仮の見せかけも作らない(UI-P1-008)。
        return;
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
    const bool moved = bodyDrag_.gesture.Release(
        kachakacha::v2::geometry::ScreenPoint{position.x(), position.y()})
        == kachakacha::v2::app::PointerGesture::Drag;
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
    controlDrag_.gesture.Begin(ScreenPoint{position.x(), position.y()});
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
    (void)controlDrag_.gesture.Update(ScreenPoint{position.x(), position.y()});
    if (!controlDrag_.gesture.IsDrag()) {
        // 掴んだだけ。門を越えるまでは形を作り直さない。
        return;
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
    const bool moved = controlDrag_.gesture.Release(
        kachakacha::v2::geometry::ScreenPoint{position.x(), position.y()})
        == kachakacha::v2::app::PointerGesture::Drag;
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

std::vector<kachakacha::v2::app::PickCandidate> V2Viewport::CollectBoxShapeCandidates(
    const kachakacha::v2::app::BoxSelection& request) const
{
    std::vector<kachakacha::v2::app::PickCandidate> candidates;
    if (!display_.shapesVisible) {
        return candidates;
    }
    for (const auto& shape : shapeViews_) {
        // 数えるのは稜線だけにする。三角形の網まで見ると、面の内側の継ぎ目が
        // 「触れた」を作ってしまい、塗りの中を少しかすめただけで選ばれる。
        kachakacha::v2::app::BoxReach reach;
        for (const auto& edge : shape.mesh.edges) {
            kachakacha::v2::app::AccumulateBoxReach(reach, request.box, mapping_, edge);
        }
        if (!reach.Taken(request.kind)) {
            continue;
        }
        kachakacha::v2::app::PickCandidate candidate;
        candidate.entityId = shape.entityId;
        candidate.kind = kachakacha::v2::app::SelectionElementKind::Object;
        if (reach.hitPoint.has_value()) {
            candidate.hitPoint = *reach.hitPoint;
        }
        candidates.push_back(candidate);
    }
    return candidates;
}

kachakacha::v2::app::SelectionMode V2Viewport::SelectionModeFor(
    Qt::KeyboardModifiers modifiers)
{
    // Ctrl だけが選択の追加・解除。Shift は作図拘束、Alt は奥候補へ予約する。
    return (modifiers & Qt::ControlModifier) != 0
        ? kachakacha::v2::app::SelectionMode::Toggle
        : kachakacha::v2::app::SelectionMode::Replace;
}

void V2Viewport::BeginBoxSelect(const QPointF& position, Qt::KeyboardModifiers modifiers)
{
    boxSelect_ = BoxSelect{};
    boxSelect_.active = true;
    boxSelect_.startPx = position;
    boxSelect_.currentPx = position;
    // 押した時点の選択を覚える。離すときはここから当て直す。
    boxSelect_.selectionAtPress = selection_;
    boxSelect_.modifiers = modifiers;
    boxSelect_.gesture.Begin(
        kachakacha::v2::geometry::ScreenPoint{position.x(), position.y()});
}

void V2Viewport::DragBoxSelect(const QPointF& position)
{
    if (!boxSelect_.active) {
        return;
    }
    boxSelect_.currentPx = position;
    (void)boxSelect_.gesture.Update(
        kachakacha::v2::geometry::ScreenPoint{position.x(), position.y()});
    // 引いている間も「最後にカーソルがあった場所」は進める。
    // 止めると、離した直後の Tab が古い場所の候補を送る。
    cursorPosition_ = position;
    const auto kind = BoxSelectKind();
    // 離す前に、どちらの取り方になるのかを言う。向きで意味が変わることは
    // 引いている矩形を見るだけでは分からない。
    if (!kind.has_value()) {
        status_ = "矩形選択: あと少し引くと始まります。";
    } else if (*kind == kachakacha::v2::app::BoxSelectionKind::Contained) {
        status_ = "矩形選択(左から右): 完全に含まれるものだけを選びます。";
    } else {
        status_ = "矩形選択(右から左): 触れたものも選びます。";
    }
    if (statusCallback_) {
        statusCallback_(status_);
    }
    update();
}

QRectF V2Viewport::BoxSelectRect() const
{
    if (!boxSelect_.active) {
        return QRectF();
    }
    return QRectF(boxSelect_.startPx, boxSelect_.currentPx).normalized();
}

std::optional<kachakacha::v2::app::BoxSelectionKind> V2Viewport::BoxSelectKind() const
{
    using kachakacha::v2::geometry::ScreenPoint;
    if (!boxSelect_.active) {
        return std::nullopt;
    }
    if (!boxSelect_.gesture.IsDrag()) {
        return std::nullopt;   // まだ「押しただけ」。矩形として扱わない。
    }
    const ScreenPoint from{boxSelect_.startPx.x(), boxSelect_.startPx.y()};
    const ScreenPoint to{boxSelect_.currentPx.x(), boxSelect_.currentPx.y()};
    return kachakacha::v2::app::MakeBoxSelection(from, to).kind;
}

void V2Viewport::CancelBoxSelect()
{
    if (!boxSelect_.active) {
        return;
    }
    boxSelect_ = BoxSelect{};
    status_ = "矩形選択をやめました。";
    if (statusCallback_) {
        statusCallback_(status_);
    }
    update();
}

bool V2Viewport::ReleaseBoxSelect(const QPointF& position)
{
    using kachakacha::v2::app::BoxSelectionKind;
    using kachakacha::v2::geometry::ScreenPoint;
    if (!boxSelect_.active) {
        return false;
    }
    const ScreenPoint from{boxSelect_.startPx.x(), boxSelect_.startPx.y()};
    const ScreenPoint to{position.x(), position.y()};
    const auto base = boxSelect_.selectionAtPress;
    const auto mode = SelectionModeFor(boxSelect_.modifiers);
    // 引きずったかどうかは **道のり** で決める。離した場所だけを見ると、
    // 大きく引いてから押した場所へ戻して離したときに、
    // 引きずらなかったことになってしまう(UI-P1-008)。
    const bool dragged = boxSelect_.gesture.Release(to)
        == kachakacha::v2::app::PointerGesture::Drag;
    boxSelect_ = BoxSelect{};
    if (!dragged) {
        // 押しただけ。押した時点のクリック選択(SelectAt)をそのまま残す。
        update();
        return false;
    }
    cursorPosition_ = position;
    const auto request = kachakacha::v2::app::MakeBoxSelection(from, to);
    // 線と作図点は core が場面から集める。塗った形は画面が持つ網から集める。
    auto candidates = kachakacha::v2::app::CollectBoxPickCandidates(session_->Scene(),
        mapping_, request, session_->GetDocument().Snapshot().settings.tolerance,
        PickFocusNow());
    auto shapes = CollectBoxShapeCandidates(request);
    candidates.insert(candidates.end(), std::make_move_iterator(shapes.begin()),
        std::make_move_iterator(shapes.end()));
    SetSelection(kachakacha::v2::app::ApplyBoxSelection(base, candidates, mode));
    // 離した場所の候補を集め直す。矩形の前の候補を出したままだと、
    // 次の Tab がどこの候補を送っているのか読めない。
    RefreshPickCycle(position);
    SyncHoverWithCandidate();
    status_ = std::string(request.kind == BoxSelectionKind::Contained
            ? "矩形に完全に含まれるものを選びました。選んでいるもの: "
            : "矩形に触れたものも選びました。選んでいるもの: ")
        + std::to_string(kachakacha::v2::app::SelectionItemCount(selection_)) + " 件";
    if (statusCallback_) {
        statusCallback_(status_);
    }
    update();
    return true;
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
    hoverOffPlane_ = false;
}

//! いまカーソルの下にあるものが「見えているのに掴めない」か(§5.1 の禁止対象)。
//!
//! 作図中は作業平面の外の線を薄くして、掴まないことにしている(PlaneFocus)。
//! 薄い線の上で押しても何も起きないので、押してみるまで理由が分からなかった。
//! 掴めないことはカーソルの形で先に言う。
//!
//! 拾えたときは調べない。調べるのは、絞った結果が空になったときだけである。
//! 毎回2度拾うと、線の多い文書でカーソルが重くなる。
void V2Viewport::RefreshForbiddenHover(const QPointF& position)
{
    using kachakacha::v2::geometry::ScreenPoint;
    hoverOffPlane_ = false;
    if (!cycle_.candidates.empty()) {
        return;   // 拾えた。禁止ではない。
    }
    const auto focus = PickFocusNow();
    if (!focus.drawing || !focus.dimOffPlane) {
        return;   // 絞っていない。空なのは、そこに何も無いからである。
    }
    // 絞りを外して拾い直す。ここで拾えるなら、絞りが弾いたということ。
    const auto loose = kachakacha::v2::app::PickCurve(session_->Scene(), mapping_,
        ScreenPoint{position.x(), position.y()},
        session_->GetDocument().Snapshot().settings.tolerance);
    hoverOffPlane_ = loose.has_value();
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

// ---------------------------------------------------------------------------
// マウスの便り。V2Viewport.cpp が行数の上限に近づいたのでこちらへ移した。
// 中身は移す前と同じで、クリックと引きずりの分け方だけ PointerGesture へ寄せた。
// ---------------------------------------------------------------------------

void V2Viewport::mouseMoveEvent(QMouseEvent* event)
{
    // Shift はマウスの便りにも乗っているので、作図拘束へ反映する。
    // 一時スナップ解除の S は修飾キーではないため、キーイベントで保持する。
    SetAxisConstraintByKey((event->modifiers() & Qt::ShiftModifier) != 0);
    if (controlDrag_.active) {
        DragControlPoint(event->position());
        return;
    }
    if (bodyDrag_.active) {
        DragBody(event->position());
        return;
    }
    if (boxSelect_.active) {
        // 矩形を引いている。当たり判定もスナップも探さない。
        // 探すと、引いている途中に Hover が動いて、どこを囲っているのか読めなくなる。
        DragBoxSelect(event->position());
        return;
    }
    if (rightPressed_) {
        // 右で引きずってもカメラは1mmも動かさない(ui-ux-integrated-spec §5.2)。
        // 引きずったかどうかだけ覚えて、離すときに献立を出すか決める。
        (void)rightGesture_.Update(
            kachakacha::v2::geometry::ScreenPoint{event->position().x(),
                event->position().y()});
        return;
    }
    if (panning_) {
        const QPointF delta = event->position() - lastDragPosition_;
        if (orbiting_) {
            OrbitByPixels(delta.x(), delta.y());
        } else {
            PanByPixels(delta.x(), delta.y());
        }
        lastDragPosition_ = event->position();
        return;
    }
    if (gadgetDrag_.has_value()) {
        DragViewGadget(event->position());
        return;
    }
    if (cubeDrag_.active) {
        DragViewCube(event->position());
        return;
    }
    // 操作板の上に来たら光らせる。押せる場所が目で分かるようにする。
    // 見る順は押すときと同じ(ボタン → キューブ → 輪)。
    // 違う順で見ると、光る場所と実際に動く物が食い違う。
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
    setFocus();
    // 右ボタンはカメラへ割り当てない(ui-ux-integrated-spec §5.2)。
    // 押した場所だけ覚えて、離すときに「押しただけ」かどうかを決める。
    if (event->button() == Qt::RightButton) {
        // 左で掴んでいる最中・矩形を引いている最中は右を受けない。
        // 掴んだまま献立が出ると、どこで離したことになるのかが決まらない。
        if (controlDrag_.active || bodyDrag_.active || boxSelect_.active) {
            return;
        }
        rightPressed_ = true;
        rightGesture_.Begin(kachakacha::v2::geometry::ScreenPoint{event->position().x(),
            event->position().y()});
        return;
    }
    // 中ボタンは画面を動かす(V1同等)。Shift+中ボタンは軌道回転。
    // 押した時点では動かさず、引きずってから決める。
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        orbiting_ = (event->modifiers() & Qt::ShiftModifier) != 0;
        lastDragPosition_ = event->position();
        RefreshCursorShape();
        return;
    }
    kachakacha::v2::view::AxisArrowModifier modifier =
        kachakacha::v2::view::AxisArrowModifier::None;
    if ((event->modifiers() & Qt::ShiftModifier) != 0) {
        modifier = kachakacha::v2::view::AxisArrowModifier::Fine;
    } else if ((event->modifiers() & Qt::ControlModifier) != 0) {
        modifier = kachakacha::v2::view::AxisArrowModifier::Coarse;
    }
    // 順は PressViewNavigator が持っている。ここで書き写さない。
    if (PressViewNavigator(event->position(), modifier) != ViewPress::None) {
        return;
    }
    if (session_->CurrentTool() == kachakacha::v2::modeling::DrawingTool::Select) {
        // 制御点 → 選んだ物、の順で掴む。順を逆にすると、
        // 制御点が線の上に乗っているので、いつまでも制御点を掴めない。
        if (event->modifiers() == Qt::NoModifier
            && BeginControlPointDrag(event->position())) {
            return;
        }
        // 選んでいる物の上を押したら、掴んだとみなす(V1同等)。
        // 引きずらずに離せば、ただの選び直しとして扱う。
        if (event->modifiers() == Qt::NoModifier && BeginBodyDrag(event->position())) {
            return;
        }
        // 押した瞬間は今までどおり1件を選ぶ。返りを離すまで待たせない。
        // 同時に矩形選択の構えへ入り、5px 以上引いて離したときだけ矩形として決める。
        // 構えるのを先にするのは、矩形が「押した時点の選択」から当て直すためである。
        if (event->button() == Qt::LeftButton && !PickPending()) {
            BeginBoxSelect(event->position(), event->modifiers());
        }
        SelectAt(event->position(), event->modifiers());
        return;
    }
    ClickAt(event->position());
}

void V2Viewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (rightPressed_ && event->button() == Qt::RightButton) {
        const auto kind = rightGesture_.Release(
            kachakacha::v2::geometry::ScreenPoint{event->position().x(),
                event->position().y()});
        rightPressed_ = false;
        rightGesture_.Reset();
        // 引きずった後の解放では献立を出さない。出すと、画面をなぞっただけで
        // メニューが飛び出す。押しただけのときが右クリックである。
        if (kind == kachakacha::v2::app::PointerGesture::Click) {
            PressRightWithoutMoving(event->position());
        }
        return;
    }
    if (controlDrag_.active) {
        (void)ReleaseControlPointDrag(event->position());
        return;
    }
    if (bodyDrag_.active) {
        // 引きずっていなければ選び直しになる。掴んだ場所で選び直す。
        if (!ReleaseBodyDrag(event->position())) {
            SelectAt(event->position(), event->modifiers());
        }
        return;
    }
    if (boxSelect_.active && event->button() == Qt::LeftButton) {
        // 引きずっていなければ何もしない。押した時点の選択がそのまま残る。
        (void)ReleaseBoxSelect(event->position());
        return;
    }
    if (panning_) {
        panning_ = false;
        orbiting_ = false;
        RefreshCursorShape();
        return;
    }
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

