#include "V2PartDock.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace {

using kachakacha::v2::app::ParameterId;
using kachakacha::v2::fabrication::ThicknessPlacement;

[[nodiscard]] QDoubleSpinBox* MakeField(QWidget* parent, double minimum, double maximum,
    const QString& suffix)
{
    auto* box = new QDoubleSpinBox(parent);
    box->setRange(minimum, maximum);
    box->setDecimals(3);
    box->setSuffix(suffix);
    return box;
}

[[nodiscard]] QPushButton* MakeRun(QWidget* parent, const QString& text, const char* command,
    V2PartDock* dock)
{
    auto* button = new QPushButton(text, parent);
    QObject::connect(button, &QPushButton::clicked, dock,
        [dock, command] { dock->PressRun(command); });
    return button;
}

} // namespace

V2PartDock::V2PartDock(QWidget* parent)
    : QDockWidget(QStringLiteral("部品"), parent)
{
    setObjectName(QStringLiteral("partDock"));
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    selection_ = new QLabel(body);
    selection_->setWordWrap(true);
    layout->addWidget(selection_);

    auto* form = new QFormLayout();
    layout->addLayout(form);

    extrudeDistance_ = MakeField(body, -10000.0, 10000.0, QStringLiteral(" mm"));
    extrudeDistance_->setToolTip(QStringLiteral(
        "押し出しの距離。負なら逆へ出ます。数の棚の「押し出しの距離」と同じ値です。"));
    form->addRow(QStringLiteral("押し出しの距離"), extrudeDistance_);

    thickness_ = MakeField(body, 0.0, 1000.0, QStringLiteral(" mm"));
    thickness_->setToolTip(QStringLiteral(
        "面に付ける厚み。プラ板の厚みです(0.3 / 0.5 / 1.0 など)。"));
    form->addRow(QStringLiteral("板厚"), thickness_);

    placement_ = new QComboBox(body);
    placement_->addItem(QStringLiteral("外側"));
    placement_->addItem(QStringLiteral("中央"));
    placement_->addItem(QStringLiteral("内側"));
    placement_->setToolTip(QStringLiteral(
        "面のどちら側へ厚みを付けるか。外側は法線の向き、内側は逆、中央は half ずつ。"));
    form->addRow(QStringLiteral("厚みの付け方"), placement_);

    offsetDistance_ = MakeField(body, -10000.0, 10000.0, QStringLiteral(" mm"));
    offsetDistance_->setToolTip(QStringLiteral(
        "面を離す距離(形状ガイドの「離した面」)。負なら逆側へ離します。"));
    form->addRow(QStringLiteral("面を離す距離"), offsetDistance_);

    revolveAngle_ = MakeField(body, 0.0, 360.0, QStringLiteral(" °"));
    revolveAngle_->setToolTip(QStringLiteral(
        "回転体で回す角度。360 なら一周します。断面1本目、軸の直線2本目の順で選びます。"));
    form->addRow(QStringLiteral("回転体の角度"), revolveAngle_);

    jigClearance_ = MakeField(body, 0.0, 1000.0, QStringLiteral(" mm"));
    jigClearance_->setToolTip(QStringLiteral(
        "治具のすき間。0 なら面にぴったり当てます。"));
    form->addRow(QStringLiteral("治具のすき間"), jigClearance_);

    jigThickness_ = MakeField(body, -1000.0, 1000.0, QStringLiteral(" mm"));
    jigThickness_->setToolTip(QStringLiteral(
        "治具の厚み。**正なら面の表側、負なら裏側** に当て板を作ります。0 にはできません。"));
    form->addRow(QStringLiteral("治具の厚み"), jigThickness_);

    auto* buttons = new QWidget(body);
    auto* buttonLayout = new QVBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(2);
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("押し出し"), "part.extrude", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("面に厚みを付ける"),
        "part.thicken", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("面を平面まで立体に"),
        "part.thicken_to_plane", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("回転体を作る"),
        "guide.revolve", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("治具を作る"),
        "part.surface_jig", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("ワイヤー群から部品"),
        "part.from_wire_cage", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("足す"), "part.boolean_add", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("引く"), "part.boolean_cut", this));
    buttonLayout->addWidget(MakeRun(buttons, QStringLiteral("現在状態を固定"),
        "derived.freeze", this));
    layout->addWidget(buttons);
    layout->addStretch(1);

    const auto notify = [this](ParameterId id, QDoubleSpinBox* field) {
        QObject::connect(field, &QDoubleSpinBox::valueChanged, this, [this, id, field] {
            if (parameterHandler_) {
                parameterHandler_(id, field->value());
            }
        });
    };
    notify(ParameterId::ExtrudeDistance, extrudeDistance_);
    notify(ParameterId::ExtrudeDistance, thickness_);
    notify(ParameterId::OffsetDistanceMm, offsetDistance_);
    notify(ParameterId::RevolveAngleDeg, revolveAngle_);
    notify(ParameterId::JigClearanceMm, jigClearance_);
    notify(ParameterId::JigThicknessMm, jigThickness_);
    QObject::connect(placement_, &QComboBox::currentIndexChanged, this, [this] {
        if (placementHandler_) {
            placementHandler_(Placement());
        }
    });

    setWidget(body);
}

