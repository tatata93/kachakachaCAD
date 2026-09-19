#pragma once

//! 「厚み」の棚(指示書 matrix P-10)。
//!
//! 4つの段を1枚に置く。
//!   1. 入力   … 面。1つだけの欄なので「選び直す」(押し直すのと同じ、外して待つ)
//!   2. 作り方 … 外側 / 中央 / 内側 / 平面まで(4枚目は相手の作業平面を選ぶ欄が出る)
//!   3. 厚み   … mm。「平面まで」のときは相手との距離で決まるので隠す
//!   4. 状態   … 何を待っているか・生成可否・体積・下見の様子
//! 下に キャンセル / 確定。
//!
//! 何がどの欄に入るかは core(app/ThickenInputState)が決める。ここは映して押すだけ
//! (V2BooleanDock と同じ役目分け)。

#include "V2ExtrudeTargetChoice.h"

#include "kachakacha/app/ThickenInputState.h"
#include "kachakacha/base/Ids.h"
#include "kachakacha/fabrication/FabricationSettings.h"

#include <QDockWidget>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

class QLabel;
class QPushButton;
class QDoubleSpinBox;
class QComboBox;
class QVBoxLayout;

class V2ThickenDock final : public QDockWidget {
public:
    explicit V2ThickenDock(QWidget* parent);

    //! いまの入力を映す。欄・カード・厚み・状態を一度に書き直す。
    void ShowInput(const kachakacha::v2::app::ThickenInputState& state,
        const QString& surfaceNameJa, const std::vector<QString>& statusLinesJa,
        bool canConfirm);

    //! 「平面まで」の相手に出せる作業平面。棚を出すときに文書から渡す。
    void SetTargets(const std::vector<ExtrudeTargetChoice>& targets);
    //! いま選んでいる相手。無ければ値を持たない。
    [[nodiscard]] std::optional<kachakacha::v2::base::EntityId> TargetEntityId() const;

    //! 「選び直す」を押した(面の欄を空にする)。
    void SetReselectHandler(std::function<void()> handler);
    //! 作り方のカードを押した(外側/中央/内側)。
    void SetPlacementHandler(
        std::function<void(kachakacha::v2::fabrication::ThicknessPlacement)> handler);
    //! 「平面まで」のカードを押した。
    void SetToPlaneHandler(std::function<void()> handler);
    //! 相手の作業平面を選んだ。
    void SetTargetHandler(
        std::function<void(const kachakacha::v2::base::EntityId&)> handler);
    //! 厚みの欄を打った。
    void SetThicknessHandler(std::function<void(double)> handler);
    //! 下の2つのボタン。
    void SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel);

    //! **見えているボタンを実際に押す。**人の道の試験はこちらを使う。
    //! 見えていなければ偽。不可視の widget を叩いて通したことにしない。
    [[nodiscard]] bool ClickReselect();
    [[nodiscard]] bool ClickPlacementCard(kachakacha::v2::fabrication::ThicknessPlacement value);
    [[nodiscard]] bool ClickToPlaneCard();
    [[nodiscard]] bool ClickConfirm();

    //! 欄に出ている面の名前。
    [[nodiscard]] QString SurfaceTextJa() const;
    //! 「状態」に出ている文。
    [[nodiscard]] QString StatusTextJa() const;
    //! 厚みの欄が見えているか(「平面まで」のときは隠す)。試験から見る。
    [[nodiscard]] bool ThicknessRowShown() const;
    //! 相手の作業平面の欄が見えているか(「平面まで」のときだけ出す)。
    [[nodiscard]] bool TargetRowShown() const;

private:
    void BuildPlacementCards(QVBoxLayout* layout);

    QLabel* surfaceValue_ = nullptr;
    QPushButton* reselect_ = nullptr;
    QPushButton* outsideCard_ = nullptr;
    QPushButton* centeredCard_ = nullptr;
    QPushButton* insideCard_ = nullptr;
    QPushButton* toPlaneCard_ = nullptr;
    QLabel* thicknessLabel_ = nullptr;
    QDoubleSpinBox* thickness_ = nullptr;
    QLabel* targetLabel_ = nullptr;
    QComboBox* target_ = nullptr;
    std::vector<ExtrudeTargetChoice> targets_;
    QLabel* status_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* confirm_ = nullptr;
    bool loading_ = false;

    std::function<void()> reselectHandler_;
    std::function<void(kachakacha::v2::fabrication::ThicknessPlacement)> placementHandler_;
    std::function<void()> toPlaneHandler_;
    std::function<void(const kachakacha::v2::base::EntityId&)> targetHandler_;
    std::function<void(double)> thicknessHandler_;
    std::function<void()> confirmHandler_;
    std::function<void()> cancelHandler_;
};
