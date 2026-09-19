#pragma once

//! 作図の右パネル(V1 の「作図」タブ)。
//!
//! 円弧の作り方(3点 / 両端+半径 / 始点+接線)、補助線として作図、指定した点を作図点として
//! 残す、そして「数値で線を作る」欄。core の ToolSettings と DirectWireEntry を
//! そのまま画面に出す。判断(足りるか・作れるか)は core が持ち、ここは欄を並べて
//! 値を集めるだけにする。

#include "kachakacha/app/DirectWireEntry.h"
#include "kachakacha/app/DrawingMethodCards.h"
#include "kachakacha/app/DrawingShelfRows.h"
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
class QTabWidget;
class QToolButton;
class QGridLayout;
class QHBoxLayout;

class V2DrawingDock final : public QDockWidget {
public:
    explicit V2DrawingDock(QWidget* parent);

    //! いまの道具を伝える。棚の見出しと、出す欄がこれで決まる。
    //!
    //! 道具を替えても棚の中身が変わらず、ベジェ曲線に持ち替えても
    //! 右に「円弧の作り方」が出たままだった(オーナー指摘 2026-09-13)。
    void SetTool(kachakacha::v2::modeling::DrawingTool tool);
    //! いま棚が向いている道具。試験から見る。
    [[nodiscard]] kachakacha::v2::modeling::DrawingTool Tool() const noexcept
    {
        return tool_;
    }
    //! 道具ごとの欄が1つも出ていないとき、代わりに出している使い方の一文。
    [[nodiscard]] QString HintText() const;
    [[nodiscard]] QString ActiveToolText() const;
    [[nodiscard]] int InputModeIndex() const;
    void SetInputModeIndex(int index);

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

    // ---- 作り方カード(正本の methods)。試験と場面づくりから ----
    //! いま並んでいるカードの言葉(順に)。
    [[nodiscard]] std::vector<QString> MethodLabels() const;
    //! いま押されているカードの言葉。無ければ空。
    [[nodiscard]] QString CurrentMethodLabel() const;
    //! そのカードが押せる形か。
    [[nodiscard]] bool MethodEnabled(const QString& labelJa) const;
    //! そのカードのツールチップ(押せないときは理由)。
    [[nodiscard]] QString MethodTip(const QString& labelJa) const;
    //! **見えているカードを実際に押す。** 押せなければ偽(理由は状態欄へ)。
    [[nodiscard]] bool ClickMethod(const QString& labelJa);
    //! 押せないカードを押したときに呼ぶもの(理由を状態行へ出すため)。
    void SetBlockedMethodHandler(std::function<void(const QString& reasonJa)> handler);

private:
    void BuildArcRows(QFormLayout* form);
    void RebuildMethodCards();
    void ChooseMethod(int index);
    [[nodiscard]] QToolButton* MethodButton(const QString& labelJa) const;
    void BuildDirectWireRows(QFormLayout* form);
    void ApplyArcVisibility();
    void ApplyToolRows();
    void ApplyDirectWireVisibility();
    void EmitSettings();
    [[nodiscard]] std::array<QDoubleSpinBox*, 3> AddVectorRow(QFormLayout* form,
        const QString& label);

    QWidget* body_ = nullptr;
    QFormLayout* toolForm_ = nullptr;
    QFormLayout* wireForm_ = nullptr;
    //! 作り方カード。円弧のカードは arcMode_ を決める。
    QLabel* methodTitle_ = nullptr;
    QWidget* methodRow_ = nullptr;
    QGridLayout* methodLayout_ = nullptr;   //!< 作り方カード(3 枚ごとに折り返す)
    std::vector<QToolButton*> methodButtons_;
    std::vector<kachakacha::v2::app::DrawingMethodCard> methodCards_;
    int methodIndex_ = -1;
    kachakacha::v2::modeling::ArcMode arcMode_ = kachakacha::v2::modeling::ArcMode::ThreePoints;
    std::function<void(const QString&)> blockedMethodHandler_;
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
    QLabel* activeTool_ = nullptr;
    QTabWidget* inputModes_ = nullptr;
    //! 道具の区画の見出し。欄が無いときは使い方の一文になる。
    QLabel* toolTitle_ = nullptr;
    QLabel* hint_ = nullptr;
    kachakacha::v2::modeling::DrawingTool tool_ =
        kachakacha::v2::modeling::DrawingTool::Select;
    std::function<void(const kachakacha::v2::modeling::ToolSettings&)> settingsHandler_;
    std::function<void()> createWireHandler_;
    bool loading_ = false;
};
