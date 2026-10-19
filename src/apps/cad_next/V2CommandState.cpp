#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "kachakacha/app/CommandAvailability.h"
#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/app/ToolTargeting.h"
#include <QAction>
#include <QElapsedTimer>
#include <QString>
#include <QVariant>
using namespace kachakacha::v2::app;
void V2MainWindow::RefreshCommandVisibility()
{
    QElapsedTimer timer;timer.start();
    const auto facts=BuildFactsForCommands();
    const bool extrudeReady=PlanExtrudeFromSelection().readyToPreview;
    for (const auto& entry : commandActions_) {
        const bool visible = CommandVisibleInMode(entry.first, mode_);
        entry.second->setVisible(visible);
        QString reason;
        const CommandDescriptor* command = FindCommand(entry.first);
        const bool ready=command&&((entry.first=="edit.delete"&&SelectedGroupId().has_value())||(entry.first=="part.extrude"?extrudeReady:
            kachakacha::v2::app::SelectionSatisfies(command->predicate,facts)));
        if(!ready&&command)reason=QString::fromUtf8(std::string(command->predicateFailureJa).c_str());
        const bool canSelectTarget = command != nullptr
            && kachakacha::v2::app::PredicateCanBeSatisfiedBySelection(
                command->predicate);
        const bool enabled = ready || canSelectTarget;
        entry.second->setProperty("toolAvailable",enabled);
        entry.second->setEnabled(enabled);
        entry.second->setToolTip(command ? QString::fromUtf8(std::string(command->operationGuideJa).c_str()) : QString());
        if (!ready && !reason.isEmpty()) {
            // 対象不足なら入口は有効のまま、選ぶ対象をツールチップで示す。
            const QString suffix = canSelectTarget
                ? QStringLiteral(" 道具を開始してから対象を選べます。")
                : QString();
            entry.second->setToolTip(reason + suffix);
        }
    }
    // 固有の作図道具は作図モードだけ。ほかのモードは台帳 QAction の専用列を使う。
    for (QAction* action : toolActions_) {
        action->setProperty("toolAvailable",true);
        action->setVisible(mode_ == UiMode::Drawing);
    }
    setProperty("commandRefreshMs",timer.nsecsElapsed()/1e6);
}

kachakacha::v2::app::SelectionFacts V2MainWindow::BuildFactsForCommands() const
{
    kachakacha::v2::app::ExternalCounts external;
    // 部材も型紙も文書には入らない。画面が覚えているので、そこから渡す。
    external.fabricationModels = static_cast<int>(fabricationModels_.size());
    external.fabricationPanels = static_cast<int>(fabricationPanels_.size());
    external.patterns = static_cast<int>(patternPages_.size());
    // 役割表も画面が持つ。行を選んでいるかで、行に効くコマンドが押せるかが決まる。
    external.guideRows = static_cast<int>(guideTable_.rows.size());
    external.selectedGuideRows = CurrentGuideRow().has_value() ? 1 : 0;
    const kachakacha::v2::app::SelectionSet empty;
    return kachakacha::v2::app::BuildSelectionFacts(
        viewport_ == nullptr ? empty : viewport_->Selection(),
        session_->GetDocument().Snapshot(), session_->Scene(),
        session_->GetDocument().Snapshot().settings.tolerance, external,
        session_->HasPlacedPoints() || session_->GetDocument().CanUndo(), session_->GetDocument().CanRedo());
}

bool V2MainWindow::CommandEnabled(std::string_view id, QString* reasonOut) const
{
    const CommandDescriptor* command = FindCommand(id);
    if (command == nullptr) {
        if (reasonOut != nullptr) {
            *reasonOut = QStringLiteral("知らないコマンドです。");
        }
        return false;
    }
    // 判断は core にある。ここで数えると、画面を出さないと確かめられなくなる。
    const bool ok = (id=="edit.delete"&&SelectedGroupId().has_value()) || (id == "part.extrude" ? PlanExtrudeFromSelection().readyToPreview
        : kachakacha::v2::app::SelectionSatisfies(command->predicate,
        BuildFactsForCommands()));
    if (!ok && reasonOut != nullptr) {
        *reasonOut = QString::fromUtf8(
            std::string(command->predicateFailureJa).c_str());
    }
    return ok;
}
