#pragma once

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <functional>

// A role owns its inputs; switching the active role never clears another role.
class V2RoleInput final : public QWidget {
public:
    explicit V2RoleInput(QWidget* parent, const QString& title) : QWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 2, 0, 2);
        layout->setSpacing(3);
        name = new QLabel(title, this);
        auto font = name->font(); font.setBold(true); name->setFont(font);
        layout->addWidget(name);
        value = new QLabel(this); value->setWordWrap(true); layout->addWidget(value);
        auto* buttons = new QHBoxLayout();
        arm = new QPushButton(QStringLiteral("選ぶ・追加"), this); arm->setCheckable(true);
        clear = new QPushButton(QStringLiteral("全解除"), this);
        buttons->addWidget(arm, 1); buttons->addWidget(clear);
        layout->addLayout(buttons);
        list = new QListWidget(this);
        list->setMaximumHeight(66); list->setMinimumHeight(38);
        list->hide();
        list->setToolTip(QStringLiteral("解除する項目を選んで「この項目を外す」。他の役割は保持します。"));
        layout->addWidget(list);
        remove = new QPushButton(QStringLiteral("この項目を外す"), this);
        remove->setEnabled(false); layout->addWidget(remove);
        remove->hide();
        QObject::connect(list, &QListWidget::currentRowChanged, this,
            [this](int row) { remove->setEnabled(row >= 0); });
        QObject::connect(remove, &QPushButton::clicked, this,
            [this] { const int row = list->currentRow(); if (row >= 0 && removeItem) removeItem(row); });
        QObject::connect(arm, &QPushButton::toggled, this, [this](bool active) {
            arm->setText(active ? QStringLiteral("選択中") : QStringLiteral("選ぶ・追加"));
            arm->setToolTip(QStringLiteral("この役割の対象を選びます。他の役割の入力は保持します。"));
        });
    }
    void SetItems(const QStringList& items) {
        list->setVisible(items.size() > 1);
        remove->setVisible(items.size() > 1);
        clear->setEnabled(!items.empty());
        QStringList current;
        for (int i = 0; i < list->count(); ++i) current.push_back(list->item(i)->text());
        if (current == items) return;
        const int row = list->currentRow();
        list->clear(); list->addItems(items);
        if (row >= 0 && row < list->count()) list->setCurrentRow(row);
    }
    QLabel* name = nullptr;
    QLabel* value = nullptr;
    QPushButton* arm = nullptr;
    QPushButton* clear = nullptr;
    QListWidget* list = nullptr;
    QPushButton* remove = nullptr;
    std::function<void(int)> removeItem;
};
