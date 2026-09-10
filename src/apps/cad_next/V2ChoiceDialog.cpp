#include "V2ChoiceDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>

V2ChoiceDialog::V2ChoiceDialog(const QString& title, const QString& label,
    const QStringList& items, int initialIndex, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    items_ = new QComboBox(this);
    for (const QString& item : items) {
        items_->addItem(item);
    }
    if (initialIndex >= 0 && initialIndex < items.size()) {
        items_->setCurrentIndex(initialIndex);
    }
    form->addRow(label, items_);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

int V2ChoiceDialog::ChosenIndex() const
{
    return items_->currentIndex();
}

void V2ChoiceDialog::SetChosenIndex(int index)
{
    items_->setCurrentIndex(index);
}
