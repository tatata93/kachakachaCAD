#include "V2RevolveAxisInput.h"
#include "V2MainWindow.h"
#include "V2SurfaceDock.h"
#include "V2ParameterDock.h"
#include "V2NumericSpinBox.h"
#include "kachakacha/app/CommandParameters.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <QString>
#include <QObject>
#include <QPointer>

namespace {
struct Controls final : QObject {
    explicit Controls(V2SurfaceDock& dock) : QObject(&dock) {}
    QPointer<QWidget> row;
    QPointer<QDoubleSpinBox> angle;
};
Controls* Inputs(V2SurfaceDock& dock)
{
    for (auto* child : dock.children()) if (auto* inputs = dynamic_cast<Controls*>(child)) return inputs;
    return nullptr;
}
V2MainWindow* Owner(V2SurfaceDock& dock)
{
    for (auto* parent = static_cast<QWidget*>(&dock); parent; parent = parent->parentWidget())
        if (auto* window = dynamic_cast<V2MainWindow*>(parent)) return window;
    return nullptr;
}
}

void v2ui::InstallRevolveControls(V2SurfaceDock& dock, QVBoxLayout& layout)
{
    auto* row = new QWidget(dock.widget()); row->setObjectName(QStringLiteral("surfaceRevolveControls"));
    auto* line = new QHBoxLayout(row); line->setContentsMargins(0, 0, 0, 0);
    line->addWidget(new QLabel(QStringLiteral("回す角度"), row));
    auto* angle = new V2NumericSpinBox(row); angle->setObjectName(QStringLiteral("surfaceRevolveAngle"));
    auto* inputs = new Controls(dock); inputs->row = row; inputs->angle = angle;
    angle->setRange(.001, 360); angle->setDecimals(3); angle->setSuffix(QStringLiteral("°"));
    angle->setKeyboardTracking(false); line->addWidget(angle, 1);
    auto* full = new QPushButton(QStringLiteral("一周"), row); full->setObjectName(QStringLiteral("surfaceRevolveFullTurn"));
    full->setToolTip(QStringLiteral("360°回して一周する面を下見します。")); line->addWidget(full);
    auto* window = Owner(dock);
    QObject::connect(angle, &QDoubleSpinBox::valueChanged, &dock, [window](double value) {
        if (window) window->ParameterDock().Apply(kachakacha::v2::app::ParameterId::RevolveAngleDeg, QString::number(value, 'g', 15));
    });
    QObject::connect(full, &QPushButton::clicked, angle, [window, angle] {
        if (angle->value() != 360) angle->setValue(360);
        else if (window) window->ParameterDock().Apply(kachakacha::v2::app::ParameterId::RevolveAngleDeg, QStringLiteral("360"));
    });
    layout.addWidget(row); row->hide();
}

void v2ui::RefreshRevolveControls(V2SurfaceDock& dock, bool visible)
{
    const auto* inputs = Inputs(dock); if (!inputs) return;
    if (inputs->row) inputs->row->setVisible(visible);
    auto* window = Owner(dock);
    auto* angle = inputs->angle.data();
    if (window && angle) {
        const QSignalBlocker block(angle);
        angle->setValue(kachakacha::v2::app::ParameterValueOf(window->ParameterDock().Values(), kachakacha::v2::app::ParameterId::RevolveAngleDeg));
    }
}
