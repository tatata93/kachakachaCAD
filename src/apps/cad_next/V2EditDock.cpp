#include "V2EditDock.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

using kachakacha::v2::app::LineMeasure;
using kachakacha::v2::app::PlaneEditFields;
using kachakacha::v2::app::WireEditFields;
using kachakacha::v2::app::WireEditShape;
using kachakacha::v2::geometry::Vector3;

namespace {

constexpr int kNothingPage = 0;
constexpr int kPlanePage = 1;
constexpr int kWirePage = 2;
constexpr int kPointsGeometry = 0;
constexpr int kArcGeometry = 1;

[[nodiscard]] QDoubleSpinBox* MakeNumber(QWidget* parent, double step, double minimum = -1.0e6)
{
    auto* field = new QDoubleSpinBox(parent);
    field->setRange(minimum, 1.0e6);
    field->setDecimals(3);
    field->setSingleStep(step);
    return field;
}

[[nodiscard]] QWidget* MakeSection(QWidget* parent, const QString& title)
{
    auto* label = new QLabel(title, parent);
    label->setStyleSheet(QStringLiteral("font-weight: 600;"));
    return label;
}

} // namespace

V2EditDock::V2EditDock(QWidget* parent)
    : QDockWidget(QStringLiteral("編集"), parent)
{
    setObjectName(QStringLiteral("editDock"));
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    layout->addWidget(MakeSection(body, QStringLiteral("選択内容の数値編集")));
    selection_ = new QLabel(QStringLiteral("選択なし"), body);
    selection_->setWordWrap(true);
    layout->addWidget(selection_);

    pages_ = new QStackedWidget(body);
    nothing_ = new QLabel(QStringLiteral("作業平面か線を 1 つ選ぶと、その数値がここに出ます。"),
        pages_);
    nothing_->setWordWrap(true);
    pages_->addWidget(nothing_);
    pages_->addWidget(BuildPlanePage());
    pages_->addWidget(BuildWirePage());
    layout->addWidget(pages_);

    apply_ = new QPushButton(QStringLiteral("変更を適用"), body);
    apply_->setObjectName(QStringLiteral("primaryButton"));
    QObject::connect(apply_, &QPushButton::clicked, this, [this] { PressApply(); });
    layout->addWidget(apply_);

    message_ = new QLabel(body);
    message_->setWordWrap(true);
    layout->addWidget(message_);
    layout->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body);
    setWidget(scroll);
    ShowNothing(QString());
}

QWidget* V2EditDock::BuildPlanePage()
{
    auto* page = new QWidget(pages_);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(QStringLiteral("原点"), MakeVector3Row(page, planeOrigin_, 1.0));
    form->addRow(QStringLiteral("法線"), MakeVector3Row(page, planeNormal_, 0.1));
    form->addRow(QStringLiteral("平面内 X"), MakeVector3Row(page, planeU_, 0.1));
    return page;
}

