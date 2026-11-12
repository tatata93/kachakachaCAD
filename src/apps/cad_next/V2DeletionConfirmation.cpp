#include "V2DeletionConfirmation.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QObject>
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

int v2ui::ConfirmDeletion(QWidget* parent, const QStringList& names, bool hasFolders,
    const QStringList* withBoundaries, const std::function<void(bool)>& preview)
{
    bool keepContents = false;
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("confirmDependentDelete"));
    dialog.setWindowTitle(withBoundaries?QStringLiteral("削除するものを確認"):QStringLiteral("使っているものも削除"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(!hasFolders
        ? QStringLiteral("この要素を使っているもの、または同じ操作の出力があります。\n次の要素が消えます。削除しますか？\n3Dビューの赤色が削除対象です。")
        : QStringLiteral("グループと中の要素を削除しますか？\n中身を残してグループだけ解除することもできます。\n3Dビューの赤色が削除対象です。"), &dialog));
    auto* list = new QListWidget(&dialog);
    list->addItems(names);
    list->setMinimumSize(330, 160);
    layout->addWidget(list);
    QCheckBox* include=nullptr;
    if(withBoundaries) {
        include=new QCheckBox(QStringLiteral("同時に生成した境界ワイヤーも削除"),&dialog);
        include->setObjectName(QStringLiteral("deleteGeneratedBoundaries"));
        layout->addWidget(include);
        QObject::connect(include,&QCheckBox::toggled,&dialog,[&](bool checked){
            list->clear();list->addItems(checked?*withBoundaries:names);
            if(preview)preview(checked);
        });
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto* confirm = buttons->addButton(QStringLiteral("一覧の要素を削除"), QDialogButtonBox::AcceptRole);
    confirm->setObjectName(QStringLiteral("confirmDelete"));
    if (hasFolders) {
        auto* keep = buttons->addButton(QStringLiteral("グループを解除（中身を残す）"), QDialogButtonBox::ActionRole);
        keep->setObjectName(QStringLiteral("keepGroupContents"));
        QObject::connect(keep, &QPushButton::clicked, &dialog, [&] { keepContents = true; dialog.accept(); });
    }
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return -1;
    return keepContents ? 1 : include&&include->isChecked() ? 2 : 0;
}
