#pragma once

//! 作図の右パネル(V1 の「作図」タブ)。
//!
//! 円弧の作り方(3点 / 両端+半径 / 始点+接線)、補助線として作図、指定した点を作図点として
//! 残す、そして「数値で線を作る」欄。core の ToolSettings と DirectWireEntry を
//! そのまま画面に出す。判断(足りるか・作れるか)は core が持ち、ここは欄を並べて
//! 値を集めるだけにする。

#include "kachakacha/app/DirectWireEntry.h"
#include "kachakacha/modeling/ToolController.h"

#include <QDockWidget>
#include <QString>

#include <array>
#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;

class V2DrawingDock final : public QDockWidget {
public:
    explicit V2DrawingDock(QWidget* parent);

    //! いまの欄から作った道具の設定(作業平面の向きは含まない。それは場面が持つ)。
    [[nodiscard]] kachakacha::v2::modeling::ToolSettings Settings() const;
    //! 欄へ入れる(試験と復元)。
    void SetSettings(const kachakacha::v2::modeling::ToolSettings& settings);
    //! 設定が変わったときに呼ぶもの。
    void SetSettingsHandler(
        std::function<void(const kachakacha::v2::modeling::ToolSettings&)> handler);

    //! 「数値で線を作る」の欄から作った要求。
    [[nodiscard]] kachakacha::v2::app::DirectWireRequest DirectWire() const;
    [[nodiscard]] QString DirectWireName() const;
    void SetDirectWire(const kachakacha::v2::app::DirectWireRequest& request,
        const QString& name);
    //! 「線を作る」を押したときに呼ぶもの。
    void SetCreateWireHandler(std::function<void()> handler);
    //! 「線を作る」を押したのと同じ(試験用)。
    void PressCreateWire();
    //! 数値の線の種類の位置(試験用)。
    void SetDirectWireKindIndex(int index);
    //! 作れなかった理由を出す。
    void ShowMessage(const QString& text);

private:
    void BuildArcRows(QFormLayout* form);
    void BuildDirectWireRows(QFormLayout* form);
    void ApplyArcVisibility();
    void ApplyDirectWireVisibility();
    void EmitSettings();
    [[nodiscard]] std::array<QDoubleSpinBox*, 3> AddVectorRow(QFormLayout* form,
        const QString& label);

    QWidget* body_ = nullptr;
    QFormLayout* toolForm_ = nullptr;
    QFormLayout* wireForm_ = nullptr;
    QComboBox* arcMode_ = nullptr;
    QDoubleSpinBox* arcRadius_ = nullptr;
    QDoubleSpinBox* arcSweep_ = nullptr;
    QCheckBox* construction_ = nullptr;
    QCheckBox* keepPoints_ = nullptr;
    QComboBox* wireKind_ = nullptr;
    std::array<std::array<QDoubleSpinBox*, 3>, 4> wirePoints_{};
    QDoubleSpinBox* wireRadius_ = nullptr;
    QLineEdit* wireName_ = nullptr;
    QCheckBox* wireConstruction_ = nullptr;
    QPushButton* createWire_ = nullptr;
    QLabel* message_ = nullptr;
    std::function<void(const kachakacha::v2::modeling::ToolSettings&)> settingsHandler_;
    std::function<void()> createWireHandler_;
    bool loading_ = false;
};
