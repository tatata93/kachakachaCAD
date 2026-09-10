#include "V2NumberDialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>

V2NumberDialog::V2NumberDialog(const QString& title, const QString& label, double initial,
    double minimum, double maximum, const QString& suffix, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    value_ = new QDoubleSpinBox(this);
    value_->setRange(minimum, maximum);
    value_->setDecimals(2);
    value_->setSingleStep(5.0);
    value_->setSuffix(suffix);
    value_->setValue(initial);
    form->addRow(label, value_);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

double V2NumberDialog::Value() const
{
    return value_->value();
}

void V2NumberDialog::SetValue(double value)
{
    value_->setValue(value);
}
