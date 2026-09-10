#include "V2WorkPlaneDock.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <string>
#include <string_view>

namespace {

using kachakacha::v2::app::WorkPlaneMethods;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::StandardPlaneKind;
using kachakacha::v2::modeling::WorkPlaneMethod;

[[nodiscard]] QString Text(std::string_view value)
{
    return QString::fromUtf8(std::string(value).c_str());
}

//! 作り方ごとに、どの欄を出すか。core の NeedsOf は「選ぶもの」を言うので、
//! 数の欄(3点・軸・通過点・線上位置)はここで足す。
struct RowUse {
    bool standard = false;
    bool pointNormal = false;
    bool threePoints = false;
    bool referencePlane = false;
    bool secondPlane = false;
    bool offset = false;
    bool axis = false;
    bool angle = false;
    bool curveParameter = false;
};

[[nodiscard]] RowUse RowUseOf(WorkPlaneMethod method)
{
    RowUse use;
    const auto needs = kachakacha::v2::app::NeedsOf(method);
    use.standard = needs.usesStandardKind;
    use.offset = needs.usesOffset;
    use.angle = needs.usesAngle;
    use.referencePlane = needs.planes >= 1;
    use.secondPlane = needs.planes >= 2;
    use.pointNormal = method == WorkPlaneMethod::PointNormal;
    use.threePoints = method == WorkPlaneMethod::ThreePoints;
    use.axis = method == WorkPlaneMethod::AngleAboutEdge;
    use.curveParameter = method == WorkPlaneMethod::NormalToCurveAtPoint;
    return use;
}

} // namespace

V2WorkPlaneDock::V2WorkPlaneDock(QWidget* parent)
    : QDockWidget(QStringLiteral("作業平面"), parent)
{
    setObjectName(QStringLiteral("workPlaneDock"));
    body_ = new QWidget(this);
    auto* layout = new QVBoxLayout(body_);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    form_ = new QFormLayout();
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setSpacing(3);

    name_ = new QLineEdit(body_);
    name_->setPlaceholderText(QStringLiteral("空なら作り方の名前"));
    form_->addRow(QStringLiteral("名前"), name_);

    method_ = new QComboBox(body_);
    for (const auto method : WorkPlaneMethods()) {
        method_->addItem(Text(kachakacha::v2::modeling::WorkPlaneMethodNameJa(method)));
    }
    form_->addRow(QStringLiteral("作り方"), method_);

    standard_ = new QComboBox(body_);
    standard_->addItem(QStringLiteral("上面 XY(上から見る面)"));
    standard_->addItem(QStringLiteral("側面 YZ(横から見る面)"));
    standard_->addItem(QStringLiteral("正面 XZ(前から見る面)"));
    form_->addRow(QStringLiteral("標準面"), standard_);

    origin_ = AddVectorRow(QStringLiteral("通過点 mm"), Vector3{0.0, 0.0, 0.0}, 1.0);
    normal_ = AddVectorRow(QStringLiteral("法線"), Vector3{0.0, 0.0, 1.0}, 0.1);
    uAxis_ = AddVectorRow(QStringLiteral("横方向"), Vector3{1.0, 0.0, 0.0}, 0.1);

    const WorkPlaneChoice defaults;
    for (std::size_t index = 0; index < 3; ++index) {
        threePoints_[index] = AddVectorRow(
            QStringLiteral("点%1 mm").arg(static_cast<int>(index) + 1),
            defaults.threePoints[index], 1.0);
    }

    referencePlane_ = AddPlaneRow(QStringLiteral("基準平面"));
    secondPlane_ = AddPlaneRow(QStringLiteral("相手の平面"));
    offset_ = AddNumberRow(QStringLiteral("離す距離"), -100000.0, 100000.0, 0.0,
        QStringLiteral(" mm"));
    axisPoint_ = AddVectorRow(QStringLiteral("軸の点 mm"), Vector3{0.0, 0.0, 0.0}, 1.0);
    axisDirection_ = AddVectorRow(QStringLiteral("軸の向き"), Vector3{1.0, 0.0, 0.0}, 0.1);
    angle_ = AddNumberRow(QStringLiteral("回す角度"), -360.0, 360.0, 0.0,
        QStringLiteral(" 度"));
    curveParameter_ = AddNumberRow(QStringLiteral("線上の位置"), 0.0, 1.0, 0.5, QString());
    curveParameter_->setSingleStep(0.05);

    layout->addLayout(form_);

    selectionLabel_ = new QLabel(body_);
    selectionLabel_->setWordWrap(true);
    layout->addWidget(selectionLabel_);
    needs_ = new QLabel(body_);
    needs_->setWordWrap(true);
    layout->addWidget(needs_);

    activate_ = new QCheckBox(QStringLiteral("作ったら作業中にする"), body_);
    activate_->setChecked(true);
    layout->addWidget(activate_);

    create_ = new QPushButton(QStringLiteral("平面を作る"), body_);
    QObject::connect(create_, &QPushButton::clicked, this, [this] {
        if (createHandler_) {
            createHandler_();
        }
    });
    layout->addWidget(create_);
    layout->addStretch(1);
    // 棚の中身は巻物にする。欄が多い棚の最小幅で右の棚全体が広がり、
    // 画面(作図の場所)が狭くなって入力列が画面の外へ寄っていた。
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body_);
    setWidget(scroll);

    QObject::connect(method_, &QComboBox::currentIndexChanged, this, [this] {
        ApplyVisibility();
        RefreshNeeds();
    });
    QObject::connect(referencePlane_, &QComboBox::currentIndexChanged, this,
        [this] { RefreshNeeds(); });
    QObject::connect(secondPlane_, &QComboBox::currentIndexChanged, this,
        [this] { RefreshNeeds(); });
    ApplyVisibility();
    RefreshNeeds();
}

