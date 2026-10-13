#pragma once
#include "kachakacha/app/SolidOutputMode.h"
#include <QComboBox>
#include <QString>
#include <functional>

//! Changing the output presentation never invalidates native geometry.
class V2SolidOutputSelector final : public QComboBox {
public:
    explicit V2SolidOutputSelector(QWidget* parent, const char* name) : QComboBox(parent) {
        setObjectName(QString::fromLatin1(name));
        addItems({QStringLiteral("ソリッドのみ"), QStringLiteral("ワイヤーのみ"), QStringLiteral("両方（ソリッド＋ワイヤー）")});
        setToolTip(QStringLiteral("ワイヤーは出来上がりの境界から作ります。生成対象だけの変更では形状を再計算しません。"));
        QObject::connect(this, &QComboBox::currentIndexChanged, this, [this] { if (changed) changed(); });
    }
    kachakacha::v2::app::SolidOutputMode Mode() const {
        return static_cast<kachakacha::v2::app::SolidOutputMode>(currentIndex());
    }
    std::function<void()> changed;
};
