#pragma once

//! グリッドの右パネル(V1 の「グリッド」欄と同じ項目)。
//!
//! 表示、主点間隔(式が書ける)、副点(主点のみ / 1/2 / 1/3 / 1/4)、基準 X/Y、
//! 基準を 0,0 に戻す、原点を画面で指す、作図モード以外でも表示、作図面以外の線を常に薄く、
//! 主点色・副点色・背景色。
//!
//! グリッドは見え方の都合なので文書には入れない。値の検査(間隔が正か、副点の数)は
//! core(modeling/GridModel)が持ち、ここは欄を並べて集めるだけにする。

#include "kachakacha/modeling/GridModel.h"

#include <QColor>
#include <QDockWidget>
#include <QString>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;

//! 棚で決めること。グリッドの定義 + 見え方の印。
struct V2GridChoice {
    kachakacha::v2::modeling::GridDefinition grid;
    //! 主点間隔の式(そのまま)。評価した値は grid.majorSpacingMm。
    QString spacingExpression;
    bool showInAllModes = true;
    bool dimOffPlaneLines = true;
    QColor majorColor;
    QColor minorColor;
    QColor backgroundColor;
};

class V2GridDock final : public QDockWidget {
public:
    explicit V2GridDock(QWidget* parent);

    [[nodiscard]] V2GridChoice Choice() const;
    //! いまの欄をそのまま当てる(試験と、開いたときの同期に使う)。
    void Apply();
    void SetChoice(const V2GridChoice& choice);
    //! 欄が変わったときに呼ぶもの。間隔の式が読めなければ呼ばず、理由を出す。
    void SetApplyHandler(std::function<void(const V2GridChoice&)> handler);
    //! 「原点を画面で指す」を押したときに呼ぶもの(grid.move_origin と同じ)。
    void SetPickOriginHandler(std::function<void()> handler);
    //! 色を選ばせる。差し替えると窓を出さない(試験用)。
    void SetColorChooser(std::function<QColor(const QColor& initial, const QString& title)> chooser);
    //! 主点間隔の欄に式を入れて当てる(試験用)。読めなければ false。
    bool ApplySpacingExpression(const QString& expression);
    [[nodiscard]] QString MessageText() const;

private:
    void Emit();
    void ChooseColor(QPushButton* button, QColor& color, const QString& title);
    static void PaintButton(QPushButton* button, const QColor& color);

    QCheckBox* visible_ = nullptr;
    QLineEdit* spacing_ = nullptr;
    QLabel* spacingValue_ = nullptr;
    QComboBox* subdivision_ = nullptr;
    QDoubleSpinBox* originU_ = nullptr;
    QDoubleSpinBox* originV_ = nullptr;
    QPushButton* resetOrigin_ = nullptr;
    QPushButton* pickOrigin_ = nullptr;
    QCheckBox* allModes_ = nullptr;
    QCheckBox* dimOffPlane_ = nullptr;
    QPushButton* majorColor_ = nullptr;
    QPushButton* minorColor_ = nullptr;
    QPushButton* backgroundColor_ = nullptr;
    QLabel* message_ = nullptr;
    QColor major_;
    QColor minor_;
    QColor background_;
    double spacingMm_ = 10.0;
    std::function<void(const V2GridChoice&)> applyHandler_;
    std::function<void()> pickOriginHandler_;
    std::function<QColor(const QColor&, const QString&)> colorChooser_;
    bool loading_ = false;
};
