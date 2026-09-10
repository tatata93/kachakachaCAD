#pragma once

//! 測る棚(PRD-070〜072、V1同等性)。
//!
//! 何をどう測るかは core の MeasurePanel が決める。
//! ここは行を並べるだけで、値も文言も作らない。
//! 作ると、画面を出さずに確かめられなくなる。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。

#include "kachakacha/app/MeasurePanel.h"

#include <QDockWidget>

#include <functional>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

class V2MeasureDock final : public QDockWidget {
public:
    explicit V2MeasureDock(QWidget* parent);

    //! 測る対象を渡す。並びは作り直す。
    void SetRequest(const kachakacha::v2::app::MeasureRequest& request);

    //! 測り方(V1 の3モード + 選んだものから)。変えると handler が呼ばれる。
    [[nodiscard]] kachakacha::v2::app::MeasureMode Mode() const;
    void SetMode(kachakacha::v2::app::MeasureMode mode);
    void SetModeChangedHandler(std::function<void()> handler);
    //! 「寸法を残す」。名前の欄と押した時に呼ぶもの。
    [[nodiscard]] QString DimensionName() const;
    void SetDimensionName(const QString& name);
    void SetKeepHandler(std::function<void()> handler);
    void PressKeep();
    //! 「測定を消去」。
    void SetClearHandler(std::function<void()> handler);
    void PressClear();
    //! 残した寸法の数を出す。
    void SetKeptCount(int count);
    //! 出ている行。試験から見る。
    [[nodiscard]] int RowCount() const;
    [[nodiscard]] QString RowLabel(int row) const;
    [[nodiscard]] QString RowValue(int row) const;
    [[nodiscard]] QString SummaryText() const;

private:
    void Refresh();

    QComboBox* mode_ = nullptr;
    QLabel* summary_ = nullptr;
    QTreeWidget* rows_ = nullptr;
    QLineEdit* name_ = nullptr;
    QPushButton* keep_ = nullptr;
    QPushButton* clear_ = nullptr;
    QLabel* kept_ = nullptr;
    std::function<void()> modeChanged_;
    std::function<void()> keepHandler_;
    std::function<void()> clearHandler_;
    kachakacha::v2::app::MeasureRequest request_;
};
