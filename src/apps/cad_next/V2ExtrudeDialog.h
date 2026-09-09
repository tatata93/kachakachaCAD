#pragma once

//! 押し出しの選択肢を出す窓。
//!
//! core は向き7通り・終端5通り(「ある面まで」を含む)・出力3通り・部品演算3通りを
//! 持っているのに、画面はそれぞれ1通りに固定していた。
//! 工程の案内には「方向 → 終端 → 出力」と書いてあるのに選べない、という
//! 食い違いになっていた。ここで選べるようにする。
//!
//! 通るかどうかの判断は core(app/ExtrudeOptions.h)にある。
//! この窓は、欄を出して、答えを受け取って、断り文をそのまま出すだけにする。

#include "kachakacha/app/ExtrudeOptions.h"

#include <QDialog>
#include <QString>

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

//! 相手に選べる作業平面。名前と id の組。
struct ExtrudeTargetChoice {
    kachakacha::v2::base::EntityId entityId;
    QString labelJa;
};

class V2ExtrudeDialog final : public QDialog {
public:
    V2ExtrudeDialog(const kachakacha::v2::app::ExtrudeChoice& initial,
        const kachakacha::v2::app::ExtrudeFacts& facts,
        std::vector<ExtrudeTargetChoice> targets, QWidget* parent);

    //! いま欄に入っている選択。窓を閉じた後に読む。
    [[nodiscard]] kachakacha::v2::app::ExtrudeChoice Choice() const;

    //! 試験から呼ぶ。窓を出さずに、同じ道で欄を埋めて確かめる。
    void SetDirectionIndex(int index);
    void SetExtentIndex(int index);
    void SetBooleanIndex(int index);
    void SetDistanceMm(double value);
    void SetOutputs(bool part, bool endWire, bool sideWires);
    //! 欄の出し入れと、いま何が起きるかの一文を作り直す。
    void Refresh();
    //! いまの選択が通るか。通らないなら断り文が入る。
    [[nodiscard]] bool CurrentChoiceIsValid(QString* reasonOut) const;

private:
    kachakacha::v2::app::ExtrudeFacts facts_;
    std::vector<ExtrudeTargetChoice> targets_;
    QComboBox* direction_ = nullptr;
    QComboBox* extent_ = nullptr;
    QComboBox* boolean_ = nullptr;
    QComboBox* target_ = nullptr;
    QDoubleSpinBox* distance_ = nullptr;
    QDoubleSpinBox* secondDistance_ = nullptr;
    QCheckBox* reversed_ = nullptr;
    QCheckBox* makePart_ = nullptr;
    QCheckBox* makeEndWire_ = nullptr;
    QCheckBox* makeSideWires_ = nullptr;
    QCheckBox* zeroConfirmed_ = nullptr;
    QLabel* summary_ = nullptr;
};
