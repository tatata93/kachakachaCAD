#pragma once

//! 並んだ候補から1つ選ぶ窓。形状ガイドの作り方(7通り)や、行の役割に使う。
//! 候補の文言は core が決める。この窓は並べて選ばせるだけにする。

#include <QDialog>
#include <QString>
#include <QStringList>

class QComboBox;

class V2ChoiceDialog final : public QDialog {
public:
    V2ChoiceDialog(const QString& title, const QString& label, const QStringList& items,
        int initialIndex, QWidget* parent);
    [[nodiscard]] int ChosenIndex() const;
    //! 試験から呼ぶ。窓を出さずに同じ欄へ入れる。
    void SetChosenIndex(int index);

private:
    QComboBox* items_ = nullptr;
};
