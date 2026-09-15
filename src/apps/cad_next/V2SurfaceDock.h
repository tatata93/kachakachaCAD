#pragma once

//! 「面を作る」の棚(UI の正本 `kachakachaCAD_extrude_surface_UI_final_mock.html`)。
//!
//! 正本のとおり、4つの段を1枚に置く。
//!   1. 作り方 … 主要6方式をカードで常時見せる。残りは「その他」へ
//!   2. 入力   … 断面 / ガイド / 境界。使わない役割は「この方式では不要」と出す
//!   3. 断面順 … 自動 / 手動固定と、いまの生成順を番号つきで
//!   4. 状態   … 入力数・不足・生成可否・下見の様子
//! 下に キャンセル / 入力をやり直す / 確定。
//!
//! これまでは「形状ガイドの役割」という6列の表と10個のボタンが並ぶだけで、
//! **いまの作り方がどこにも出ていなかった。**しかも後ろの札にあった。

#include "kachakacha/app/SurfaceInputState.h"

#include <QDockWidget>
#include <QString>

#include <functional>
#include <vector>

class QLabel;
class QPushButton;
class QComboBox;
class QTreeWidget;
class QVBoxLayout;

class V2SurfaceDock final : public QDockWidget {
public:
    explicit V2SurfaceDock(QWidget* parent);

    //! いまの入力を映す。作り方・役割・順序・状態を一度に書き直す。
    void ShowInput(const kachakacha::v2::app::SurfaceInputState& state,
        const std::vector<QString>& sectionNamesJa, bool previewShown);

    //! 作り方を選んだ。
    void SetMethodHandler(
        std::function<void(kachakacha::v2::modeling::GuideSurfaceMethod)> handler);
    //! その役割へ「追加」を押した。
    void SetAddHandler(std::function<void(kachakacha::v2::modeling::ChainRole)> handler);
    //! 断面順の決め方を変えた。
    void SetOrderingHandler(
        std::function<void(kachakacha::v2::app::SurfaceOrdering)> handler);
    //! 断面を1つ上/下へ動かした。
    void SetMoveSectionHandler(std::function<void(int from, int to)> handler);
    //! 下の3つのボタン。
    void SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel,
        std::function<void()> reset);

    //! 試験から押す。窓を出さずに同じ道を通す。
    void PressMethod(kachakacha::v2::modeling::GuideSurfaceMethod method);
    void PressAdd(kachakacha::v2::modeling::ChainRole role);
    void PressOrdering(kachakacha::v2::app::SurfaceOrdering ordering);
    void PressConfirm();
    void PressCancel();
    //! いま画面に出ている断面の並び(番号順)。試験から読む。
    [[nodiscard]] std::vector<QString> SectionOrderTexts() const;
    //! いま前に出ている作り方。
    [[nodiscard]] kachakacha::v2::modeling::GuideSurfaceMethod MethodShown() const noexcept
    {
        return shown_.method;
    }
    //! その役割の欄に出ている言葉。「この方式では不要」などを試験から読む。
    [[nodiscard]] QString SlotTextJa(kachakacha::v2::modeling::ChainRole role) const;
    //! 「確定」を押せるか。
    [[nodiscard]] bool CanConfirm() const;

private:
    void BuildMethodCards(QVBoxLayout* layout);
    void BuildSlotRows(QVBoxLayout* layout);
    void BuildOrderRows(QVBoxLayout* layout);

    kachakacha::v2::app::SurfaceInputState shown_;
    QLabel* state_ = nullptr;
    std::vector<QPushButton*> methodCards_;
    QComboBox* otherMethods_ = nullptr;
    QLabel* sectionValue_ = nullptr;
    QLabel* guideValue_ = nullptr;
    QLabel* boundaryValue_ = nullptr;
    QPushButton* addSection_ = nullptr;
    QPushButton* addGuide_ = nullptr;
    QPushButton* addBoundary_ = nullptr;
    QPushButton* orderAuto_ = nullptr;
    QPushButton* orderManual_ = nullptr;
    QTreeWidget* orderList_ = nullptr;
    QPushButton* moveUp_ = nullptr;
    QPushButton* moveDown_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* confirm_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* reset_ = nullptr;
    bool loading_ = false;

    std::function<void(kachakacha::v2::modeling::GuideSurfaceMethod)> methodHandler_;
    std::function<void(kachakacha::v2::modeling::ChainRole)> addHandler_;
    std::function<void(kachakacha::v2::app::SurfaceOrdering)> orderingHandler_;
    std::function<void(int, int)> moveSectionHandler_;
    std::function<void()> confirmHandler_;
    std::function<void()> cancelHandler_;
    std::function<void()> resetHandler_;
};
