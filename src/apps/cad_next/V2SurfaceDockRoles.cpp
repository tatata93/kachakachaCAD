//! 「面を作る」の棚の、おまかせの段と一覧の行の役割(プロンプト beginner_workflow)。
//! V2SurfaceDock.h の頭の注記を見よ。決め方は core(app/SurfaceRoleAssist)。

#include "V2SurfaceDock.h"

#include "kachakacha/app/SurfaceRoleAssist.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstddef>

using kachakacha::v2::app::WireRoleChoice;
using kachakacha::v2::modeling::ChainRole;

void V2SurfaceDock::BuildRoleAssist(QVBoxLayout* layout)
{
    layout->addWidget(new QLabel(QStringLiteral("おまかせ(線のつながりから役割と作り方を決める)"),
        widget()));
    assist_ = new QLabel(widget());
    assist_->setWordWrap(true);
    layout->addWidget(assist_);
    resume_ = new QPushButton(QStringLiteral("おまかせに戻す"), widget());
    resume_->setToolTip(QStringLiteral("作り方を選ぶ前の状態に戻し、線のつながりから役割と作り方を決め直します"));
    QObject::connect(resume_, &QPushButton::clicked, this, [this] {
        if (!loading_ && resumeHandler_) {
            resumeHandler_();
        }
    });
    layout->addWidget(resume_);
    for (std::size_t index = 0; index < candidates_.size(); ++index) {
        auto* button = new QPushButton(widget());
        button->setToolTip(QStringLiteral("その作り方の役割で入れ直します(作り方はあなたが選んだことになります)"));
        QObject::connect(button, &QPushButton::clicked, this, [this, index] {
            if (!loading_ && candidateHandler_) {
                candidateHandler_(static_cast<int>(index));
            }
        });
        button->setVisible(false);
        candidates_[index] = button;
        layout->addWidget(button);
    }
}

void V2SurfaceDock::ShowRoleAssist(bool autoRoles, const std::vector<QString>& linesJa,
    const std::vector<std::pair<QString, bool>>& candidates)
{
    QString text;
    for (const QString& line : linesJa) {
        text += (text.isEmpty() ? QString() : QStringLiteral("\n")) + line;
    }
    assist_->setText(text);
    resume_->setVisible(!autoRoles);
    for (std::size_t index = 0; index < candidates_.size(); ++index) {
        QPushButton* button = candidates_[index];
        const bool shown = autoRoles && index < candidates.size();
        button->setVisible(shown);
        if (shown) {
            button->setText(QStringLiteral("この作り方にする: ") + candidates[index].first);
            // 成り立たない候補は押せない形にする(押せるのに作れない、にしない)。
            button->setEnabled(candidates[index].second);
        }
    }
}

void V2SurfaceDock::SetRoleAssistHandlers(std::function<void()> resume,
    std::function<void(int)> useCandidate)
{
    resumeHandler_ = std::move(resume);
    candidateHandler_ = std::move(useCandidate);
}

void V2SurfaceDock::SetEntryRoleHandler(
    std::function<void(ChainRole, int, WireRoleChoice)> handler)
{
    entryRoleHandler_ = std::move(handler);
}

//! 一覧で選んだ行の役割を、役割の選び肢に映す(自動なら「自動」)。
void V2SurfaceDock::RefreshEntryRole(int index)
{
    if (index < 0 || index >= static_cast<int>(slots_.size())) {
        return;
    }
    SlotRow& row = slots_[static_cast<std::size_t>(index)];
    if (row.role == nullptr || row.list == nullptr) {
        return;
    }
    const std::vector<WireRoleChoice>* roles = nullptr;
    switch (row.key) {
    case ChainRole::Section:    roles = &shownNames_.sectionRoles; break;
    case ChainRole::GuideU:     roles = &shownNames_.guideRoles; break;
    case ChainRole::Centerline: roles = &shownNames_.centerlineRoles; break;
    default:                    roles = &shownNames_.boundaryRoles; break;
    }
    const int current = row.list->currentItem() == nullptr
        ? -1 : row.list->indexOfTopLevelItem(row.list->currentItem());
    WireRoleChoice role = WireRoleChoice::Auto;
    if (current >= 0 && current < static_cast<int>(roles->size())) {
        role = (*roles)[static_cast<std::size_t>(current)];
    }
    const auto& choices = kachakacha::v2::app::WireRoleChoices();
    const auto found = std::find(choices.begin(), choices.end(), role);
    const bool wasLoading = loading_;
    loading_ = true;
    row.role->setCurrentIndex(found == choices.end() ? 0 : static_cast<int>(found - choices.begin()));
    row.role->setEnabled(current >= 0);
    loading_ = wasLoading;
}

bool V2SurfaceDock::ChooseEntryRole(ChainRole slot, int row, WireRoleChoice role)
{
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        SlotRow& found = slots_[index];
        if (found.key != slot || found.list == nullptr || found.role == nullptr) {
            continue;
        }
        if (!found.list->isVisible() || !found.role->isVisible() || row < 0
            || row >= found.list->topLevelItemCount()) {
            return false;
        }
        found.list->setCurrentItem(found.list->topLevelItem(row));
        RefreshEntryRole(static_cast<int>(index));
        const auto& choices = kachakacha::v2::app::WireRoleChoices();
        const auto at = std::find(choices.begin(), choices.end(), role);
        if (at == choices.end() || !found.role->isEnabled()) {
            return false;
        }
        const int wanted = static_cast<int>(at - choices.begin());
        if (found.role->currentIndex() == wanted) {
            // 同じ役割を選び直した(変わらない)。人が選んだことだけを伝える。
            if (entryRoleHandler_) {
                entryRoleHandler_(slot, row, role);
            }
            return true;
        }
        found.role->setCurrentIndex(wanted);
        return true;
    }
    return false;
}

bool V2SurfaceDock::ClickResumeAuto()
{
    if (resume_ == nullptr || !resume_->isVisible() || !resume_->isEnabled()) {
        return false;
    }
    resume_->click();
    return true;
}

bool V2SurfaceDock::ClickCandidate(int index)
{
    if (index < 0 || index >= static_cast<int>(candidates_.size())) {
        return false;
    }
    QPushButton* button = candidates_[static_cast<std::size_t>(index)];
    if (button == nullptr || !button->isVisible() || !button->isEnabled()) {
        return false;
    }
    button->click();
    return true;
}

QString V2SurfaceDock::RoleAssistTextJa() const
{
    return assist_ == nullptr ? QString() : assist_->text();
}