std::array<QDoubleSpinBox*, 3> V2WorkPlaneDock::AddVectorRow(const QString& label,
    const Vector3& initial, double step)
{
    auto* row = new QWidget(body_);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(2);
    std::array<QDoubleSpinBox*, 3> fields{};
    const double values[3] = {initial.x, initial.y, initial.z};
    for (std::size_t index = 0; index < 3; ++index) {
        auto* field = new QDoubleSpinBox(row);
        field->setRange(-100000.0, 100000.0);
        field->setDecimals(3);
        field->setSingleStep(step);
        field->setValue(values[index]);
        rowLayout->addWidget(field);
        fields[index] = field;
    }
    form_->addRow(label, row);
    return fields;
}

QDoubleSpinBox* V2WorkPlaneDock::AddNumberRow(const QString& label, double minimum,
    double maximum, double initial, const QString& suffix)
{
    auto* field = new QDoubleSpinBox(body_);
    field->setRange(minimum, maximum);
    field->setDecimals(3);
    field->setSingleStep(1.0);
    field->setSuffix(suffix);
    field->setValue(initial);
    form_->addRow(label, field);
    return field;
}

QComboBox* V2WorkPlaneDock::AddPlaneRow(const QString& label)
{
    auto* combo = new QComboBox(body_);
    combo->addItem(QStringLiteral("(選んでいるものから)"));
    form_->addRow(label, combo);
    return combo;
}

void V2WorkPlaneDock::ApplyVisibility()
{
    const RowUse use = RowUseOf(Choice().method);
    // 使わない欄は出さない。出したままにすると、入れた値が効くのか分からない。
    const auto show = [this](QWidget* field, bool on) {
        if (field != nullptr) {
            form_->setRowVisible(field, on);
        }
    };
    const auto showVector = [&show](const std::array<QDoubleSpinBox*, 3>& fields, bool on) {
        if (fields[0] != nullptr) {
            show(fields[0]->parentWidget(), on);
        }
    };
    show(standard_, use.standard);
    showVector(origin_, use.pointNormal);
    showVector(normal_, use.pointNormal);
    showVector(uAxis_, use.pointNormal);
    for (const auto& point : threePoints_) {
        showVector(point, use.threePoints);
    }
    show(referencePlane_, use.referencePlane);
    show(secondPlane_, use.secondPlane);
    show(offset_, use.offset);
    showVector(axisPoint_, use.axis);
    showVector(axisDirection_, use.axis);
    show(angle_, use.angle);
    show(curveParameter_, use.curveParameter);
}

