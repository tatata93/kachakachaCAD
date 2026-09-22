//! 道具の Enter / Esc を、窓のところで受ける(オーナー指示 2026-09-15 §14)。
//!
//! これまで Enter と Esc は `V2Viewport::keyPressEvent` にしか無かった。
//! 3D画面にキーボードの焦点が無いと届かないので、右の欄へ数字を打った直後の
//! Enter は何も起きなかった。**直すには 3D を一度クリックするしかなく、
//! そのクリックで選択が変わる。**
//!
//! ここでは窓が合図を先に受け取る。焦点がどこにあっても同じように効く。
//!
//! 数値欄との取り合いは、次の1つの決まりで解く。
//!
//!   - 欄の中で Enter を押し、**値が変わった** → その値を入れて下見を作り直す。確定しない。
//!   - 欄の中で Enter を押し、**値が変わっていない** → 確定する。
//!   - 欄の外で Enter → 確定する。
//!
//! 「打った値は必ず一度、下見で見える」ようになる。見ていない値で作らない
//! (オーナー指示 §9)。

#include "V2MainWindow.h"
#include "V2EdgeFinishTool.h"
#include "V2SolidTool.h"
#include "V2SurfaceEditTool.h"

#include "V2Viewport.h"

#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/ToolKeys.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QObject>
#include <QString>
#include <QWidget>

namespace {

//! その相手が、道具の棚の中の「打ち込む欄」か。
[[nodiscard]] bool IsTypingField(QObject* target)
{
    return dynamic_cast<QAbstractSpinBox*>(target) != nullptr
        || dynamic_cast<QLineEdit*>(target) != nullptr;
}

} // namespace

//! いま Enter と Esc を引き受ける道具が動いているか。
//! 押し出しと「面を作る」。どちらも右の棚で入力してから Enter で確定する。
bool V2MainWindow::ToolWantsConfirmKeys() const
{
    if (viewport_ == nullptr) {
        return false;
    }
    return !pendingCommandId_.empty() || viewport_->ExtrudeHandleShown()
        || surfaceShelfShown_ || approxShelfShown_ || booleanShelfShown_
        || thickenShelfShown_ || cornerPreviewShown_
        || ShelfShown(kachakacha::v2::app::Shelf::Array)
        || (surfaceEdit_ != nullptr && surfaceEdit_->Active())
        || OwnedToolShelf() != kachakacha::v2::app::Shelf::None;
}

bool V2MainWindow::HandleToolKey(int key, QObject* target)
{
    if (viewport_ == nullptr) {
        return false;
    }
    if (!pendingCommandId_.empty()) {
        if (key == Qt::Key_Escape) {
            const QString label = PendingCommandLabel();
            ClearPendingCommand();
            SetStatus(label + QStringLiteral(": やめました。"));
            return true;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            ConfirmPendingCommand();
            return true;
        }
    }
    if (viewport_->ExtrudeHandleShown()) {
        return HandleExtrudeToolKey(key, target);
    }
    if (surfaceShelfShown_) {
        return HandleSurfaceToolKey(key);
    }
    if (approxShelfShown_) {
        // 近似も同じ。打ち込む欄との取り合いは無い。Enter は確定、Esc はやめる。
        if (key == Qt::Key_Escape) {
            EndApprox();
            SetStatus(QStringLiteral("近似: やめました。何も作っていません。"));
            return true;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            ConfirmApprox();
            return true;
        }
    }
    if (cornerPreviewShown_ && HandleCornerToolKey(key)) {
        return true;
    }
    if (booleanShelfShown_) {
        if (key == Qt::Key_Escape) {
            EndBoolean();
            SetStatus(QStringLiteral("足す・引く: やめました。何も作っていません。"));
            return true;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            ConfirmBoolean();
            return true;
        }
    }
    if (thickenShelfShown_) {
        return HandleThickenToolKey(key, target);
    }
    if (surfaceEdit_ != nullptr && surfaceEdit_->Active()) {
        return surfaceEdit_->HandleKey(key);
    }
    // 立体を作る。角度の欄は打つたびに下見を作り直すので、打ちかけの取り合いは無い。
    if (solidTool_ != nullptr && solidTool_->Active()) {
        return solidTool_->HandleKey(key);
    }
    if (edgeFinishTool_ != nullptr && edgeFinishTool_->Active()) {
        return edgeFinishTool_->HandleKey(key);
    }
    if (ShelfShown(kachakacha::v2::app::Shelf::Array)) {
        // 配列の棚(D-23)。打ち込む欄との取り合いは無い。Enter は確定、Esc はやめる。
        if (key == Qt::Key_Escape) {
            EndArray();
            SetStatus(QStringLiteral("配列: やめました。"));
            return true;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            ConfirmArray();
            return true;
        }
    }
    return false;
}