QWidget* V2EditDock::BuildWirePage()
{
    auto* page = new QWidget(pages_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* metaWidget = new QWidget(page);
    auto* meta = new QFormLayout(metaWidget);
    meta->setContentsMargins(0, 0, 0, 0);
    sourcePlane_ = new QComboBox(metaWidget);
    sourcePlane_->addItem(QStringLiteral("なし"));
    meta->addRow(QStringLiteral("作成元平面"), sourcePlane_);
    construction_ = new QCheckBox(QStringLiteral("補助線(面・出力から除外)"), metaWidget);
    meta->addRow(QStringLiteral("用途"), construction_);
    layout->addWidget(metaWidget);

    linePanel_ = BuildLinePanel();
    layout->addWidget(linePanel_);

    geometry_ = new QStackedWidget(page);
    pointsPage_ = new QWidget(geometry_);
    pointsLayout_ = new QVBoxLayout(pointsPage_);
    pointsLayout_->setContentsMargins(0, 0, 0, 0);
    pointsLayout_->setSpacing(2);
    geometry_->addWidget(pointsPage_);
    geometry_->addWidget(BuildArcPage());
    layout->addWidget(geometry_);
    return page;
}

QWidget* V2EditDock::BuildLinePanel()
{
    // V1 の「直線の寸法を保持」。V2 には拘束が無いので「置き直す」と言う。
    auto* panel = new QWidget(pages_);
    auto* form = new QFormLayout(panel);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(MakeSection(panel, QStringLiteral("直線の長さと向きを置き直す")));

    auto* lengthRow = new QWidget(panel);
    auto* lengthLayout = new QHBoxLayout(lengthRow);
    lengthLayout->setContentsMargins(0, 0, 0, 0);
    relocateLength_ = new QCheckBox(QStringLiteral("置き直す"), lengthRow);
    length_ = MakeNumber(lengthRow, 1.0, 0.0);
    length_->setSuffix(QStringLiteral(" mm"));
    lengthLayout->addWidget(relocateLength_);
    lengthLayout->addWidget(length_);
    form->addRow(QStringLiteral("長さ"), lengthRow);

    auto* angleRow = new QWidget(panel);
    auto* angleLayout = new QHBoxLayout(angleRow);
    angleLayout->setContentsMargins(0, 0, 0, 0);
    relocateAngle_ = new QCheckBox(QStringLiteral("置き直す"), angleRow);
    angle_ = MakeNumber(angleRow, 1.0, -360.0);
    angle_->setRange(-360.0, 360.0);
    angle_->setSuffix(QStringLiteral(" °"));
    auto* horizontal = new QPushButton(QStringLiteral("水平 0°"), angleRow);
    auto* vertical = new QPushButton(QStringLiteral("垂直 90°"), angleRow);
    QObject::connect(horizontal, &QPushButton::clicked, this, [this] { SetAngle(0.0); });
    QObject::connect(vertical, &QPushButton::clicked, this, [this] { SetAngle(90.0); });
    angleLayout->addWidget(relocateAngle_);
    angleLayout->addWidget(angle_);
    angleLayout->addWidget(horizontal);
    angleLayout->addWidget(vertical);
    form->addRow(QStringLiteral("平面内角度"), angleRow);
    angleFrame_ = new QLabel(panel);
    angleFrame_->setWordWrap(true);
    form->addRow(QStringLiteral(""), angleFrame_);
    return panel;
}

QWidget* V2EditDock::BuildArcPage()
{
    auto* page = new QWidget(geometry_);
    auto* form = new QFormLayout(page);
    arcForm_ = form;
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(QStringLiteral("中心"), MakeVector3Row(page, arcCenter_, 1.0));
    form->addRow(QStringLiteral("円の X 軸"), MakeVector3Row(page, arcU_, 0.1));
    form->addRow(QStringLiteral("円の Y 軸"), MakeVector3Row(page, arcV_, 0.1));
    radius_ = MakeNumber(page, 1.0, 0.0);
    radius_->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("半径"), radius_);
    startAngle_ = MakeNumber(page, 1.0, -360.0);
    startAngle_->setSuffix(QStringLiteral(" °"));
    form->addRow(QStringLiteral("開始角"), startAngle_);
    sweepAngle_ = MakeNumber(page, 1.0, -360.0);
    sweepAngle_->setRange(-360.0, 360.0);
    sweepAngle_->setSuffix(QStringLiteral(" °"));
    form->addRow(QStringLiteral("中心角"), sweepAngle_);
    return page;
}

QWidget* V2EditDock::MakeVector3Row(QWidget* parent, Vector3Fields& fields, double step)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);
    for (auto& field : fields) {
        field = MakeNumber(row, step);
        layout->addWidget(field);
    }
    return row;
}

void V2EditDock::SetVector3(const Vector3Fields& fields, const Vector3& value)
{
    fields[0]->setValue(value.x);
    fields[1]->setValue(value.y);
    fields[2]->setValue(value.z);
}

Vector3 V2EditDock::ReadVector3(const Vector3Fields& fields)
{
    return Vector3{fields[0]->value(), fields[1]->value(), fields[2]->value()};
}

