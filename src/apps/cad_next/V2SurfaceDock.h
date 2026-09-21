#pragma once

//! 「面を作る」の棚(UI の正本 `kachakachaCAD_extrude_surface_UI_final_mock.html`)。
//!
//! 正本のとおり、4つの段を1枚に置く。
//!   1. 作り方 … 主要6方式をカードで常時見せる。残りは「その他」へ
//!   2. 入力   … 断面 / ガイド / 中心線 / 境界。欄ごとに **可変長の一覧**(1 本ずつ外す・
//!               向き反転)。使わない役割は「この方式では不要」と出す
//!   3. 断面順 … 自動 / 手動固定と、いまの生成順を番号つきで
//!   4. 状態   … 入力数・不足・生成可否・下見の様子
//! 下に キャンセル / 入力をやり直す / 確定。
//!
//! これまでは「形状ガイドの役割」という6列の表と10個のボタンが並ぶだけで、
//! **いまの作り方がどこにも出ていなかった。**しかも後ろの札にあった。

#include "kachakacha/app/SurfaceInputState.h"

#include <QDockWidget>
#include <QString>

#include <array>
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

    //! 欄ごとの名前。3D で押した順(断面は生成順)。
    struct SlotNames {
        std::vector<QString> sections;
        std::vector<QString> guides;
        std::vector<QString> centerlines;
        std::vector<QString> boundaries;
        //! 境界の辺ごとの連続条件(0=G0,1=G1,2=G2)と支持面の名前。boundaries と同じ並び。
        std::vector<int> boundaryContinuity;
        std::vector<QString> boundarySupports;
    };

    //! いまの入力を映す。作り方・役割・順序・状態を一度に書き直す。
    //! `solverNoteJa` は検査が決めた作り方の内訳(「断面とガイドを全部通るように張る」)。
    void ShowInput(const kachakacha::v2::app::SurfaceInputState& state,
        const SlotNames& names, bool previewShown,
        const QString& deviationNoteJa = QString(), const QString& solverNoteJa = QString());

    //! 作り方を選んだ。
    void SetMethodHandler(
        std::function<void(kachakacha::v2::modeling::GuideSurfaceMethod)> handler);
    //! その欄の「ここへ選ぶ」を押した(以後の 3D クリックはその欄へ入る)。
    void SetActivateHandler(
        std::function<void(kachakacha::v2::modeling::ChainRole)> handler);
    //! その欄の「解除」を押した(欄を空にする)。
    void SetClearHandler(std::function<void(kachakacha::v2::modeling::ChainRole)> handler);
    //! 断面順の決め方を変えた。
    void SetOrderingHandler(
        std::function<void(kachakacha::v2::app::SurfaceOrdering)> handler);
    //! 断面を1つ上/下へ動かした。
    void SetMoveSectionHandler(std::function<void(int from, int to)> handler);
    //! 一覧の 1 行を「外す」(その欄の何番目か)。
    void SetRemoveEntryHandler(
        std::function<void(kachakacha::v2::modeling::ChainRole, int row)> handler);
    //! 一覧の 1 行を「向き反転」。
    void SetFlipEntryHandler(
        std::function<void(kachakacha::v2::modeling::ChainRole, int row)> handler);
    //! 境界の辺(一覧の row 行)の連続条件を変えた。
    void SetContinuityHandler(
        std::function<void(int row, kachakacha::v2::modeling::SurfaceContinuity)> handler);
    //! 境界の辺(一覧の row 行)の「支持面を選ぶ」を押した。
    void SetPickSupportHandler(std::function<void(int row)> handler);
    //! 四辺面の張り方を変えた。
    void SetFourEdgeStyleHandler(
        std::function<void(kachakacha::v2::modeling::FourEdgeStyle)> handler);
    //! 下の3つのボタン。
    void SetActionHandlers(std::function<void()> confirm, std::function<void()> cancel,
        std::function<void()> reset);

    //! **見えているカードを実際に押す。**人の道の試験はこちらを使う。
    //! 見えていなければ偽を返す。不可視の widget を叩いて通したことにしない。
    [[nodiscard]] bool ClickMethodCard(kachakacha::v2::modeling::GuideSurfaceMethod method);

    //! **見えている「ここへ選ぶ」「解除」を実際に押す。**人の道の試験はこちら。
    //! 見えていなければ偽。不可視の widget を叩いて通したことにしない。
    [[nodiscard]] bool ClickActivate(kachakacha::v2::modeling::ChainRole slot);
    [[nodiscard]] bool ClickClear(kachakacha::v2::modeling::ChainRole slot);
    //! その欄の一覧の row 行を選んでから、見えている「外す」「向き反転」を押す。
    [[nodiscard]] bool ClickRemoveEntry(kachakacha::v2::modeling::ChainRole slot, int row);
    [[nodiscard]] bool ClickFlipEntry(kachakacha::v2::modeling::ChainRole slot, int row);
    //! 境界の一覧の row 行を選んで、見えている連続条件の欄を選ぶ / 「支持面を選ぶ」を押す。
    [[nodiscard]] bool ChooseContinuity(int row, kachakacha::v2::modeling::SurfaceContinuity order);
    [[nodiscard]] bool ClickPickSupport(int row);
    //! いま連続条件の欄に出ている言葉(「支持面: 屋根」「この作り方では指定できません」など)。
    [[nodiscard]] QString ContinuityTextJa() const;
    //! いま「4. 状態」に出ている言葉(外れ・製作性の目安を含む)。
    [[nodiscard]] QString StatusTextJa() const;
    //! 見えている「張り方」を選ぶ(四辺面のときだけ見える)。
    [[nodiscard]] bool ChooseFourEdgeStyle(kachakacha::v2::modeling::FourEdgeStyle style);
    //! その欄の一覧に出ている行(「1  Section_A」など)。
    [[nodiscard]] std::vector<QString> SlotEntryTexts(
        kachakacha::v2::modeling::ChainRole slot) const;
    //! 見えている「自動 / 手動固定」を押す。
    [[nodiscard]] bool ClickOrdering(kachakacha::v2::app::SurfaceOrdering ordering);
    //! 断面順の行を選んでから、見えている「↑」「↓」を押す。押せなければ偽。
    [[nodiscard]] bool ClickMoveRow(int row, bool up);
    //! いま「ここへ選ぶ」が押された形で出ている欄。無ければ Section。
    [[nodiscard]] kachakacha::v2::modeling::ChainRole ActiveSlotShown() const;

    //! 試験から押す。窓を出さずに同じ道を通す。
    void PressMethod(kachakacha::v2::modeling::GuideSurfaceMethod method);
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
    void RefreshSlotTitles(kachakacha::v2::modeling::GuideSurfaceMethod method);

    //! 欄 1 つぶん。欄の並びは SurfaceSlotKey(0..kSurfaceSlotCount-1)と同じ。
    struct SlotRow {
        kachakacha::v2::modeling::ChainRole key = kachakacha::v2::modeling::ChainRole::Section;
        QLabel* title = nullptr;
        QLabel* value = nullptr;
        QTreeWidget* list = nullptr;
        QPushButton* arm = nullptr;
        QPushButton* clear = nullptr;
        QPushButton* remove = nullptr;
        QPushButton* flip = nullptr;
    };
    std::array<SlotRow, kachakacha::v2::app::kSurfaceSlotCount> slots_{};
    void BuildSlotRow(QVBoxLayout* layout, int index);
    void FillSlotRow(SlotRow& row, const kachakacha::v2::app::SurfaceInputState& state,
        const std::vector<QString>& names, bool previewShown);
    [[nodiscard]] const SlotRow* RowFor(kachakacha::v2::modeling::ChainRole slot) const;
    //! 境界の辺ごとの連続条件(境界面・四辺面)。境界の一覧で選んだ行に効く。
    QLabel* continuityTitle_ = nullptr;
    QComboBox* continuity_ = nullptr;
    QPushButton* pickSupport_ = nullptr;
    QLabel* supportName_ = nullptr;
    SlotNames shownNames_;
    void RefreshContinuityRow();
    QLabel* fourEdgeStyleTitle_ = nullptr;
    QComboBox* fourEdgeStyle_ = nullptr;
    QLabel* solverNote_ = nullptr;
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
    std::function<void(kachakacha::v2::modeling::ChainRole)> activateHandler_;
    std::function<void(kachakacha::v2::modeling::ChainRole)> clearHandler_;
    std::function<void(kachakacha::v2::app::SurfaceOrdering)> orderingHandler_;
    std::function<void(int, int)> moveSectionHandler_;
    std::function<void(kachakacha::v2::modeling::ChainRole, int)> removeEntryHandler_;
    std::function<void(kachakacha::v2::modeling::ChainRole, int)> flipEntryHandler_;
    std::function<void(kachakacha::v2::modeling::FourEdgeStyle)> fourEdgeStyleHandler_;
    std::function<void(int, kachakacha::v2::modeling::SurfaceContinuity)> continuityHandler_;
    std::function<void(int)> pickSupportHandler_;
    std::function<void()> confirmHandler_;
    std::function<void()> cancelHandler_;
    std::function<void()> resetHandler_;
};
