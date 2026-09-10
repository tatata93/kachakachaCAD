#include "V2CornerDock.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

namespace {

[[nodiscard]] QDoubleSpinBox* MakeMm(QWidget* parent, double minimum)
{
    auto* field = new QDoubleSpinBox(parent);
    field->setRange(minimum, 1.0e6);
    field->setDecimals(3);
    field->setSingleStep(0.5);
    field->setSuffix(QStringLiteral(" mm"));
    return field;
}

[[nodiscard]] QComboBox* MakeSideCombo(QWidget* parent)
{
    auto* combo = new QComboBox(parent);
    combo->addItem(QStringLiteral("自動"));
    combo->addItem(QStringLiteral("始点側"));
    combo->addItem(QStringLiteral("終点側"));
    return combo;
}

} // namespace

V2CornerDock::V2CornerDock(QWidget* parent)
    : QDockWidget(QStringLiteral("面取り"), parent)
{
    setObjectName(QStringLiteral("cornerDock"));
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    auto* hint = new QLabel(QStringLiteral(
        "線を 2 本、直す順に選んでから作ります(1 本目が A、2 本目が B)。"
        "離れた線は交点まで自動で延ばします。"), body);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* formWidget = new QWidget(body);
    form_ = new QFormLayout(formWidget);
    form_->setContentsMargins(0, 0, 0, 0);
    kind_ = new QComboBox(formWidget);
    kind_->addItem(QStringLiteral("C面取り"));
    kind_->addItem(QStringLiteral("R丸め"));
    form_->addRow(QStringLiteral("加工種類"), kind_);
    first_ = new QLabel(QStringLiteral("(未選択)"), formWidget);
    second_ = new QLabel(QStringLiteral("(未選択)"), formWidget);
    form_->addRow(QStringLiteral("直線 A"), first_);
    firstKeep_ = MakeSideCombo(formWidget);
    form_->addRow(QStringLiteral("A の残す側"), firstKeep_);
    form_->addRow(QStringLiteral("直線 B"), second_);
    secondKeep_ = MakeSideCombo(formWidget);
    form_->addRow(QStringLiteral("B の残す側"), secondKeep_);
    size_ = MakeMm(formWidget, 0.0);
    size_->setToolTip(QStringLiteral("数の棚の「面取り量 / 丸め半径」と同じ値です。"));
    form_->addRow(QStringLiteral("A の切戻し"), size_);
    secondSetback_ = MakeMm(formWidget, 0.0);
    secondSetback_->setToolTip(QStringLiteral("0 なら A と同じ(対称)。"));
    form_->addRow(QStringLiteral("B の切戻し"), secondSetback_);
    radius_ = MakeMm(formWidget, 0.0);
    radius_->setToolTip(QStringLiteral("数の棚の「面取り量 / 丸め半径」と同じ値です。"));
    form_->addRow(QStringLiteral("半径"), radius_);
    layout->addWidget(formWidget);

    create_ = new QPushButton(QStringLiteral("C面取りを作成"), body);
    create_->setObjectName(QStringLiteral("primaryButton"));
    layout->addWidget(create_);

    auto* cornerTitle = new QLabel(QStringLiteral("ポリラインの角"), body);
    cornerTitle->setStyleSheet(QStringLiteral("font-weight: 600;"));
    layout->addWidget(cornerTitle);
    auto* cornerWidget = new QWidget(body);
    auto* cornerForm = new QFormLayout(cornerWidget);
    cornerForm->setContentsMargins(0, 0, 0, 0);
    onlyVertex_ = new QCheckBox(QStringLiteral("この頂点の角だけ"), cornerWidget);
    cornerForm->addRow(onlyVertex_);
    vertex_ = new QDoubleSpinBox(cornerWidget);
    vertex_->setRange(0.0, 9999.0);
    vertex_->setDecimals(0);
    vertex_->setSingleStep(1.0);
    vertex_->setToolTip(QStringLiteral(
        "頂点番号(0 始まり、点の番号)。閉じた輪郭では 0 が始点/終点の角。"
        "開いた折れ線の両端は角ではないので断られます。"));
    cornerForm->addRow(QStringLiteral("頂点番号"), vertex_);
    layout->addWidget(cornerWidget);
    corner_ = new QPushButton(QStringLiteral("角を加工(上の種類と値を使用)"), body);
    layout->addWidget(corner_);
    layout->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body);
    setWidget(scroll);

    QObject::connect(kind_, &QComboBox::currentIndexChanged, this, [this] { RefreshKind(); });
    QObject::connect(size_, &QDoubleSpinBox::valueChanged, this, [this] { EmitSize(); });
    QObject::connect(radius_, &QDoubleSpinBox::valueChanged, this, [this] { EmitSize(); });
    QObject::connect(create_, &QPushButton::clicked, this, [this] { PressCreate(); });
    QObject::connect(corner_, &QPushButton::clicked, this, [this] { PressCorner(); });
    RefreshKind();
}

