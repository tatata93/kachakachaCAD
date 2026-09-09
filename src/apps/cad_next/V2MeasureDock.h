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
#include <QString>

class QLabel;
class QTreeWidget;

class V2MeasureDock final : public QDockWidget {
public:
    explicit V2MeasureDock(QWidget* parent);

    //! 測る対象を渡す。並びは作り直す。
    void SetRequest(const kachakacha::v2::app::MeasureRequest& request);

    //! 出ている行。試験から見る。
    [[nodiscard]] int RowCount() const;
    [[nodiscard]] QString RowLabel(int row) const;
    [[nodiscard]] QString RowValue(int row) const;
    [[nodiscard]] QString SummaryText() const;

private:
    void Refresh();

    QLabel* summary_ = nullptr;
    QTreeWidget* rows_ = nullptr;
    kachakacha::v2::app::MeasureRequest request_;
};
