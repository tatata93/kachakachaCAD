#pragma once

//! 「立体を作る」の棚(回転体・ロフト立体・スイープ、matrix P-08/P-09)。
//!
//! 正本(部品モードの HTML)の並びどおり 4 つの段を 1 枚に置く。
//!   1. 作り方 … 回転体: 全回転 / 角度指定 / 対称回転
//!               ロフト立体: 断面のみ / ガイド付き / 中心線付き
//!               スイープ: 一定断面 / ねじれ指定 / ガイド付き
//!               **まだ作れない作り方は押せない形で理由を添えて置く**(押せるのに何も
//!               起きないボタンを作らない)。
//!   2. 入力   … 輪郭(ロフト立体は断面)と、回転軸 / 経路。欄ごとに「ここへ選ぶ」と「解除」
//!   3. 設定   … 角度(全回転のときは 360° で固定)・姿勢(スイープ)・操作(新しい部品 /
//!               足す / 引く)と、足す・引くのときだけ出る相手の欄
//!   4. 状態   … 何を待っているか・生成可否・体積・下見の様子
//! 下に キャンセル / 確定。
//!
//! 何がどの欄に入るかは core(app/SolidInputState)が決める。ここは映して押すだけ
//! (V2BooleanDock と同じ役目分け)。

#include "kachakacha/app/SolidInputState.h"

#include <QDockWidget>
#include <QString>

#include <array>
#include <functional>
#include <vector>

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

class V2SolidDock final : public QDockWidget {
public:
    explicit V2SolidDock(QWidget* parent);

    //! いまの入力を映す。作り方・欄・設定・状態を一度に書き直す。
    //! `secondNamesJa` は回転軸(回転体)か経路(スイープ)の名前。
    void ShowInput(const kachakacha::v2::app::SolidInputState& state,
        const QString& profileNamesJa, const QString& secondNamesJa, const QString& targetNameJa,
        const std::vector<QString>& statusLinesJa, bool canConfirm);

    //! 回転体の作り方のカード(全回転 / 角度指定 / 対称回転)を押した。
    void SetRevolveModeHandler(std::function<void(kachakacha::v2::app::RevolveMode)> handler);
    //! 角度(度)を打った。
    void SetAngleHandler(std::function<void(double)> handler);
    //! 操作(0 = 新しい部品、1 = 足す、2 = 引く)を押した。
    void SetBooleanHandler(std::function<void(int)> handler);
    //! その欄の「ここへ選ぶ」を押した。
    void SetActivateHandler(std::function<void(kachakacha::v2::app::SolidSlot)> handler);
    //! その欄の「解除」を押した。
    void SetClearHandler(std::function<void(kachakacha::v2::app::SolidSlot)> handler);
    //! 下の2つのボタン。
    void SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel);

    //! **見えていて押せるボタンを実際に押す。**人の道の試験はこちらを使う。
    //! 見えていない・押せない(まだ作れない作り方など)なら偽。
    [[nodiscard]] bool ClickMethodCard(int index);
    [[nodiscard]] bool ClickBoolean(int booleanMode);
    [[nodiscard]] bool ClickActivate(kachakacha::v2::app::SolidSlot slot);
    [[nodiscard]] bool ClickClear(kachakacha::v2::app::SolidSlot slot);
    [[nodiscard]] bool ClickConfirm();
    //! 角度の欄に打つ(見えていて打てるときだけ。打てたら真)。
    [[nodiscard]] bool TypeAngle(double degrees);

    //! 作り方のカードの字と、押せるか(試験と台帳の突き合わせ用)。
    [[nodiscard]] QString MethodCardTextJa(int index) const;
    [[nodiscard]] bool MethodCardEnabled(int index) const;
    [[nodiscard]] bool MethodCardChecked(int index) const;
    //! 押せないカードの理由(ツールチップと同じ字)。
    [[nodiscard]] QString MethodCardWhyJa(int index) const;
    //! 見出しに出ている道具の名前(回転体 / ロフト立体 / スイープ)。
    [[nodiscard]] QString ToolNameJa() const;
    //! いま「ここへ選ぶ」が押された形で出ている欄。
    [[nodiscard]] kachakacha::v2::app::SolidSlot ActiveSlotShown() const;
    //! 欄に出ている名前。見えていない欄は空。
    [[nodiscard]] QString SlotTextJa(kachakacha::v2::app::SolidSlot slot) const;
    [[nodiscard]] bool SlotRowShown(kachakacha::v2::app::SolidSlot slot) const;
    //! 角度の欄が打てるか(全回転では 360° で固定、ロフト立体・スイープでは隠す)。
    [[nodiscard]] bool AngleEditable() const;
    //! 「状態」に出ている文。
    [[nodiscard]] QString StatusTextJa() const;
    //! 確定が押せるか。
    [[nodiscard]] bool ConfirmEnabled() const;

private:
    struct SlotRow {
        QLabel* name = nullptr;
        QLabel* value = nullptr;
        QPushButton* arm = nullptr;
        QPushButton* clear = nullptr;
    };

    void BuildMethods(QVBoxLayout* layout);
    void BuildInputs(QVBoxLayout* layout);
    void BuildSettings(QVBoxLayout* layout);
    void BuildSlotRow(QVBoxLayout* layout, SlotRow& row, kachakacha::v2::app::SolidSlot slot);
    [[nodiscard]] const SlotRow* RowFor(kachakacha::v2::app::SolidSlot slot) const;
    void ShowMethods(const kachakacha::v2::app::SolidInputState& state);
    void ShowSlots(const kachakacha::v2::app::SolidInputState& state,
        const QString& profileNamesJa, const QString& secondNamesJa, const QString& targetNameJa);

    QLabel* toolName_ = nullptr;
    std::array<QPushButton*, 3> cards_{};
    QLabel* methodNote_ = nullptr;
    SlotRow profile_;
    SlotRow second_;   //!< 回転軸(回転体)/ 経路(スイープ)。ロフト立体では隠す
    kachakacha::v2::app::SolidSlot secondSlot_ = kachakacha::v2::app::SolidSlot::Axis;
    QLabel* angleLabel_ = nullptr;
    QDoubleSpinBox* angle_ = nullptr;
    QLabel* poseLabel_ = nullptr;
    std::array<QPushButton*, 3> booleans_{};
    SlotRow target_;
    QLabel* status_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* confirm_ = nullptr;
    kachakacha::v2::modeling::SolidMethod method_ = kachakacha::v2::modeling::SolidMethod::Revolve;
    bool loading_ = false;

    std::function<void(kachakacha::v2::app::RevolveMode)> revolveModeHandler_;
    std::function<void(double)> angleHandler_;
    std::function<void(int)> booleanHandler_;
    std::function<void(kachakacha::v2::app::SolidSlot)> activateHandler_;
    std::function<void(kachakacha::v2::app::SolidSlot)> clearHandler_;
    std::function<void()> confirmHandler_;
    std::function<void()> cancelHandler_;
};