void V2CornerDock::RefreshKind()
{
    const bool fillet = kind_->currentIndex() == 1;
    form_->setRowVisible(size_, !fillet);
    form_->setRowVisible(secondSetback_, !fillet);
    form_->setRowVisible(radius_, fillet);
    create_->setText(fillet ? QStringLiteral("R丸めを作成") : QStringLiteral("C面取りを作成"));
}

void V2CornerDock::EmitSize()
{
    if (loading_ || !sizeHandler_) {
        return;
    }
    // 出ている方の欄が正。もう片方は同じ値にそろえる(数の棚は 1 つの値)。
    const bool fillet = kind_->currentIndex() == 1;
    const double value = fillet ? radius_->value() : size_->value();
    loading_ = true;
    size_->setValue(value);
    radius_->setValue(value);
    loading_ = false;
    sizeHandler_(value);
}

V2CornerChoice V2CornerDock::Choice() const
{
    V2CornerChoice choice;
    choice.fillet = kind_->currentIndex() == 1;
    choice.sizeMm = choice.fillet ? radius_->value() : size_->value();
    choice.secondSetbackMm = secondSetback_->value();
    choice.firstKeepSide = firstKeep_->currentIndex();
    choice.secondKeepSide = secondKeep_->currentIndex();
    choice.onlyVertex = onlyVertex_->isChecked();
    choice.vertexIndex = static_cast<int>(vertex_->value());
    return choice;
}

void V2CornerDock::SetChoice(const V2CornerChoice& choice)
{
    loading_ = true;
    kind_->setCurrentIndex(choice.fillet ? 1 : 0);
    size_->setValue(choice.sizeMm);
    radius_->setValue(choice.sizeMm);
    secondSetback_->setValue(choice.secondSetbackMm);
    firstKeep_->setCurrentIndex(choice.firstKeepSide);
    secondKeep_->setCurrentIndex(choice.secondKeepSide);
    onlyVertex_->setChecked(choice.onlyVertex);
    vertex_->setValue(static_cast<double>(choice.vertexIndex));
    loading_ = false;
    RefreshKind();
}

void V2CornerDock::SetSizeMm(double sizeMm)
{
    loading_ = true;
    size_->setValue(sizeMm);
    radius_->setValue(sizeMm);
    loading_ = false;
}

void V2CornerDock::SetSizeHandler(std::function<void(double)> handler)
{
    sizeHandler_ = std::move(handler);
}

void V2CornerDock::SetRunHandler(std::function<void(const char*)> handler)
{
    runHandler_ = std::move(handler);
}

void V2CornerDock::SetPairText(const QString& first, const QString& second)
{
    first_->setText(first.isEmpty() ? QStringLiteral("(未選択)") : first);
    second_->setText(second.isEmpty() ? QStringLiteral("(未選択)") : second);
}

void V2CornerDock::SetFillet(bool fillet)
{
    kind_->setCurrentIndex(fillet ? 1 : 0);
    RefreshKind();
}

void V2CornerDock::SetSecondSetback(double mm)
{
    secondSetback_->setValue(mm);
}

void V2CornerDock::SetKeepSides(int first, int second)
{
    firstKeep_->setCurrentIndex(first);
    secondKeep_->setCurrentIndex(second);
}

void V2CornerDock::SetOnlyVertex(bool only, int vertexIndex)
{
    onlyVertex_->setChecked(only);
    vertex_->setValue(static_cast<double>(vertexIndex));
}

void V2CornerDock::PressCreate()
{
    if (runHandler_) {
        runHandler_(kind_->currentIndex() == 1 ? "wire.fillet" : "wire.chamfer");
    }
}

void V2CornerDock::PressCorner()
{
    if (runHandler_) {
        runHandler_(kind_->currentIndex() == 1 ? "wire.corner_fillet" : "wire.corner_chamfer");
    }
}

QString V2CornerDock::FirstText() const
{
    return first_->text();
}

QString V2CornerDock::SecondText() const
{
    return second_->text();
}

QString V2CornerDock::CreateButtonText() const
{
    return create_->text();
}
