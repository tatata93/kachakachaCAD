//! カーソル横の数値入力列(ui-workflows §7)。出す・移す・打つ・確定する・描く。
//!
//! V2Viewport.cpp が 1500 行の上限に届いたので、入力列まわりだけをここへ移した。
//! 中身は動かしていない(欄の決め方も解き方も core の app/CursorInput にある)。

#include "V2Viewport.h"

#include "kachakacha/app/CursorInput.h"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <string>

using kachakacha::v2::geometry::ScreenPoint;
using kachakacha::v2::geometry::Vector3;

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
        viewMessage_ = begun.FirstSummaryJa();
        return false;
    }
    cursorPanel_ = begun.Value();
    // 作り方カードが名指しした欄(円の「直径指定」なら 直径)へ、出した瞬間に移す。
    // 移さないと、カードを押しても Tab で探すことになり「押しても何も起きない」に見える。
    if (!preferredCursorFieldId_.isEmpty()) {
        const auto moved = kachakacha::v2::app::FocusField(cursorPanel_,
            preferredCursorFieldId_.toStdString());
        if (moved.HasValue()) {
            cursorPanel_ = moved.Value();
        }
    }
    cursorDelta_ = Vector3{};
    update();
    return true;
}

//! 作り方カードが選ばれたときに、次に出す入力列で先に選んでおく欄。空なら既定のまま。
void V2Viewport::SetPreferredCursorField(const QString& fieldId)
{
    preferredCursorFieldId_ = fieldId;
    // いま出ているなら、その場で移す。出ていなければ次に出たときに移る。
    if (cursorPanel_.active && !fieldId.isEmpty()) {
        const auto moved = kachakacha::v2::app::FocusField(cursorPanel_,
            fieldId.toStdString());
        if (moved.HasValue()) {
            cursorPanel_ = moved.Value();
            update();
        }
    }
}

void V2Viewport::SyncCursorInputWithTool(bool placedPoint, bool committed)
{
    // 最初の点を置いた直後に入力列を出す(ui-workflows §7)。確定したら閉じる。
    // ポリラインは確定せずに点が増えるので、置くたびに新しい基準で出し直す。
    if (committed || !session_->HasPlacedPoints()) {
        if (cursorPanel_.active) {
            CloseCursorInput();
        }
        return;
    }
    if (placedPoint && kachakacha::v2::app::ToolUsesCursorInput(session_->CurrentTool())) {
        (void)OpenCursorInput();
    }
}

//! 入力列が出ているときの Enter。**人が押す道はここ1本**(画面の鍵盤も自己試験も通る)。
//! 主要欄が決まったら、その値で次の点を置く。まだなら次の欄へ移る。
bool V2Viewport::PressEnterInCursorInput()
{
    if (!cursorPanel_.active) {
        return false;
    }
    if (CommitCursorField()) {
        return PlacePointFromCursorInput();
    }
    if (cursorPanel_.active && !cursorPanel_.states.empty()
        && cursorPanel_.focusedIndex < cursorPanel_.states.size()
        && !cursorPanel_.states[cursorPanel_.focusedIndex].error) {
        (void)FocusNextCursorField(false);
    }
    return false;
}

bool V2Viewport::PlacePointFromCursorInput()
{
    const auto solved = kachakacha::v2::app::SolveDelta(cursorPanel_, cursorDelta_);
    if (!solved.HasValue()) {
        viewMessage_ = solved.FirstSummaryJa();
        if (statusCallback_) {
            statusCallback_(viewMessage_);
        }
        update();
        return false;
    }
    // 欄の値は作業平面の u, v(mm)。基準の点からその分だけ進んだ世界座標に置く。
    const Vector3 anchor = session_->ConstraintAnchor();
    const Vector3 world = cursorPanel_.onWorkPlane
        ? anchor + workPlane_.uAxis * solved.Value().x + workPlane_.vAxis * solved.Value().y
        : anchor + solved.Value();
    const auto result = session_->PlacePoint(world);
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
    hover_ = session_->Hover(ScreenPoint{cursorPosition_.x(), cursorPosition_.y()});
    update();
    return result.placedPoint;
}

bool V2Viewport::focusNextPrevChild(bool /*next*/)
{
    // 図面の上では Tab を焦点移動に使わない。候補送りと数値入力欄へ回す。
    return false;
}

//! いま拾う相手を絞る印。作図中は作業平面の上の線だけを拾う。
kachakacha::v2::app::PickFocus V2Viewport::PickFocusNow() const
{
    kachakacha::v2::app::PickFocus focus;
    focus.drawing = session_->CurrentTool() != kachakacha::v2::modeling::DrawingTool::Select;
    focus.dimOffPlane = display_.dimOffPlaneLines;
    focus.plane = workPlane_;
    return focus;
}

