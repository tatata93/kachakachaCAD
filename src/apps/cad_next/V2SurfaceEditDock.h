#pragma once

//! 「面の編集」の棚(プロンプト additional_surface_tools)。
//!
//!   1. 作り方 … 面を合わせる / 面をつなぐ / 面を整える / 対称に写す / U/V 線 / 面へ投影
//!   2. 入力   … 入れたものの一覧(欄・名前)。行を選んで「外す」、「すべて解除」。
//!               **欄は固定で増やさない**(縁 2 本・面 1〜任意・線 1〜任意を一覧で出す)
//!   3. 設定   … 滑らかさ(縁 A / 縁 B)、許容、張り、U/V の向きと本数、対称面
//!   4. 状態   … 何を待っているか・作れるか・測った値
//! 下に キャンセル / 確定。
//!
//! 何がどの欄に入るかは core(app/SurfaceEditInputState)が決める。ここは映して押すだけ。

#include "kachakacha/app/SurfaceEditInputState.h"
#include "kachakacha/base/Ids.h"

#include <QDockWidget>
#include <QString>

#include <array>
#include <functional>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QVBoxLayout;

class V2SurfaceEditDock final : public QDockWidget {
public:
    //! 一覧の 1 行。
    struct Entry {
        QString slotJa;
        QString nameJa;
        kachakacha::v2::base::EntityId id;
    };

    explicit V2SurfaceEditDock(QWidget* parent);

    //! いまの入力を映す。カード・一覧・設定・状態を一度に書き直す。
    void ShowInput(const kachakacha::v2::app::SurfaceEditInputState& state,
        const std::vector<Entry>& entries, const std::vector<QString>& statusLinesJa,
        bool canConfirm);

    void SetOperationHandler(
        std::function<void(kachakacha::v2::app::SurfaceEditOperation)> handler);
    void SetRemoveHandler(std::function<void(const kachakacha::v2::base::EntityId&)> handler);
    void SetClearHandler(std::function<void()> handler);
    //! 設定のどれかを変えた。棚の値を写した入力を渡す(作り方と入力はそのまま)。
    void SetOptionsHandler(
        std::function<void(const kachakacha::v2::app::SurfaceEditInputState&)> handler);
    void SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel);

    //! **見えているボタンを実際に押す。**人の道の試験はこちらを使う(見えていなければ偽)。
    [[nodiscard]] bool ClickOperation(kachakacha::v2::app::SurfaceEditOperation operation);
    [[nodiscard]] bool ClickRemoveRow(int row);
    [[nodiscard]] bool ClickClear();
    [[nodiscard]] bool ClickConfirm();
    [[nodiscard]] bool ClickCancel();
    //! 滑らかさの欄を選ぶ(second = 縁 B)。見えていなければ偽。
    [[nodiscard]] bool ChooseContinuity(bool second,
        kachakacha::v2::modeling::SurfaceContinuity value);
    [[nodiscard]] bool TypeTolerance(double millimetres);
    [[nodiscard]] bool TypeTension(double value);
    [[nodiscard]] bool ChooseIso(int direction, int count);
    [[nodiscard]] bool ChooseMirrorPlane(kachakacha::v2::app::MirrorPlaneChoice choice);

    [[nodiscard]] std::vector<QString> EntryTexts() const;
    [[nodiscard]] QString StatusTextJa() const;
    [[nodiscard]] bool ContinuityRowShown(bool second) const;
    [[nodiscard]] bool ConfirmEnabled() const;

private:
    void BuildOperationCards(QVBoxLayout* layout);
    void BuildOptions(QVBoxLayout* layout);
    void EmitOptions();

    std::array<QPushButton*, 6> cards_{};
    QTreeWidget* entries_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* clear_ = nullptr;
    QLabel* continuityALabel_ = nullptr;
    QComboBox* continuityA_ = nullptr;
    QLabel* continuityBLabel_ = nullptr;
    QComboBox* continuityB_ = nullptr;
    QLabel* toleranceLabel_ = nullptr;
    QDoubleSpinBox* tolerance_ = nullptr;
    QLabel* tensionLabel_ = nullptr;
    QDoubleSpinBox* tension_ = nullptr;
    QLabel* isoLabel_ = nullptr;
    QComboBox* isoDirection_ = nullptr;
    QSpinBox* isoCount_ = nullptr;
    QLabel* mirrorLabel_ = nullptr;
    QComboBox* mirrorPlane_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* confirm_ = nullptr;
    bool loading_ = false;
    kachakacha::v2::app::SurfaceEditInputState shown_;
    std::vector<Entry> shownEntries_;

    std::function<void(kachakacha::v2::app::SurfaceEditOperation)> operationHandler_;
    std::function<void(const kachakacha::v2::base::EntityId&)> removeHandler_;
    std::function<void()> clearHandler_;
    std::function<void(const kachakacha::v2::app::SurfaceEditInputState&)> optionsHandler_;
    std::function<void()> confirmHandler_;
    std::function<void()> cancelHandler_;
};