void V2WorkPlaneDock::RefreshNeeds()
{
    const WorkPlaneChoice choice = Choice();
    selectionLabel_->setText(QStringLiteral("いまの選択: 平面 %1・点 %2・線 %3")
            .arg(facts_.planes)
            .arg(facts_.points)
            .arg(facts_.edges));
    // コンボで決めた平面は、選んでいる平面と同じ扱い。
    kachakacha::v2::app::WorkPlaneFacts facts = facts_;
    facts.planes += (choice.referencePlaneId.has_value() ? 1 : 0)
        + (choice.secondPlaneId.has_value() ? 1 : 0);
    // 3点と回転軸は、選んでいなければ数の欄で足りる。
    const auto needs = kachakacha::v2::app::NeedsOf(choice.method);
    if (choice.method == WorkPlaneMethod::ThreePoints && facts.points < needs.points) {
        facts.points = needs.points;
    }
    if (choice.method == WorkPlaneMethod::AngleAboutEdge && facts.edges < needs.edges) {
        facts.edges = needs.edges;
    }
    const auto checked = kachakacha::v2::app::ValidateWorkPlaneChoice(choice.method, facts);
    if (checked.HasValue()) {
        needs_->setText(QStringLiteral("作れます(%1)。")
                .arg(Text(kachakacha::v2::app::WorkPlaneNeedsJa(choice.method))));
        create_->setEnabled(true);
        canCreate_ = true;
        return;
    }
    // 押してから断らない。いま何が足りないかを、その場で出す。
    const QString reason = checked.Diagnostics().empty()
        ? QString()
        : QString::fromStdString(checked.Diagnostics().front().detailsJa);
    needs_->setText(QStringLiteral("このままでは作れません: %1").arg(reason));
    create_->setEnabled(false);
    canCreate_ = false;
}

QString V2WorkPlaneDock::NeedsText() const
{
    return needs_ != nullptr ? needs_->text() : QString();
}

void V2WorkPlaneDock::PressCreate()
{
    if (canCreate_ && createHandler_) {
        createHandler_();
    }
}

bool V2WorkPlaneDock::CanCreate() const
{
    return canCreate_;
}

Vector3 V2WorkPlaneDock::VectorOf(const std::array<QDoubleSpinBox*, 3>& fields)
{
    return Vector3{fields[0]->value(), fields[1]->value(), fields[2]->value()};
}

void V2WorkPlaneDock::SetVector(const std::array<QDoubleSpinBox*, 3>& fields,
    const Vector3& value)
{
    fields[0]->setValue(value.x);
    fields[1]->setValue(value.y);
    fields[2]->setValue(value.z);
}

std::optional<kachakacha::v2::base::EntityId> V2WorkPlaneDock::PlaneOf(
    const QComboBox* combo) const
{
    const int index = combo->currentIndex() - 1;
    if (index < 0 || index >= static_cast<int>(planeIds_.size())) {
        return std::nullopt;
    }
    return planeIds_[static_cast<std::size_t>(index)];
}

void V2WorkPlaneDock::SelectPlane(QComboBox* combo,
    const std::optional<kachakacha::v2::base::EntityId>& id)
{
    if (!id.has_value()) {
        combo->setCurrentIndex(0);
        return;
    }
    for (std::size_t index = 0; index < planeIds_.size(); ++index) {
        if (planeIds_[index] == *id) {
            combo->setCurrentIndex(static_cast<int>(index) + 1);
            return;
        }
    }
    combo->setCurrentIndex(0);
}

