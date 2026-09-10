#pragma once

//! 数を1つ聞く窓。組立率や厚みなど、欄が1つで済むものに使う。
//! 大きな窓を作るほどではないが、帯に数字を打たせるのは分かりにくい、というときの器。

#include <QDialog>
#include <QString>

class QDoubleSpinBox;

class V2NumberDialog final : public QDialog {
public:
    V2NumberDialog(const QString& title, const QString& label, double initial,
        double minimum, double maximum, const QString& suffix, QWidget* parent);
    [[nodiscard]] double Value() const;
    //! 試験から呼ぶ。窓を出さずに同じ欄へ入れる。
    void SetValue(double value);

private:
    QDoubleSpinBox* value_ = nullptr;
};
