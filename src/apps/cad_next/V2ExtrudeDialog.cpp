#include "V2ExtrudeDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QObject>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace {

using kachakacha::v2::app::ExtentUsesDistance;
using kachakacha::v2::app::ExtentUsesSecondDistance;
using kachakacha::v2::app::ExtentUsesTarget;

[[nodiscard]] QString Text(std::string_view value)
{
    return QString::fromUtf8(std::string(value).c_str());
}

//! 並びの何番目か。見つからなければ 0。
template <typename Mode>
[[nodiscard]] int IndexOf(const std::vector<Mode>& modes, Mode mode)
{
    for (std::size_t i = 0; i < modes.size(); ++i) {
        if (modes[i] == mode) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

} // namespace

int V2ExtrudeDialog::DirectionIndexOf(
    kachakacha::v2::modeling::ExtrudeDirectionMode mode)
{
    return IndexOf(kachakacha::v2::app::ExtrudeDirections(), mode);
}

int V2ExtrudeDialog::ExtentIndexOf(kachakacha::v2::modeling::ExtrudeExtentMode mode)
{
    return IndexOf(kachakacha::v2::app::ExtrudeExtents(), mode);
}

int V2ExtrudeDialog::BooleanIndexOf(kachakacha::v2::modeling::ExtrudeBooleanMode mode)
{
    return IndexOf(kachakacha::v2::app::ExtrudeBooleans(), mode);
}

void V2ExtrudeDialog::BuildDirectionRows(
    QFormLayout* form, const kachakacha::v2::app::ExtrudeChoice& initial)
{
    direction_ = new QComboBox(this);
    for (const auto mode : kachakacha::v2::app::ExtrudeDirections()) {
        direction_->addItem(Text(kachakacha::v2::app::ExtrudeDirectionNameJa(mode)));
    }
    // いま効いている決め方を出す。出さないと、窓を開いて何も触らずに確定した
    // だけで、向きが並びの先頭(作業平面に垂直)へ黙って変わる
    // (Codex P1-EXTRUDE-R6 B1)。
    direction_->setCurrentIndex(DirectionIndexOf(initial.direction));
    form->addRow(QStringLiteral("押す向き"), direction_);

    reversed_ = new QCheckBox(QStringLiteral("逆向きにする"), this);
    reversed_->setChecked(initial.reversed);
    form->addRow(reversed_);
}

void V2ExtrudeDialog::BuildExtentRows(
    QFormLayout* form, const kachakacha::v2::app::ExtrudeChoice& initial)
{
    extent_ = new QComboBox(this);
    for (const auto mode : kachakacha::v2::app::ExtrudeExtents()) {
        extent_->addItem(Text(kachakacha::v2::app::ExtrudeExtentNameJa(mode)));
    }
    extent_->setCurrentIndex(ExtentIndexOf(initial.extent));
    form->addRow(QStringLiteral("どこまで"), extent_);

    distance_ = new QDoubleSpinBox(this);
    distance_->setRange(0.0, 10000.0);
    distance_->setDecimals(3);
    distance_->setSingleStep(0.1);
    distance_->setSuffix(QStringLiteral(" mm"));
    distance_->setValue(initial.distanceMm);
    form->addRow(QStringLiteral("距離"), distance_);

    secondDistance_ = new QDoubleSpinBox(this);
    secondDistance_->setRange(0.0, 10000.0);
    secondDistance_->setDecimals(3);
    secondDistance_->setSingleStep(0.1);
    secondDistance_->setSuffix(QStringLiteral(" mm"));
    secondDistance_->setValue(initial.secondDistanceMm);
    form->addRow(QStringLiteral("逆側の距離"), secondDistance_);

    target_ = new QComboBox(this);
    for (const auto& choice : targets_) {
        target_->addItem(choice.labelJa);
    }
    if (initial.targetEntityId.has_value()) {
        for (std::size_t i = 0; i < targets_.size(); ++i) {
            if (targets_[i].entityId == *initial.targetEntityId) {
                target_->setCurrentIndex(static_cast<int>(i));
                break;
            }
        }
    }
    form->addRow(QStringLiteral("届かせる相手"), target_);
}

void V2ExtrudeDialog::BuildOutputRows(
    QFormLayout* form, const kachakacha::v2::app::ExtrudeChoice& initial)
{
    makePart_ = new QCheckBox(QStringLiteral("部品(立体)を作る"), this);
    makePart_->setChecked(initial.makePart);
    form->addRow(makePart_);
    makeEndWire_ = new QCheckBox(QStringLiteral("押し出し先の輪郭を作る"), this);
    makeEndWire_->setChecked(initial.makeEndProfileWire);
    form->addRow(makeEndWire_);
    makeSideWires_ = new QCheckBox(QStringLiteral("側面の境界を作る"), this);
    makeSideWires_->setChecked(initial.makeSideBoundaryWires);
    form->addRow(makeSideWires_);

    boolean_ = new QComboBox(this);
    for (const auto mode : kachakacha::v2::app::ExtrudeBooleans()) {
        boolean_->addItem(Text(kachakacha::v2::app::ExtrudeBooleanNameJa(mode)));
    }
    boolean_->setCurrentIndex(BooleanIndexOf(initial.booleanMode));
    form->addRow(QStringLiteral("部品どうしの演算"), boolean_);

    zeroConfirmed_ = new QCheckBox(
        QStringLiteral("距離0のまま、ワイヤーだけ作ることを承知する"), this);
    zeroConfirmed_->setChecked(initial.zeroDistanceConfirmed);
    form->addRow(zeroConfirmed_);
}

V2ExtrudeDialog::V2ExtrudeDialog(const kachakacha::v2::app::ExtrudeChoice& initial,
    const kachakacha::v2::app::ExtrudeFacts& facts,
    std::vector<ExtrudeTargetChoice> targets, QWidget* parent)
    : QDialog(parent)
    , initialCustomDirection_(initial.customDirection)
    , facts_(facts)
    , targets_(std::move(targets))
{
    setWindowTitle(QStringLiteral("押し出し"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    BuildDirectionRows(form, initial);
    BuildExtentRows(form, initial);
    BuildOutputRows(form, initial);

    layout->addLayout(form);
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // 欄を変えたら、その場で出し入れと一文を作り直す。
    // 押してから断ると、何を直せばよいのか分からない。
    const auto refresh = [this] { Refresh(); };
    QObject::connect(direction_, &QComboBox::currentIndexChanged, this, refresh);
    QObject::connect(extent_, &QComboBox::currentIndexChanged, this, refresh);
    QObject::connect(boolean_, &QComboBox::currentIndexChanged, this, refresh);
    QObject::connect(makePart_, &QCheckBox::toggled, this, refresh);
    QObject::connect(makeEndWire_, &QCheckBox::toggled, this, refresh);
    QObject::connect(makeSideWires_, &QCheckBox::toggled, this, refresh);
    QObject::connect(reversed_, &QCheckBox::toggled, this, refresh);
    QObject::connect(zeroConfirmed_, &QCheckBox::toggled, this, refresh);
    Refresh();
}

kachakacha::v2::app::ExtrudeChoice V2ExtrudeDialog::Choice() const
{
    kachakacha::v2::app::ExtrudeChoice choice;
    const auto& directions = kachakacha::v2::app::ExtrudeDirections();
    const auto& extents = kachakacha::v2::app::ExtrudeExtents();
    const auto& booleans = kachakacha::v2::app::ExtrudeBooleans();
    const int directionIndex = direction_->currentIndex();
    const int extentIndex = extent_->currentIndex();
    const int booleanIndex = boolean_->currentIndex();
    if (directionIndex >= 0 && directionIndex < static_cast<int>(directions.size())) {
        choice.direction = directions[static_cast<std::size_t>(directionIndex)];
    }
    if (extentIndex >= 0 && extentIndex < static_cast<int>(extents.size())) {
        choice.extent = extents[static_cast<std::size_t>(extentIndex)];
    }
    if (booleanIndex >= 0 && booleanIndex < static_cast<int>(booleans.size())) {
        choice.booleanMode = booleans[static_cast<std::size_t>(booleanIndex)];
    }
    // 窓に XYZ の欄は無い。渡された自由な向きをそのまま返す。
    choice.customDirection = initialCustomDirection_;
    choice.reversed = reversed_->isChecked();
    choice.distanceMm = distance_->value();
    choice.secondDistanceMm = secondDistance_->value();
    choice.makePart = makePart_->isChecked();
    choice.makeEndProfileWire = makeEndWire_->isChecked();
    choice.makeSideBoundaryWires = makeSideWires_->isChecked();
    choice.zeroDistanceConfirmed = zeroConfirmed_->isChecked();
    choice.hasSelectedPart = facts_.parts > 0;
    const int targetIndex = target_->currentIndex();
    if (ExtentUsesTarget(choice.extent) && targetIndex >= 0
        && targetIndex < static_cast<int>(targets_.size())) {
        choice.targetEntityId = targets_[static_cast<std::size_t>(targetIndex)].entityId;
    }
    return choice;
}

void V2ExtrudeDialog::SetDirectionIndex(int index)
{
    direction_->setCurrentIndex(index);
    Refresh();
}

void V2ExtrudeDialog::SetExtentIndex(int index)
{
    extent_->setCurrentIndex(index);
    Refresh();
}

void V2ExtrudeDialog::SetBooleanIndex(int index)
{
    boolean_->setCurrentIndex(index);
    Refresh();
}

void V2ExtrudeDialog::SetDistanceMm(double value)
{
    distance_->setValue(value);
    Refresh();
}

void V2ExtrudeDialog::SetOutputs(bool part, bool endWire, bool sideWires)
{
    makePart_->setChecked(part);
    makeEndWire_->setChecked(endWire);
    makeSideWires_->setChecked(sideWires);
    Refresh();
}

bool V2ExtrudeDialog::CurrentChoiceIsValid(QString* reasonOut) const
{
    const auto checked = kachakacha::v2::app::ValidateExtrudeChoice(Choice(), facts_);
    if (checked.HasValue()) {
        return true;
    }
    if (reasonOut != nullptr && !checked.Diagnostics().empty()) {
        *reasonOut = QString::fromStdString(checked.FirstSummaryJa());
    }
    return false;
}

void V2ExtrudeDialog::Refresh()
{
    const auto choice = Choice();
    // 使わない欄は出さない。出したままにすると、入れた値が効くのかどうか分からない。
    distance_->setEnabled(ExtentUsesDistance(choice.extent));
    secondDistance_->setEnabled(ExtentUsesSecondDistance(choice.extent));
    target_->setEnabled(ExtentUsesTarget(choice.extent) && !targets_.empty());
    zeroConfirmed_->setEnabled(!choice.makePart);
    QString reason;
    if (CurrentChoiceIsValid(&reason)) {
        summary_->setText(
            QString::fromStdString(kachakacha::v2::app::ExtrudeSummaryJa(choice)));
        return;
    }
    // 押してから断らない。いま何が足りないかを、その場で出す。
    summary_->setText(QStringLiteral("このままでは押せません: %1").arg(reason));
}