WorkPlaneChoice V2WorkPlaneDock::Choice() const
{
    WorkPlaneChoice choice;
    const auto& methods = WorkPlaneMethods();
    const int index = method_->currentIndex();
    if (index >= 0 && index < static_cast<int>(methods.size())) {
        choice.method = methods[static_cast<std::size_t>(index)];
    }
    choice.standard = static_cast<StandardPlaneKind>(
        standard_->currentIndex() < 0 ? 0 : standard_->currentIndex());
    choice.name = name_->text().trimmed().toStdString();
    choice.offsetMm = offset_->value();
    choice.angleDeg = angle_->value();
    choice.origin = VectorOf(origin_);
    choice.normal = VectorOf(normal_);
    choice.uAxis = VectorOf(uAxis_);
    for (std::size_t point = 0; point < 3; ++point) {
        choice.threePoints[point] = VectorOf(threePoints_[point]);
    }
    choice.axisPoint = VectorOf(axisPoint_);
    choice.axisDirection = VectorOf(axisDirection_);
    choice.referencePlaneId = PlaneOf(referencePlane_);
    choice.secondPlaneId = PlaneOf(secondPlane_);
    choice.curveParameter = curveParameter_->value();
    return choice;
}

void V2WorkPlaneDock::SetChoice(const WorkPlaneChoice& choice)
{
    const auto& methods = WorkPlaneMethods();
    for (std::size_t index = 0; index < methods.size(); ++index) {
        if (methods[index] == choice.method) {
            method_->setCurrentIndex(static_cast<int>(index));
            break;
        }
    }
    standard_->setCurrentIndex(static_cast<int>(choice.standard));
    name_->setText(QString::fromStdString(choice.name));
    offset_->setValue(choice.offsetMm);
    angle_->setValue(choice.angleDeg);
    SetVector(origin_, choice.origin);
    SetVector(normal_, choice.normal);
    SetVector(uAxis_, choice.uAxis);
    for (std::size_t point = 0; point < 3; ++point) {
        SetVector(threePoints_[point], choice.threePoints[point]);
    }
    SetVector(axisPoint_, choice.axisPoint);
    SetVector(axisDirection_, choice.axisDirection);
    SelectPlane(referencePlane_, choice.referencePlaneId);
    SelectPlane(secondPlane_, choice.secondPlaneId);
    curveParameter_->setValue(choice.curveParameter);
    ApplyVisibility();
    RefreshNeeds();
}

void V2WorkPlaneDock::SetPlanes(
    const std::vector<std::pair<kachakacha::v2::base::EntityId, QString>>& planes)
{
    // 選び直しても、前に選んでいた平面が残るなら残す。
    const auto reference = PlaneOf(referencePlane_);
    const auto second = PlaneOf(secondPlane_);
    planeIds_.clear();
    for (QComboBox* combo : {referencePlane_, secondPlane_}) {
        combo->clear();
        combo->addItem(QStringLiteral("(選んでいるものから)"));
    }
    for (const auto& [id, name] : planes) {
        planeIds_.push_back(id);
        referencePlane_->addItem(name);
        secondPlane_->addItem(name);
    }
    SelectPlane(referencePlane_, reference);
    SelectPlane(secondPlane_, second);
    RefreshNeeds();
}

void V2WorkPlaneDock::Refresh(const kachakacha::v2::app::WorkPlaneFacts& facts)
{
    facts_ = facts;
    RefreshNeeds();
}

bool V2WorkPlaneDock::ActivateAfterCreate() const
{
    return activate_->isChecked();
}

void V2WorkPlaneDock::SetCreateHandler(std::function<void()> handler)
{
    createHandler_ = std::move(handler);
}

void V2WorkPlaneDock::SetMethodIndex(int index)
{
    method_->setCurrentIndex(index);
    ApplyVisibility();
    RefreshNeeds();
}