QDoubleSpinBox* V2PartDock::FieldFor(ParameterId id) const
{
    switch (id) {
    // 板厚と押し出しの距離は V2 では同じ数(数の棚の「押し出しの距離」)。
    // 別の数にすると、板から立体を出すときに2か所へ同じ値を打つことになる。
    case ParameterId::ExtrudeDistance:  return extrudeDistance_;
    case ParameterId::OffsetDistanceMm: return offsetDistance_;
    case ParameterId::RevolveAngleDeg:  return revolveAngle_;
    case ParameterId::JigClearanceMm:   return jigClearance_;
    case ParameterId::JigThicknessMm:   return jigThickness_;
    default:                            return nullptr;
    }
}

void V2PartDock::SetParameterMm(ParameterId id, double value)
{
    QDoubleSpinBox* field = FieldFor(id);
    if (field == nullptr) {
        return;
    }
    // 便りを止めてから入れる。止めないと、映した値がそのまま打ち返されて回る。
    const bool blocked = field->blockSignals(true);
    field->setValue(value);
    field->blockSignals(blocked);
    if (id == ParameterId::ExtrudeDistance && thickness_ != nullptr) {
        const bool second = thickness_->blockSignals(true);
        thickness_->setValue(value);
        thickness_->blockSignals(second);
    }
}

double V2PartDock::ParameterMm(ParameterId id) const
{
    const QDoubleSpinBox* field = FieldFor(id);
    return field == nullptr ? 0.0 : field->value();
}

void V2PartDock::SetParameterHandler(
    std::function<void(ParameterId, double)> handler)
{
    parameterHandler_ = std::move(handler);
}

ThicknessPlacement V2PartDock::Placement() const
{
    switch (placement_->currentIndex()) {
    case 1:  return ThicknessPlacement::Centered;
    case 2:  return ThicknessPlacement::Inside;
    default: return ThicknessPlacement::Outside;
    }
}

void V2PartDock::SetPlacement(ThicknessPlacement value)
{
    const int index = value == ThicknessPlacement::Centered ? 1
        : (value == ThicknessPlacement::Inside ? 2 : 0);
    const bool blocked = placement_->blockSignals(true);
    placement_->setCurrentIndex(index);
    placement_->blockSignals(blocked);
}

void V2PartDock::SetPlacementHandler(std::function<void(ThicknessPlacement)> handler)
{
    placementHandler_ = std::move(handler);
}

void V2PartDock::SetRunHandler(std::function<void(const char*)> handler)
{
    runHandler_ = std::move(handler);
}

void V2PartDock::PressRun(const char* command)
{
    if (runHandler_) {
        runHandler_(command);
    }
}

void V2PartDock::SetSelectionText(const QString& text)
{
    selection_->setText(text);
}

QString V2PartDock::SelectionText() const
{
    return selection_->text();
}
