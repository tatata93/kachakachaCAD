#pragma once

//! 「面の解析」の棚(プロンプト surface_analysis)。
//!
//!   1. 見るもの … なし / ゼブラ / 平均曲率 / ガウス曲率(可展性)/ U/V 線 / 曲率コーム /
//!                 境目の連続 / 入力線からのずれ
//!   2. 対象     … 選んだ面(無ければ全部の面)と、面を作る・面の編集の下見
//!   3. 読み方   … 色の読み方と、製作性の目安(数値の基準つき。断定しない)
//! 下に 閉じる(解析の表示は残る)。
//!
//! どの面を解析し、どう色を塗るかは道具(V2SurfaceAnalysisTool)と core が決める。

#include "kachakacha/app/SurfaceAnalysis.h"

#include <QDockWidget>
#include <QString>

#include <array>
#include <functional>
#include <vector>

class QLabel;
class QPushButton;
class QVBoxLayout;

class V2SurfaceAnalysisDock final : public QDockWidget {
public:
    explicit V2SurfaceAnalysisDock(QWidget* parent);

    void ShowState(kachakacha::v2::app::SurfaceAnalysisMode mode, const QString& targetJa,
        const std::vector<QString>& legendJa);
    void SetModeHandler(std::function<void(kachakacha::v2::app::SurfaceAnalysisMode)> handler);
    void SetCloseHandler(std::function<void()> handler);

    //! 見えているボタンを実際に押す(人の道の試験)。
    [[nodiscard]] bool ClickMode(kachakacha::v2::app::SurfaceAnalysisMode mode);
    [[nodiscard]] bool ClickClose();
    [[nodiscard]] QString LegendTextJa() const;
    [[nodiscard]] QString TargetTextJa() const;

private:
    std::array<QPushButton*, 8> modes_{};
    QLabel* target_ = nullptr;
    QLabel* legend_ = nullptr;
    QPushButton* close_ = nullptr;
    bool loading_ = false;
    std::function<void(kachakacha::v2::app::SurfaceAnalysisMode)> modeHandler_;
    std::function<void()> closeHandler_;
};