//! 「厚み」の Enter / Esc。厚み(mm)の欄だけ、打ちかけの字を確かめる取り合いがある
//! (押し出しと同じ決まり。値が変わっていれば下見だけ作り直し、確定は次の Enter で)。
bool V2MainWindow::HandleThickenToolKey(int key, QObject* target)
{
    using kachakacha::v2::app::ToolKeyAction;
    kachakacha::v2::app::ToolKeyContext context;
    context.previewActive = true;
    if (key == Qt::Key_Escape) {
        if (kachakacha::v2::app::ActionForCancelKey(context) != ToolKeyAction::Cancel) {
            return false;
        }
        EndThicken();
        SetStatus(QStringLiteral("厚み: やめました。何も作っていません。"));
        return true;
    }
    if (key != Qt::Key_Return && key != Qt::Key_Enter) {
        return false;
    }
    context.inTypingField = IsTypingField(target);
    if (context.inTypingField) {
        // 打ちかけの字を、まず値にする。変わったかどうかは入力の厚みで見る。
        const double before = thickenInput_.thicknessMm;
        if (auto* spin = dynamic_cast<QAbstractSpinBox*>(target); spin != nullptr) {
            spin->interpretText();
        }
        context.valueChanged = thickenInput_.thicknessMm != before;
    }
    if (kachakacha::v2::app::ActionForConfirmKey(context) == ToolKeyAction::CommitValueAndWait) {
        SetStatus(QStringLiteral("厚み: %1 mm にしました。"
                                 "下見のとおりでよければ、もう一度 Enter で確定します。")
                .arg(thickenInput_.thicknessMm));
        return true;
    }
    ConfirmThicken();
    return true;
}

//! 「面を作る」の Enter / Esc(§12・§14)。
//!
//! ここには打ち込む欄との取り合いが無い。距離のような数値を打つ欄が無く、
//! 入力はどれも押して選ぶものだからである。Enter は素直に確定にする。
bool V2MainWindow::HandleSurfaceToolKey(int key)
{
    if (key == Qt::Key_Escape) {
        EndSurfacePreview();
        SetStatus(QStringLiteral("面を作る: やめました。何も作っていません。"));
        return true;
    }
    if (key != Qt::Key_Return && key != Qt::Key_Enter) {
        return false;
    }
    ConfirmSurface();
    return true;
}

bool V2MainWindow::HandleExtrudeToolKey(int key, QObject* target)
{
    using kachakacha::v2::app::ToolKeyAction;
    kachakacha::v2::app::ToolKeyContext context;
    context.previewActive = true;   // ここへ来た時点で動いている
    if (key == Qt::Key_Escape) {
        if (kachakacha::v2::app::ActionForCancelKey(context) != ToolKeyAction::Cancel) {
            return false;
        }
        EndExtrudePreview();
        SetStatus(QStringLiteral("押し出し: やめました。"));
        return true;
    }
    if (key != Qt::Key_Return && key != Qt::Key_Enter) {
        return false;
    }
    context.inTypingField = IsTypingField(target);
    if (context.inTypingField) {
        // 打ちかけの字を、まず値にする。変わったかどうかは下見の距離で見る。
        const double before = viewport_->ExtrudeHandleDistanceMm();
        if (auto* spin = dynamic_cast<QAbstractSpinBox*>(target); spin != nullptr) {
            spin->interpretText();
        }
        context.valueChanged = viewport_->ExtrudeHandleDistanceMm() != before;
    }
    if (kachakacha::v2::app::ActionForConfirmKey(context)
        == ToolKeyAction::CommitValueAndWait) {
        SetStatus(QStringLiteral("押し出し: %1 mm にしました。"
                                 "下見のとおりでよければ、もう一度 Enter で確定します。")
                .arg(viewport_->ExtrudeHandleDistanceMm()));
        return true;
    }
    ConfirmExtrude();
    return true;
}

bool V2MainWindow::eventFilter(QObject* target, QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (HandleToolKey(key->key(), target)) {
            return true;   // ここで受け止めた。欄や画面へは渡さない。
        }
    }
    return QMainWindow::eventFilter(target, event);
}