void V2Viewport::DiscardHoverState()
{
    // 道具の持ち物はまとめて捨てる。preview だけ消して snap を残すと、
    // 前の道具が選んでいた吸着先のリングが出たまま、次の道具の点がそこへ寄る。
    hover_ = kachakacha::v2::app::HoverResult{};
    // Tab・Alt の候補送りも道具ごとの持ち物である。番号だけ残ると、
    // 次の道具で Tab を押した拍子に、前の道具で送っていた先が出る。
    ForgetPickCycle();
    hoverOffPlane_ = false;
}

void V2Viewport::OnToolChanged()
{
    if (cursorPanel_.active) {
        CloseCursorInput();
    }
    // 道具を替えると途中の点は捨てられる(DrawingSession::SelectTool)。ここで消さないと、
    // もう作られない形の途中経過だけが残り、まだ引いている途中に見える(§3 規則3)。
    // 途中経過だけでなく、吸着・位置・候補送りも前の道具のものである。一緒に捨てる。
    const std::string beforeJa = hover_.messageJa;
    DiscardHoverState();
    ClearMeasurePicks();
    // 押し出しの下見も残さない。前の道具の手つきが画面に残ると、
    // いま何をしているのか読めなくなる(オーナー指示 2026-09-14 §4)。
    if (extrudeHandle_.shown && cancelExtrude_) {
        cancelExtrude_();
    }
    HideExtrudeHandle();
    // 捨てたままだと、マウスを動かすまで新しい道具の吸着もリングも出ない。
    // いまのカーソル位置で **一度だけ** 見直す。持ち越しは既に捨ててあるので、
    // ここで前の道具の吸着先が復活することはない。
    RefreshHoverAfterToolChange(beforeJa);
    // 道具が変わればカーソルの形も変わる。ここで呼ばないと、次に押すまで
    // 矢印のままで、いま作図できるのかどうかが手元で分からない。
    RefreshCursorShape();
    update();
}

void V2Viewport::RefreshHoverInPlace()
{
    hover_ = session_->Hover(kachakacha::v2::geometry::ScreenPoint{
        cursorPosition_.x(), cursorPosition_.y()});
    RefreshPickCycle(cursorPosition_);
    SyncHoverWithCandidate();
    RefreshForbiddenHover(cursorPosition_);
}

void V2Viewport::RefreshHoverAfterToolChange(const std::string& beforeJa)
{
    RefreshHoverInPlace();
    // 帯を書き換えるのは、いま出ているのが前の案内のときだけにする。
    // 断った理由や作った結果が出ているときに上書きすると、読む前に消える。
    if (status_ == beforeJa || status_.empty()) {
        status_ = hover_.messageJa;
        if (statusCallback_) {
            statusCallback_(status_);
        }
    }
}

void V2Viewport::ClearMeasurePicks()
{
    if (measurePicks_.empty()) {
        return;
    }
    measurePicks_.clear();
    if (measurePicksChanged_) {
        measurePicksChanged_();
    }
    update();
}

void V2Viewport::SetMeasurePicksChangedCallback(std::function<void()> callback)
{
    measurePicksChanged_ = std::move(callback);
}

void V2Viewport::CloseCursorInput()
{
    cursorPanel_ = kachakacha::v2::app::CancelCursorInput(cursorPanel_);
    update();
}

bool V2Viewport::FocusNextCursorField(bool backward)
{
    // 打った字は捨てずに留めてから移る(core が確かめる)。
    const auto moved = kachakacha::v2::app::LockAndFocusNextField(cursorPanel_, cursorDelta_,
        backward);
    if (!moved.HasValue()) {
        viewMessage_ = moved.FirstSummaryJa();
        if (cursorPanel_.focusedIndex < cursorPanel_.states.size()) {
            cursorPanel_.states[cursorPanel_.focusedIndex].error = true;
            cursorPanel_.states[cursorPanel_.focusedIndex].messageJa = viewMessage_;
        }
        if (statusCallback_) {
            statusCallback_(viewMessage_);
        }
        update();
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
        viewMessage_ = typed.FirstSummaryJa();
        return false;
    }
    cursorPanel_ = typed.Value();
    update();
    return true;
}

bool V2Viewport::CommitCursorField()
{
    const auto committed = kachakacha::v2::app::CommitFocusedField(cursorPanel_,
        cursorDelta_);
    if (!committed.HasValue()) {
        // 断られたら、その欄を赤くして理由を出す。入力列は閉じない。
        const std::size_t at = cursorPanel_.focusedIndex;
        if (at < cursorPanel_.states.size()) {
            cursorPanel_.states[at].error = true;
            cursorPanel_.states[at].messageJa =
                committed.FirstSummaryJa();
        }
        viewMessage_ = committed.FirstSummaryJa();
        if (statusCallback_) {
            statusCallback_(viewMessage_);
        }
        update();
        return false;
    }
    cursorPanel_ = committed.Value().panel;
    viewMessage_.clear();
    update();
    // 主要欄(長さ・半径)が決まれば形は決まる。決まっていなければ次の欄へ。
    return committed.Value().readyToFinish;
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
