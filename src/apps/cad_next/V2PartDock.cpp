#include "V2PartDock.h"
#include "V2PanelFrame.h"
#include "kachakacha/app/CommandCatalog.h"

#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QString>
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
    form_ = form;
    layout->addLayout(form);

    extrudeDistance_ = MakeField(body, -10000.0, 10000.0, QStringLiteral(" mm"));
    extrudeDistance_->setToolTip(QStringLiteral(
        "押し出しの距離。板厚とは別の数です。"));
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

    auto* footer = new QWidget(this);
    auto* footerLayout = new QVBoxLayout(footer);
    QPushButton* cancel = nullptr;
    footerLayout->addLayout(MakeCancelConfirmRow(footer, &cancel, &confirm_));
    QObject::connect(cancel, &QPushButton::clicked, this, [this] { HandleKey(Qt::Key_Escape); });
    QObject::connect(confirm_, &QPushButton::clicked, this, [this] {
        const auto command = command_;
        if (!command.empty()) PressRun(command.c_str());
    });
    layout->addStretch(1);

    const auto notify = [this](ParameterId id, QDoubleSpinBox* field) {
        QObject::connect(field, &QDoubleSpinBox::valueChanged, this, [this, id, field] {
            if (parameterHandler_) {
                parameterHandler_(id, field->value());
            }
        });
    };
    notify(ParameterId::ExtrudeLengthMm, extrudeDistance_);
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

    setWidget(MakeScrollableToolPanel(body, footer));
    FocusCommand({});
}

QDoubleSpinBox* V2PartDock::FieldFor(ParameterId id) const
{
    switch (id) {
    // **板厚と押し出しの距離は別の数である。**
    // 同じ数にしていたので、板厚の上限 20mm が押し出しへ漏れ、
    // 100mm 級の形を押せなかった。
    case ParameterId::ExtrudeLengthMm:  return extrudeDistance_;
    case ParameterId::ExtrudeDistance:  return thickness_;
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

void V2PartDock::FocusCommand(std::string_view command)
{
    command_ = std::string(command);
    const bool jig = command == "part.surface_jig";
    form_->setRowVisible(extrudeDistance_, command == "part.extrude");
    form_->setRowVisible(thickness_, command == "part.thicken");
    form_->setRowVisible(placement_, command == "part.thicken");
    form_->setRowVisible(offsetDistance_, command == "surface.offset");
    form_->setRowVisible(revolveAngle_, command == "guide.revolve");
    form_->setRowVisible(jigClearance_, jig);
    form_->setRowVisible(jigThickness_, jig);
    const auto* spec = kachakacha::v2::app::FindCommand(command);
    confirm_->setText(spec ? QString::fromUtf8(std::string(spec->labelJa).c_str()) : QStringLiteral("確定 Enter"));
}

bool V2PartDock::HandleKey(int key)
{
    if (!ToolActive()) return false;
    if (key == Qt::Key_Escape) { return true; }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) { confirm_->click(); return true; }
    return false;
}
