#pragma once

//! 「足す・引く・交差」の棚(引継ぎ 2026-09-17 の 4、交差は P-17)。
//!
//! 3つの段を1枚に置く。
//!   1. 作り方 … 足す / 引く / 交差(押して切り替える)
//!   2. 入力   … 土台 / 相手。欄ごとに「ここへ選ぶ」(押された形 = 次のクリックが入る欄)と「解除」
//!   3. 状態   … 何を待っているか・生成可否・体積の変わり方・下見の様子
//! 下に キャンセル / 確定。
//!
//! 何がどの欄に入るかは core(app/BooleanInputState)が決める。ここは映して押すだけ。

#include "kachakacha/app/BooleanInputState.h"

#include <QDockWidget>
#include <QString>

#include <functional>
#include <vector>

class QLabel;
class QPushButton;
class QVBoxLayout;

class V2BooleanDock final : public QDockWidget {
public:
    explicit V2BooleanDock(QWidget* parent);

    //! いまの入力を映す。操作・欄・状態を一度に書き直す。
    void ShowInput(const kachakacha::v2::app::BooleanInputState& state,
        const QString& targetNameJa, const QString& toolNameJa,
        const std::vector<QString>& statusLinesJa, bool canConfirm);

    //! 足す / 引く / 交差を押した。
    void SetOperationHandler(std::function<void(kachakacha::v2::app::BooleanKind)> handler);
    //! その欄の「ここへ選ぶ」を押した。
    void SetActivateHandler(std::function<void(kachakacha::v2::app::BooleanSlot)> handler);
    //! その欄の「解除」を押した。
    void SetClearHandler(std::function<void(kachakacha::v2::app::BooleanSlot)> handler);
    //! 下の2つのボタン。
    void SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel);

    //! **見えているボタンを実際に押す。**人の道の試験はこちらを使う。
    //! 見えていなければ偽。不可視の widget を叩いて通したことにしない。
    [[nodiscard]] bool ClickOperation(kachakacha::v2::app::BooleanKind kind);
    [[nodiscard]] bool ClickActivate(kachakacha::v2::app::BooleanSlot slot);
    [[nodiscard]] bool ClickClear(kachakacha::v2::app::BooleanSlot slot);
    [[nodiscard]] bool ClickConfirm();

    //! いま「ここへ選ぶ」が押された形で出ている欄。
    [[nodiscard]] kachakacha::v2::app::BooleanSlot ActiveSlotShown() const;
    //! 欄に出ている名前。
    [[nodiscard]] QString TargetTextJa() const;
    [[nodiscard]] QString ToolTextJa() const;
    //! 押された形で出ている作り方。
    [[nodiscard]] kachakacha::v2::app::BooleanKind KindShown() const;
    //! 「状態」に出ている文。
    [[nodiscard]] QString StatusTextJa() const;

private:
    [[nodiscard]] QPushButton* ArmFor(kachakacha::v2::app::BooleanSlot slot) const;
    [[nodiscard]] QPushButton* ClearFor(kachakacha::v2::app::BooleanSlot slot) const;
    void BuildRows(QVBoxLayout* layout);

    QPushButton* add_ = nullptr;
    QPushButton* cut_ = nullptr;
    QPushButton* intersect_ = nullptr;
    QLabel* targetValue_ = nullptr;
    QLabel* toolValue_ = nullptr;
    QPushButton* armTarget_ = nullptr;
    QPushButton* armTool_ = nullptr;
    QPushButton* clearTarget_ = nullptr;
    QPushButton* clearTool_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* confirm_ = nullptr;
    bool loading_ = false;

    std::function<void(kachakacha::v2::app::BooleanKind)> operationHandler_;
    std::function<void(kachakacha::v2::app::BooleanSlot)> activateHandler_;
    std::function<void(kachakacha::v2::app::BooleanSlot)> clearHandler_;
    std::function<void()> confirmHandler_;
    std::function<void()> cancelHandler_;
};
