#include "V2PanelFrame.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QWidget>

QLabel* MakePanelSectionTitle(QWidget* parent, const QString& text)
{
    auto* label = new QLabel(text, parent);
    StylePanelSectionTitle(label);
    return label;
}

void StylePanelSectionTitle(QLabel* label)
{
    if (label == nullptr) {
        return;
    }
    label->setObjectName(QStringLiteral("panelSectionTitle"));
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    label->setWordWrap(true);
}

QHBoxLayout* MakeCancelConfirmRow(QWidget* parent, QPushButton** cancel, QPushButton** confirm)
{
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    auto* cancelButton = new QPushButton(parent);
    auto* confirmButton = new QPushButton(parent);
    MarkCancelConfirm(cancelButton, confirmButton);
    row->addWidget(cancelButton);
    row->addStretch(1);
    row->addWidget(confirmButton);
    if (cancel != nullptr) {
        *cancel = cancelButton;
    }
    if (confirm != nullptr) {
        *confirm = confirmButton;
    }
    return row;
}

void MarkCancelConfirm(QPushButton* cancel, QPushButton* confirm)
{
    if (cancel != nullptr) {
        cancel->setObjectName(QStringLiteral("panelCancel"));
        cancel->setText(QStringLiteral("キャンセル Esc"));
        cancel->setToolTip(QStringLiteral("この道具をやめます(Esc と同じ)。"));
    }
    if (confirm != nullptr) {
        confirm->setObjectName(QStringLiteral("panelConfirm"));
        // 字は正本の「確定」に Enter を添える。「製作モデルを作る(確定 Enter)」のように
        // 何を作るかを言っている字はそのまま残す。
        if (confirm->text().isEmpty() || confirm->text() == QStringLiteral("確定")) {
            confirm->setText(QStringLiteral("確定 Enter"));
        }
        if (confirm->toolTip().isEmpty()) {
            confirm->setToolTip(QStringLiteral("いまの入力で作ります(Enter と同じ)。"));
        }
    }
}
