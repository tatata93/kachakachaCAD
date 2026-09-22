#pragma once

//! 「辺の丸め・面取り」の棚(立体の辺のフィレット・面取り、matrix P-12)。
//!
//!   1. 作り方 … フィレット(R 丸め)/ 面取り(C)
//!   2. 入力   … 部品(3D で辺の近くを押す)と辺(何本でも)。欄ごとに「解除」
//!   3. 設定   … 半径 / 距離(mm)
//!   4. 状態   … 何を待っているか・生成可否・体積の変わり方・下見の様子
//! 下に キャンセル / 確定。
//!
//! 何が入るかは core(app/EdgeFinishInputState)が決める。ここは映して押すだけ。

#include "kachakacha/app/EdgeFinishInputState.h"

#include <QDockWidget>
#include <QString>

#include <array>
#include <functional>
#include <vector>

class QDoubleSpinBox;
class QLabel;
class QPushButton;

class V2EdgeFinishDock final : public QDockWidget {
public:
    explicit V2EdgeFinishDock(QWidget* parent);

    void ShowInput(const kachakacha::v2::app::EdgeFinishInputState& state,
        const QString& partNameJa, const std::vector<QString>& statusLinesJa, bool canConfirm);

    //! 作り方(0 フィレット / 1 面取り)を押した。
    void SetKindHandler(std::function<void(int)> handler);
    //! 半径 / 距離を打った。
    void SetSizeHandler(std::function<void(double)> handler);
    //! 部品の「解除」/ 辺の「解除」。
    void SetClearHandlers(std::function<void()> part, std::function<void()> edges);
    void SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel);

    //! **見えていて押せるボタンを実際に押す。**人の道の試験はこちらを使う。
    [[nodiscard]] bool ClickKind(int kind);
    [[nodiscard]] bool ClickClearEdges();
    [[nodiscard]] bool ClickConfirm();
    [[nodiscard]] bool TypeSize(double sizeMm);

    [[nodiscard]] QString PartTextJa() const;
    [[nodiscard]] QString EdgesTextJa() const;
    [[nodiscard]] QString SizeLabelJa() const;
    [[nodiscard]] QString StatusTextJa() const;
    [[nodiscard]] bool ConfirmEnabled() const;
    [[nodiscard]] int KindShown() const;

private:
    std::array<QPushButton*, 2> kinds_{};
    QLabel* partValue_ = nullptr;
    QPushButton* clearPart_ = nullptr;
    QLabel* edgesValue_ = nullptr;
    QPushButton* clearEdges_ = nullptr;
    QLabel* sizeLabel_ = nullptr;
    QDoubleSpinBox* size_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* confirm_ = nullptr;
    bool loading_ = false;

    std::function<void(int)> kindHandler_;
    std::function<void(double)> sizeHandler_;
    std::function<void()> clearPartHandler_;
    std::function<void()> clearEdgesHandler_;
    std::function<void()> confirmHandler_;
    std::function<void()> cancelHandler_;
};
