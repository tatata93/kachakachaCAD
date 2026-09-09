#include "V2WorkPlaneDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace {

[[nodiscard]] QString Text(std::string_view value)
{
    return QString::fromUtf8(std::string(value).c_str());
}

} // namespace

V2WorkPlaneDialog::V2WorkPlaneDialog(const WorkPlaneChoice& initial,
    const kachakacha::v2::app::WorkPlaneFacts& facts, QWidget* parent)
    : QDialog(parent)
    , facts_(facts)
{
    setWindowTitle(QStringLiteral("作業平面を作る"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    method_ = new QComboBox(this);
    for (const auto method : kachakacha::v2::app::WorkPlaneMethods()) {
        method_->addItem(
            Text(kachakacha::v2::modeling::WorkPlaneMethodNameJa(method)));
    }
    form->addRow(QStringLiteral("作り方"), method_);

    standard_ = new QComboBox(this);
    standard_->addItem(QStringLiteral("XY(上から見る面)"));
    standard_->addItem(QStringLiteral("YZ(横から見る面)"));
    standard_->addItem(QStringLiteral("ZX(前から見る面)"));
    standard_->setCurrentIndex(static_cast<int>(initial.standard));
    form->addRow(QStringLiteral("標準面"), standard_);

    offset_ = new QDoubleSpinBox(this);
    offset_->setRange(-100000.0, 100000.0);
    offset_->setDecimals(3);
    offset_->setSingleStep(1.0);
    offset_->setSuffix(QStringLiteral(" mm"));
    offset_->setValue(initial.offsetMm);
    form->addRow(QStringLiteral("離す距離"), offset_);

    angle_ = new QDoubleSpinBox(this);
    angle_->setRange(-360.0, 360.0);
    angle_->setDecimals(3);
    angle_->setSingleStep(1.0);
    angle_->setSuffix(QStringLiteral(" 度"));
    angle_->setValue(initial.angleDeg);
    form->addRow(QStringLiteral("回す角度"), angle_);

    layout->addLayout(form);
    needs_ = new QLabel(this);
    needs_->setWordWrap(true);
    layout->addWidget(needs_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    QObject::connect(method_, &QComboBox::currentIndexChanged, this,
        [this] { Refresh(); });
    // 初期値の作り方を選んでおく。
    const auto& methods = kachakacha::v2::app::WorkPlaneMethods();
    for (std::size_t index = 0; index < methods.size(); ++index) {
        if (methods[index] == initial.method) {
            method_->setCurrentIndex(static_cast<int>(index));
            break;
        }
    }
    Refresh();
}

WorkPlaneChoice V2WorkPlaneDialog::Choice() const
{
    WorkPlaneChoice choice;
    const auto& methods = kachakacha::v2::app::WorkPlaneMethods();
    const int index = method_->currentIndex();
    if (index >= 0 && index < static_cast<int>(methods.size())) {
        choice.method = methods[static_cast<std::size_t>(index)];
    }
    choice.standard = static_cast<kachakacha::v2::modeling::StandardPlaneKind>(
        standard_->currentIndex() < 0 ? 0 : standard_->currentIndex());
    choice.offsetMm = offset_->value();
    choice.angleDeg = angle_->value();
    return choice;
}

void V2WorkPlaneDialog::SetMethodIndex(int index)
{
    method_->setCurrentIndex(index);
    Refresh();
}

bool V2WorkPlaneDialog::CurrentChoiceIsValid(QString* reasonOut) const
{
    const auto checked =
        kachakacha::v2::app::ValidateWorkPlaneChoice(Choice().method, facts_);
    if (checked.HasValue()) {
        return true;
    }
    if (reasonOut != nullptr && !checked.Diagnostics().empty()) {
        *reasonOut = QString::fromStdString(checked.Diagnostics().front().detailsJa);
    }
    return false;
}

void V2WorkPlaneDialog::Refresh()
{
    const WorkPlaneChoice choice = Choice();
    const auto needs = kachakacha::v2::app::NeedsOf(choice.method);
    // 使わない欄は出さない。出したままにすると、入れた値が効くのか分からない。
    standard_->setEnabled(needs.usesStandardKind);
    offset_->setEnabled(needs.usesOffset);
    angle_->setEnabled(needs.usesAngle);
    QString reason;
    if (CurrentChoiceIsValid(&reason)) {
        needs_->setText(QStringLiteral("いまの選択で作れます(%1)。")
                .arg(QString::fromStdString(
                    kachakacha::v2::app::WorkPlaneNeedsJa(choice.method))));
        return;
    }
    // 押してから断らない。いま何が足りないかを、その場で出す。
    needs_->setText(QStringLiteral("このままでは作れません: %1").arg(reason));
}
