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
#include "V2OperationPanelHost.h"
#include "V2RoleInput.h"
#include <QDockWidget>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QStringList>
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "kachakacha/app/ToolTargeting.h"

#include <QString>

#include <string>
#include <algorithm>

using kachakacha::v2::app::PendingAction;
using kachakacha::v2::app::PendingCommandAction;
using kachakacha::v2::app::PredicateCanBeSatisfiedBySelection;

namespace {
constexpr auto pendingPageName = "pendingTargetPage";
void ShowPendingTargets(V2MainWindow& window, V2OperationPanelHost& host,
    const kachakacha::v2::app::CommandDescriptor& command, bool satisfied)
{
    auto* page = host.TemporaryPage();
    if (!page || page->objectName() != pendingPageName) {
        page = new QWidget(&host);
        page->setObjectName(pendingPageName);
        auto* layout = new QVBoxLayout(page);
        auto* hint = new QLabel(QString::fromUtf8(std::string(command.predicateFailureJa).c_str())
            + QStringLiteral("。3Dビューまたは左ペインから順に選んでください。"), page);
        hint->setWordWrap(true); layout->addWidget(hint);
        auto* role = new V2RoleInput(page, QStringLiteral("加工する対象（選択順）"));
        role->setObjectName(QStringLiteral("pendingTargetRole"));
        layout->addWidget(role);
        role->removeItem = [&window](int row) {
            auto selection = window.Viewport().Selection();
            if (row < 0 || row >= static_cast<int>(selection.entityIds.size())) return;
            const auto id = selection.entityIds[row];
            std::erase(selection.entityIds, id);
            std::erase_if(selection.ordered, [id](const auto& ref) { return ref.entityId == id; });
            window.Viewport().SetSelection(std::move(selection));
        };
        QObject::connect(role->arm, &QPushButton::clicked, page, [&window] {
            window.Viewport().setFocus();
        });
        QObject::connect(role->clear, &QPushButton::clicked, page, [&window] {
            window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
        });
        layout->addStretch(1);
        auto* actions = new QHBoxLayout;
        auto* cancel = new QPushButton(QStringLiteral("取消 Esc"), page);
        auto* confirm = new QPushButton(QStringLiteral("確定 Enter"), page);
        confirm->setObjectName(QStringLiteral("pendingTargetConfirm"));
        actions->addWidget(cancel); actions->addWidget(confirm); layout->addLayout(actions);
        QObject::connect(cancel, &QPushButton::clicked, page, [&window] {
            window.HandleToolKey(Qt::Key_Escape, nullptr);
        });
        QObject::connect(confirm, &QPushButton::clicked, page, [&window] {
            window.ConfirmPendingCommand();
        });
        host.ShowTemporaryPage(page, QString::fromUtf8(std::string(command.labelJa).c_str()));
    }
    QStringList names;
    for (const auto& id : window.Viewport().Selection().entityIds)
        if (const auto* entity = window.Session().GetDocument().FindEntity(id))
            names.push_back(QString::fromStdString(entity->displayName));
    for (auto* widget : page->findChildren<QWidget*>())
        if (auto* role = dynamic_cast<V2RoleInput*>(widget)) {
            role->value->setText(names.empty() ? QStringLiteral("未選択") : names.join(QStringLiteral("、")));
            role->SetItems(names);
        }
    page->findChild<QPushButton*>(QStringLiteral("pendingTargetConfirm"))->setEnabled(satisfied);
}
}

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
    // A shelf command must stop the old drawing tool before collecting its targets.
    // Dedicated surface/extrusion tools already own the input shelf and picking state.
    if (id != "part.extrude" && id != "surface.create")
        SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    // 選べば満たせる。構えて待つ。
    pendingCommandId_ = std::string(id);
    if (IsFabricationCommand(id) || IsFreezeCommand(id)) FocusFabricationStageFor(id);
    // 押し出しを構えているなら、拾う候補も押し出しが求めるものを前へ出す(§6)。
    if (id == "part.extrude") {
        viewport_->SetPickSlot(kachakacha::v2::app::ExtrudeSlot::Profile);
        // 構えている間はずっと、素のクリックで役割の違うものを足せる(§5)。
        viewport_->SetToolPickActive(true);
        // 線を1本ずつ拾わせず、閉じた線の内側を押し出し輪郭として拾う。
        viewport_->SetProfileRegionPicking(true);
        // 入力が空でも押し出し専用棚を先に見せる。
        ShowExtrudeShelf(PlanExtrudeFromSelection());
    } else if (id == "surface.create") {
        viewport_->SetToolPickActive(true);
        // 線の上を押せば線 1 本、線の無い内側を押せば輪郭をまとめて拾う。
        viewport_->SetProfileRegionPicking(true, true);
    }
    if (id != "part.extrude" && id != "surface.create" && operationHost_
        && !IsFabricationCommand(id) && !IsFreezeCommand(id) && !partDock_->ToolActive()) {
        viewport_->SetToolPickActive(true);
        viewport_->SetToolPickToggle(true);
        ShowPendingTargets(*this, *operationHost_, *command, false);
        operationDock_->show(); operationDock_->raise();
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
        viewport_->CancelReferencePick();
        viewport_->SetPickSlot(kachakacha::v2::app::ExtrudeSlot::None);
        viewport_->SetToolPickActive(false);
        viewport_->SetProfileRegionPicking(false);
        extrudeShelfShown_ = false;
        ShowToolFooter(QString());
        RefreshRightShelves();
    } else if (pendingCommandId_ == "surface.create" && viewport_ != nullptr
        && !surfaceShelfShown_) {
        viewport_->SetToolPickActive(false);
        viewport_->SetProfileRegionPicking(false);
    }
    const bool genericPage = !pendingCommandId_.empty() && operationHost_ && operationHost_->TemporaryPage()
        && operationHost_->TemporaryPage()->objectName() == pendingPageName;
    if (genericPage) {
        operationHost_->ClearTemporaryPage();
        viewport_->SetToolPickActive(false);
        viewport_->SetToolPickToggle(false);
    }
    pendingCommandId_.clear();
    if (genericPage) RefreshRightShelves();
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
        ShowExtrudeShelf(PlanExtrudeFromSelection());
    }
    QString reason;
    const bool satisfied = CommandEnabled(pendingCommandId_, &reason);
    if (pendingCommandId_ != "part.extrude" && pendingCommandId_ != "surface.create" && operationHost_
        && !IsFabricationCommand(pendingCommandId_) && !IsFreezeCommand(pendingCommandId_) && !partDock_->ToolActive())
        ShowPendingTargets(*this, *operationHost_, *command, satisfied);
    if (partDock_ != nullptr && partDock_->ToolActive() && !confirmed) {
        SetStatus(satisfied ? QStringLiteral("対象を選びました。条件を確認して下端で確定してください。") : reason);
        return;
    }
    const PendingAction action =
        PendingCommandAction(command->predicate, satisfied, confirmed);
    const QString label = QString::fromUtf8(std::string(command->labelJa).c_str());
    if (action == PendingAction::Wait) {
        SetStatus(QStringLiteral("%1: %2(選ぶと続きます。Esc でやめます)")
                .arg(label, QString::fromUtf8(std::string(command->predicateFailureJa).c_str())));
        return;
    }
    if (action == PendingAction::NeedsConfirm) {
        if (pendingCommandId_ == "part.extrude") {
            // 最初の有効な輪郭が入った時点で下見を出す。下見中も追加クリックを
            // 受け付けるため、複数輪郭を選ぶ余地は失われない。
            const std::string id = pendingCommandId_;
            ClearPendingCommand();
            RunCommand(id);
            return;
        }
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
