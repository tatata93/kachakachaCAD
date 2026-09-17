//! 構えた命令(オーナー指摘 2026-09-11「ツールを選ぶ→対象を選択の順で行う」)。
//!
//! V2 は逆だった。トリムを押すと、その場で「線を2本選んでください」と言って
//! **終わってしまう**。選んでから押し直さなければならない。
//! 道具を構えたまま待ってくれないので、押す順を覚えていないと使えない。
//!
//! ここでは、条件を満たしていない命令を **構えて待つ**。
//! 相手がそろったら走る。どのタイミングで走るかは core(app/ToolTargeting)が決める:
//!   - 「ちょうど2本」のような条件 → 2本目を選んだ時点で走る(足す余地がない)
//!   - 「1本以上」のような条件 → そろっても待つ。`Enter` で走る
//!     (1本目で走らせると、2本目を選ぶ前に終わってしまう)
//!
//! **構えは必ず見える。** 帯に「トリム: 線を2本選んでください(選ぶと続きます)」と出る。
//! 黙って待つと、押したのに何も起きなかったようにしか見えない。

#include "V2MainWindow.h"

#include "V2Viewport.h"

#include "kachakacha/app/ToolTargeting.h"

#include <QString>

#include <string>

using kachakacha::v2::app::PendingAction;
using kachakacha::v2::app::PendingCommandAction;
using kachakacha::v2::app::PredicateCanBeSatisfiedBySelection;

bool V2MainWindow::ArmCommandIfUnsatisfied(std::string_view id)
{
    const auto* command = kachakacha::v2::app::FindCommand(id);
    if (command == nullptr) {
        return true;   // 知らない命令。ここで止める。
    }
    QString reason;
    if (CommandEnabled(id, &reason)) {
        return false;   // いま使える。そのまま走らせる。
    }
    if (!PredicateCanBeSatisfiedBySelection(command->predicate)) {
        // 選んで直るものではない(戻せる履歴が無い、など)。理由を出して終わる。
        SetStatus(reason);
        return true;
    }
    // 選べば満たせる。構えて待つ。
    pendingCommandId_ = std::string(id);
    // 押し出しを構えているなら、拾う候補も押し出しが求めるものを前へ出す(§6)。
    if (id == "part.extrude") {
        viewport_->SetPickSlot(kachakacha::v2::app::ExtrudeSlot::Profile);
        // 構えている間はずっと、素のクリックで役割の違うものを足せる(§5)。
        viewport_->SetToolPickActive(true);
        // 線を1本ずつ拾わせず、閉じた線の内側を押し出し輪郭として拾う。
        viewport_->SetProfileRegionPicking(true);
    } else if (id == "surface.create") {
        viewport_->SetToolPickActive(true);
        viewport_->SetProfileRegionPicking(true);
    }
    SetStatus(QStringLiteral("%1: %2(選ぶと続きます。Esc でやめます)")
            .arg(QString::fromUtf8(std::string(command->labelJa).c_str()),
                QString::fromUtf8(std::string(command->predicateFailureJa).c_str())));
    RefreshCommandVisibility();
    return true;
}

void V2MainWindow::ClearPendingCommand()
{
    // 構えを解いたら、拾い方もふだんへ戻す。
    // ただし下見が出ている間は、押し出しが続いているので戻さない。
    if (pendingCommandId_ == "part.extrude" && viewport_ != nullptr
        && !viewport_->ExtrudeHandleShown()) {
        viewport_->SetPickSlot(kachakacha::v2::app::ExtrudeSlot::None);
        viewport_->SetToolPickActive(false);
        viewport_->SetProfileRegionPicking(false);
    } else if (pendingCommandId_ == "surface.create" && viewport_ != nullptr
        && !surfaceShelfShown_) {
        viewport_->SetToolPickActive(false);
        viewport_->SetProfileRegionPicking(false);
    }
    pendingCommandId_.clear();
}

QString V2MainWindow::PendingCommandLabel() const
{
    if (pendingCommandId_.empty()) {
        return QString();
    }
    const auto* command = kachakacha::v2::app::FindCommand(pendingCommandId_);
    return command == nullptr
        ? QString()
        : QString::fromUtf8(std::string(command->labelJa).c_str());
}

void V2MainWindow::RefreshPendingCommand(bool confirmed)
{
    if (pendingCommandId_.empty()) {
        return;
    }
    const auto* command = kachakacha::v2::app::FindCommand(pendingCommandId_);
    if (command == nullptr) {
        ClearPendingCommand();
        return;
    }
    if (pendingCommandId_ == "part.extrude") {
        // 構えている間も、拾い方は「いま足りないもの」に合わせて動かす(§6)。
        RefreshExtrudePickSlot();
    }
    QString reason;
    const bool satisfied = CommandEnabled(pendingCommandId_, &reason);
    const PendingAction action =
        PendingCommandAction(command->predicate, satisfied, confirmed);
    const QString label = QString::fromUtf8(std::string(command->labelJa).c_str());
    if (action == PendingAction::Wait) {
        SetStatus(QStringLiteral("%1: %2(選ぶと続きます。Esc でやめます)")
                .arg(label, QString::fromUtf8(std::string(command->predicateFailureJa).c_str())));
        return;
    }
    if (action == PendingAction::NeedsConfirm) {
        // そろったが、まだ足せる。数を言って待つ。
        SetStatus(QStringLiteral("%1: %2個選びました。Enter で実行、続けて選んでも構いません。")
                .arg(label)
                .arg(static_cast<int>(viewport_->Selection().entityIds.size())));
        return;
    }
    // 走らせる前に構えを解く。走った先で選択が変わっても、二度走らないようにする。
    const std::string id = pendingCommandId_;
    ClearPendingCommand();
    RunCommand(id);
}

void V2MainWindow::ConfirmPendingCommand()
{
    if (pendingCommandId_.empty()) {
        return;
    }
    RefreshPendingCommand(true);
}
