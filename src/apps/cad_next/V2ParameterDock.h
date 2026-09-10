#pragma once

//! 数の棚(板厚・面取り量・型紙の余白)。
//!
//! 何をいくつにするかの判断は core の CommandParameters にある。
//! ここは行を並べ、打たれた文字をそのまま core へ渡し、
//! core が返した理由をそのまま出すだけである。
//!
//! **断ったときは前の値へ戻す。** 打ち間違えた瞬間に値が消えると、
//! 何だったか思い出せなくなる。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。

#include "kachakacha/app/CommandParameters.h"

#include <QDockWidget>
#include <QString>

#include <functional>

class QTreeWidget;
class QTreeWidgetItem;

class V2ParameterDock final : public QDockWidget {
public:
    explicit V2ParameterDock(QWidget* parent);

    [[nodiscard]] const kachakacha::v2::app::ParameterSet& Values() const
    {
        return values_;
    }
    //! 知らせの出し先。断られた理由はここへ流す。
    void SetDiagnosticSink(std::function<void(const QString&)> sink);

    //! 打たれた文字で入れ替える。断ったら false を返し、値は変えない。
    bool Apply(kachakacha::v2::app::ParameterId id, const QString& text);
    //! 値が変わったあとに呼ぶもの(面取りの棚が同じ値を映す)。
    void SetChangedHandler(std::function<void()> handler);

    //! 出ている行。試験から見る。
    [[nodiscard]] int RowCount() const;
    [[nodiscard]] QString RowName(int row) const;
    [[nodiscard]] QString RowText(int row) const;

private:
    void Refresh();
    void OnItemChanged(QTreeWidgetItem* item, int column);

    QTreeWidget* rows_ = nullptr;
    kachakacha::v2::app::ParameterSet values_;
    std::function<void(const QString&)> sink_;
    std::function<void()> changed_;
};