void V2EditDock::EnsurePointRows(std::size_t count)
{
    // 行は捨てずに使い回す。足りない分だけ作り、余りは隠す。
    while (pointRows_.size() < count) {
        PointRow row;
        row.row = new QWidget(pointsPage_);
        auto* layout = new QHBoxLayout(row.row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(3);
        row.label = new QLabel(row.row);
        row.label->setMinimumWidth(44);
        layout->addWidget(row.label);
        for (auto& field : row.fields) {
            field = MakeNumber(row.row, 1.0);
            layout->addWidget(field);
        }
        pointsLayout_->addWidget(row.row);
        pointRows_.push_back(row);
    }
    for (std::size_t index = 0; index < pointRows_.size(); ++index) {
        pointRows_[index].row->setVisible(index < count);
    }
    visiblePointRows_ = count;
}

void V2EditDock::ShowNothing(const QString& reason)
{
    selection_->setText(QStringLiteral("選択なし"));
    nothing_->setText(reason.isEmpty()
            ? QStringLiteral("作業平面か線を 1 つ選ぶと、その数値がここに出ます。")
            : reason);
    pages_->setCurrentIndex(kNothingPage);
    apply_->setEnabled(false);
    message_->setText(QString());
}

void V2EditDock::ShowPlane(const QString& name, const PlaneEditFields& fields)
{
    selection_->setText(QStringLiteral("作業平面: %1").arg(name));
    SetVector3(planeOrigin_, fields.origin);
    SetVector3(planeNormal_, fields.normal);
    SetVector3(planeU_, fields.uDirection);
    pages_->setCurrentIndex(kPlanePage);
    apply_->setEnabled(true);
    message_->setText(QString());
}

void V2EditDock::ShowWire(const QString& name, const WireEditFields& fields,
    const LineMeasure& measure, const QString& frameName)
{
    shape_ = fields.shape;
    selection_->setText(QStringLiteral("%1: %2")
            .arg(QString::fromUtf8(std::string(
                kachakacha::v2::app::WireEditShapeNameJa(fields.shape)).c_str()),
                name));
    construction_->setChecked(fields.construction);
    int planeIndex = 0;
    for (std::size_t index = 0; index < planeIds_.size(); ++index) {
        if (fields.sourcePlaneId.has_value() && planeIds_[index] == *fields.sourcePlaneId) {
            planeIndex = static_cast<int>(index) + 1;
        }
    }
    sourcePlane_->setCurrentIndex(planeIndex);

    const bool isLine = fields.shape == WireEditShape::Line;
    linePanel_->setVisible(isLine);
    relocateLength_->setChecked(false);
    relocateAngle_->setChecked(false);
    length_->setValue(measure.lengthMm);
    angle_->setValue(measure.angleDeg);
    angleFrame_->setText(QStringLiteral("角度は %1 の X 方向が 0°、Y 方向が 90°。").arg(frameName));

    const bool isArc = fields.shape == WireEditShape::Circle
        || fields.shape == WireEditShape::CircularArc;
    if (isArc) {
        SetVector3(arcCenter_, fields.center);
        SetVector3(arcU_, fields.uAxis);
        SetVector3(arcV_, fields.vAxis);
        radius_->setValue(fields.radiusMm);
        startAngle_->setValue(fields.startAngleDeg);
        sweepAngle_->setValue(fields.sweepAngleDeg);
        const bool showAngles = fields.shape == WireEditShape::CircularArc;
        arcForm_->setRowVisible(startAngle_, showAngles);
        arcForm_->setRowVisible(sweepAngle_, showAngles);
        geometry_->setCurrentIndex(kArcGeometry);
    } else {
        EnsurePointRows(fields.points.size());
        for (std::size_t index = 0; index < fields.points.size(); ++index) {
            const bool controlPoint = fields.shape == WireEditShape::CubicBezier
                || fields.shape == WireEditShape::CubicBSpline;
            pointRows_[index].label->setText(controlPoint
                    ? QStringLiteral("制御点 %1").arg(static_cast<int>(index) + 1)
                    : QStringLiteral("点 %1").arg(static_cast<int>(index) + 1));
            SetVector3(pointRows_[index].fields, fields.points[index]);
        }
        geometry_->setCurrentIndex(kPointsGeometry);
    }
    pages_->setCurrentIndex(kWirePage);
    apply_->setEnabled(true);
    message_->setText(QString());
}

void V2EditDock::SetPlanes(
    const std::vector<std::pair<kachakacha::v2::base::EntityId, QString>>& planes)
{
    const int previous = sourcePlane_->currentIndex();
    sourcePlane_->clear();
    sourcePlane_->addItem(QStringLiteral("なし"));
    planeIds_.clear();
    for (const auto& [id, name] : planes) {
        sourcePlane_->addItem(name);
        planeIds_.push_back(id);
    }
    sourcePlane_->setCurrentIndex(previous >= 0 && previous < sourcePlane_->count() ? previous : 0);
}

bool V2EditDock::IsShowingPlane() const
{
    return pages_->currentIndex() == kPlanePage;
}

bool V2EditDock::IsShowingWire() const
{
    return pages_->currentIndex() == kWirePage;
}

PlaneEditFields V2EditDock::PlaneFields() const
{
    PlaneEditFields fields;
    fields.origin = ReadVector3(planeOrigin_);
    fields.normal = ReadVector3(planeNormal_);
    fields.uDirection = ReadVector3(planeU_);
    return fields;
}

WireEditFields V2EditDock::WireFields() const
{
    WireEditFields fields;
    fields.shape = shape_;
    fields.construction = construction_->isChecked();
    const int planeIndex = sourcePlane_->currentIndex();
    if (planeIndex > 0 && static_cast<std::size_t>(planeIndex) <= planeIds_.size()) {
        fields.sourcePlaneId = planeIds_[static_cast<std::size_t>(planeIndex) - 1];
    }
    if (shape_ == WireEditShape::Circle || shape_ == WireEditShape::CircularArc) {
        fields.center = ReadVector3(arcCenter_);
        fields.uAxis = ReadVector3(arcU_);
        fields.vAxis = ReadVector3(arcV_);
        fields.radiusMm = radius_->value();
        fields.startAngleDeg = startAngle_->value();
        fields.sweepAngleDeg = sweepAngle_->value();
        return fields;
    }
    for (std::size_t index = 0; index < visiblePointRows_; ++index) {
        fields.points.push_back(ReadVector3(pointRows_[index].fields));
    }
    if (shape_ == WireEditShape::Line) {
        if (relocateLength_->isChecked()) {
            fields.lengthMm = length_->value();
        }
        if (relocateAngle_->isChecked()) {
            fields.angleDeg = angle_->value();
        }
    }
    return fields;
}

void V2EditDock::SetApplyHandler(std::function<void()> handler)
{
    applyHandler_ = std::move(handler);
}

void V2EditDock::PressApply()
{
    if (applyHandler_) {
        applyHandler_();
    }
}

void V2EditDock::SetMessage(const QString& text)
{
    message_->setText(text);
}

QString V2EditDock::MessageText() const
{
    return message_->text();
}

QString V2EditDock::SelectionText() const
{
    return selection_->text();
}

int V2EditDock::PointRowCount() const
{
    return static_cast<int>(visiblePointRows_);
}

void V2EditDock::SetPoint(int row, const Vector3& point)
{
    if (row >= 0 && static_cast<std::size_t>(row) < visiblePointRows_) {
        SetVector3(pointRows_[static_cast<std::size_t>(row)].fields, point);
    }
}

void V2EditDock::SetPlaneOrigin(const Vector3& origin)
{
    SetVector3(planeOrigin_, origin);
}

void V2EditDock::SetPlaneNormal(const Vector3& normal)
{
    SetVector3(planeNormal_, normal);
}

void V2EditDock::SetRadius(double radiusMm)
{
    radius_->setValue(radiusMm);
}

void V2EditDock::SetSweepAngle(double degrees)
{
    sweepAngle_->setValue(degrees);
}

void V2EditDock::SetLength(double lengthMm)
{
    relocateLength_->setChecked(true);
    length_->setValue(lengthMm);
}

void V2EditDock::SetAngle(double degrees)
{
    relocateAngle_->setChecked(true);
    angle_->setValue(degrees);
}

void V2EditDock::SetConstruction(bool construction)
{
    construction_->setChecked(construction);
}

double V2EditDock::LengthValue() const
{
    return length_->value();
}

double V2EditDock::AngleValue() const
{
    return angle_->value();
}
