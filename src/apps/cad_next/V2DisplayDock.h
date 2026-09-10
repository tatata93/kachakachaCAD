#pragma once

//! 表示設定の右パネル(V1 の「表示」タブ)。形は変わらない。
//!
//! 線の色・太さ・様式、補助線の色・太さ・様式、背景色、そして段
//! (設計 / グリッド無し / 完成形 / 選択だけ)。段は core の DisplayStage、太さと様式は
//! core の DisplaySettings に入れる。色は画面の持ち物(ViewportPalette)。

#include "kachakacha/app/DisplaySettings.h"

#include <QColor>
#include <QDockWidget>
#include <QString>

#include <array>
#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

//! 棚で決めること。
struct V2DisplayChoice {
    kachakacha::v2::app::DisplaySettings settings;
    QColor wireColor;
    QColor constructionColor;
    QColor backgroundColor;
};

class V2DisplayDock final : public QDockWidget {
public:
    explicit V2DisplayDock(QWidget* parent);

    [[nodiscard]] V2DisplayChoice Choice() const;
    //! いまの欄をそのまま当てる(試験用)。
    void Apply();
    //! 欄へ入れる。段のボタンの押されている印も合わせる。
    void SetChoice(const V2DisplayChoice& choice, kachakacha::v2::app::DisplayStage stage);
    //! 太さ・様式だけを入れる(段を当てたときに呼ぶ)。色は変えない。
    void SetSettings(const kachakacha::v2::app::DisplaySettings& settings,
        kachakacha::v2::app::DisplayStage stage);
    //! 欄が変わったときに呼ぶもの。
    void SetApplyHandler(std::function<void(const V2DisplayChoice&)> handler);
    //! 段のボタンを押したときに呼ぶもの(view.stage_* と同じ道)。
    void SetStageHandler(std::function<void(kachakacha::v2::app::DisplayStage)> handler);
    //! 色を選ばせる。差し替えると窓を出さない(試験用)。
    void SetColorChooser(std::function<QColor(const QColor& initial, const QString& title)> chooser);
    //! 段のボタンを押したのと同じ(試験用)。
    void PressStage(kachakacha::v2::app::DisplayStage stage);

private:
    void Emit();
    void ChooseColor(QPushButton* button, QColor& color, const QString& title);
    static void PaintButton(QPushButton* button, const QColor& color);
    [[nodiscard]] static kachakacha::v2::app::LineStyle StyleAt(int index) noexcept;
    [[nodiscard]] static int IndexOf(kachakacha::v2::app::LineStyle style) noexcept;

    std::array<QPushButton*, 4> stageButtons_{};
    QPushButton* wireColor_ = nullptr;
    QDoubleSpinBox* wireWidth_ = nullptr;
    QComboBox* wireStyle_ = nullptr;
    QPushButton* constructionColor_ = nullptr;
    QDoubleSpinBox* constructionWidth_ = nullptr;
    QComboBox* constructionStyle_ = nullptr;
    QPushButton* backgroundColor_ = nullptr;
    QLabel* note_ = nullptr;
    QColor wire_;
    QColor construction_;
    QColor background_;
    kachakacha::v2::app::DisplaySettings settings_;
    std::function<void(const V2DisplayChoice&)> applyHandler_;
    std::function<void(kachakacha::v2::app::DisplayStage)> stageHandler_;
    std::function<QColor(const QColor&, const QString&)> colorChooser_;
    bool loading_ = false;
};
