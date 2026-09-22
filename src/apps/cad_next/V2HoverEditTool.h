#pragma once

//! 線の上に置いて押す編集(トリム・延長・分割)。Inventor のスケッチと同じ手順
//! (docs/v2/ui-redesign/INVENTOR_PROCEDURE.md)。
//!
//! 帯の トリム / 延長 / 分割 を押すと道具が構わる(何も選んでいなくてよい)。線の上にポインタを
//! 置くと、**消える区間 / 伸びる先 / 分かれる点** が下見に出る(赤系の太い破線・× 印)。押すと
//! その場で文書が変わり、道具は構えたまま(続けて押せる)。Esc で選択道具へ戻る。
//! 境目は画面の全部の線。何が起きるかは core(app/HoverEditPlan)が決め、ここは
//! 置いた線を渡して下見を描き、押されたら chains を新しいワイヤーとして文書へ入れるだけ。
//!
//! 延長・分割は、線を選んでから押した昔の道(相手を選ぶ)も残す。トリムはこの道だけ。

#include "kachakacha/app/HoverEditPlan.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>

#include <optional>

class V2MainWindow;

class V2HoverEditTool final {
public:
    explicit V2HoverEditTool(V2MainWindow& window);

    //! この道具か(トリム・延長・分割)。
    [[nodiscard]] static bool Handles(kachakacha::v2::modeling::DrawingTool tool) noexcept;
    [[nodiscard]] bool Active() const;
    //! 置いた先が変わった。下見を出し直す。
    void RefreshPreview();
    //! 押された。引き受けたら真(押しはそこで終わる)。
    [[nodiscard]] bool Click(const QPointF& position);
    //! 道具を離した / 文書が変わった。下見を片づける。
    void Clear();
    //! いまの下見(試験から見る)。
    [[nodiscard]] const std::optional<kachakacha::v2::app::HoverEditOutcome>& Outcome() const noexcept
    {
        return outcome_;
    }
    [[nodiscard]] QString LabelJa() const;

private:
    [[nodiscard]] std::optional<kachakacha::v2::app::HoverEditPick> PickAt(
        const std::optional<QPointF>& position) const;
    [[nodiscard]] kachakacha::v2::base::Result<kachakacha::v2::app::HoverEditOutcome> Plan(
        const kachakacha::v2::app::HoverEditPick& pick) const;
    void Apply(const kachakacha::v2::app::HoverEditOutcome& outcome);

    V2MainWindow& window_;
    std::optional<kachakacha::v2::app::HoverEditOutcome> outcome_;
    QString refusalJa_;
};
