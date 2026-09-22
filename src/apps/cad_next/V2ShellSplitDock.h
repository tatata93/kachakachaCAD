#pragma once

//! 「シェル・分割」の棚(部品の形状編集、matrix P-13)。
//!
//!   1. 作り方 … シェル(面を抜いて肉厚を残す)/ 分割(作業平面で 2 つに分ける)
//!   2. 入力   … 部品(3D で押す)と、シェルなら抜く面(何枚でも)。欄ごとに「解除」
//!   3. 設定   … シェル: 肉厚(mm)。分割: 平面(いまの作業平面)と、そこからずらす量(mm)
//!   4. 状態   … 何を待っているか・生成可否・体積・下見の様子
//! 下に キャンセル / 確定。作り方に関係の無い欄は隠す(押せない欄を見せない)。
//!
//! 何が入るかは core(app/ShellSplitInputState)が決める。ここは映して押すだけ。

#include "kachakacha/app/ShellSplitInputState.h"

#include <QDockWidget>
#include <QString>

#include <array>
#include <functional>
#include <vector>

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QWidget;

class V2ShellSplitDock final : public QDockWidget {
public:
    explicit V2ShellSplitDock(QWidget* parent);

    void ShowInput(const kachakacha::v2::app::ShellSplitInputState& state,
        const QString& partNameJa, const QString& planeTextJa,
        const std::vector<QString>& statusLinesJa, bool canConfirm);

    //! 作り方(0 シェル / 1 分割)を押した。
    void SetMethodHandler(std::function<void(int)> handler);
    //! 肉厚 / ずらす量を打った。
    void SetValueHandlers(std::function<void(double)> thickness, std::function<void(double)> offset);
    //! 部品の「解除」/ 面の「解除」。
    void SetClearHandlers(std::function<void()> part, std::function<void()> faces);
    void SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel);

    //! **見えていて押せるボタンを実際に押す。**人の道の試験はこちらを使う。
    [[nodiscard]] bool ClickMethod(int method);
    [[nodiscard]] bool ClickClearFaces();
    [[nodiscard]] bool ClickConfirm();
    [[nodiscard]] bool TypeThickness(double thicknessMm);
    [[nodiscard]] bool TypeOffset(double offsetMm);

    [[nodiscard]] QString PartTextJa() const;
    [[nodiscard]] QString FacesTextJa() const;
    [[nodiscard]] QString PlaneTextJa() const;
    [[nodiscard]] QString StatusTextJa() const;
    [[nodiscard]] bool ConfirmEnabled() const;
    [[nodiscard]] bool FacesRowShown() const;
    [[nodiscard]] bool PlaneRowShown() const;
    [[nodiscard]] int MethodShown() const;

private:
    std::array<QPushButton*, 2> methods_{};
    QLabel* partValue_ = nullptr;
    QPushButton* clearPart_ = nullptr;
    QWidget* facesRow_ = nullptr;
    QLabel* facesValue_ = nullptr;
    QPushButton* clearFaces_ = nullptr;
    QWidget* thicknessRow_ = nullptr;
    QDoubleSpinBox* thickness_ = nullptr;
    QWidget* planeRow_ = nullptr;
    QLabel* planeValue_ = nullptr;
    QWidget* offsetRow_ = nullptr;
    QDoubleSpinBox* offset_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* confirm_ = nullptr;
    bool loading_ = false;

    std::function<void(int)> methodHandler_;
    std::function<void(double)> thicknessHandler_;
    std::function<void(double)> offsetHandler_;
    std::function<void()> clearPartHandler_;
    std::function<void()> clearFacesHandler_;
    std::function<void()> confirmHandler_;
    std::function<void()> cancelHandler_;
};
