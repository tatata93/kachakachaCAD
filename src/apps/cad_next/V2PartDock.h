#pragma once

//! 部品の棚(オーナー指摘 2026-09-11「部品モードの時もツールの設定類は右に出せ」)。
//!
//! 作図モードには作図の棚が、製作モードには製作の棚があるのに、
//! 部品モードだけ右が「形状ガイドの役割の表」で、**道具の設定がどこにも無かった**。
//! 板厚も、厚みの付け方も、治具のすき間も、回転体の角度も、
//! 数の棚を探すか、押すたびに回る切替で当てるしかなかった。
//!
//! ここは V1 の部品タブに当たる。欄と、その欄を使うボタンを1枚に置く。
//! **値は数の棚と同じものを映す。** 別に持つと、数の棚で変えたのに
//! こちらが古いまま、ということが起きる。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。

#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/fabrication/FabricationSettings.h"

#include <QDockWidget>
#include <QString>

#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;

class V2PartDock final : public QDockWidget {
public:
    explicit V2PartDock(QWidget* parent);

    //! 数の棚と同じ値を映す。
    void SetParameterMm(kachakacha::v2::app::ParameterId id, double value);
    [[nodiscard]] double ParameterMm(kachakacha::v2::app::ParameterId id) const;
    //! 欄を打ったときに呼ぶもの(数の棚へ戻す)。
    void SetParameterHandler(
        std::function<void(kachakacha::v2::app::ParameterId, double)> handler);

    //! 厚みの付け方(外側 / 中央 / 内側)。
    [[nodiscard]] kachakacha::v2::fabrication::ThicknessPlacement Placement() const;
    void SetPlacement(kachakacha::v2::fabrication::ThicknessPlacement value);
    void SetPlacementHandler(
        std::function<void(kachakacha::v2::fabrication::ThicknessPlacement)> handler);

    //! ボタンを押したときに呼ぶもの。台帳のコマンドをそのまま渡す。
    void SetRunHandler(std::function<void(const char*)> handler);
    //! 試験から押す。
    void PressRun(const char* command);

    //! 選んでいるものの覚え書き(「面を1つ選んでいます」など)。
    void SetSelectionText(const QString& text);
    [[nodiscard]] QString SelectionText() const;

private:
    [[nodiscard]] QDoubleSpinBox* FieldFor(kachakacha::v2::app::ParameterId id) const;

    std::function<void(kachakacha::v2::app::ParameterId, double)> parameterHandler_;
    std::function<void(kachakacha::v2::fabrication::ThicknessPlacement)> placementHandler_;
    std::function<void(const char*)> runHandler_;

    QDoubleSpinBox* thickness_ = nullptr;
    QDoubleSpinBox* extrudeDistance_ = nullptr;
    QDoubleSpinBox* offsetDistance_ = nullptr;
    QDoubleSpinBox* revolveAngle_ = nullptr;
    QDoubleSpinBox* jigClearance_ = nullptr;
    QDoubleSpinBox* jigThickness_ = nullptr;
    QComboBox* placement_ = nullptr;
    QLabel* selection_ = nullptr;
};
