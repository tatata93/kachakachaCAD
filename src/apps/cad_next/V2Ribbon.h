#pragma once

//! 2段の帯(上段 = カテゴリ、下段 = 道具)。正本は 3 HTML(2026-09-18)。
//!
//! 何をどこに並べるかは core(`app/Ribbon.h`)が決める。ここは並べて押すだけ。
//! 道具のボタンは台帳の QAction を既定の動作に持つ(押した/道具を持っている が同じ印で見える)。
//! 作り方つきの道具(面作成の平面/ロフト…、測定の距離/角度…)と、核に無くて押せない道具は
//! 帯が自分の QAction を持つ。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。

#include "kachakacha/app/Ribbon.h"
#include "kachakacha/app/UiMode.h"

#include <QString>
#include <QWidget>

#include <functional>
#include <map>
#include <vector>

class QAction;
class QHBoxLayout;
class QToolButton;

class V2Ribbon final : public QWidget {
public:
    explicit V2Ribbon(QWidget* parent);

    //! 台帳の命令 id → QAction(押せるもの)。無い id は nullptr を返してよい。
    void SetActionLookup(std::function<QAction*(std::string_view)> lookup);
    //! 作り方つきの道具を押した(作り方 = surfaceMethod / measureMode の道具)。
    void SetVariantHandler(std::function<void(const kachakacha::v2::app::RibbonTool&)> handler);
    //! 押せない道具を押した(理由を状態行へ出すため)。
    void SetBlockedHandler(std::function<void(const kachakacha::v2::app::RibbonTool&)> handler);

    //! モードが変わった。そのモードのカテゴリを並べ直し、前に見ていたカテゴリを出す。
    void ShowMode(kachakacha::v2::app::UiMode mode);
    //! そのカテゴリを前に出す(見出しを押したのと同じ)。
    void ShowCategory(int index);
    //! その命令を含むカテゴリを前に出す(近道やメニューから道具を持ったとき)。
    void RevealCommand(std::string_view commandId);
    //! いまの道具の状態を印へ映す(作り方つきの道具の押された形)。
    void SetCurrentSurfaceMethod(int method, bool active);
    void SetCurrentMeasureMode(int mode, bool active);

    // ---- 試験と場面づくりから ----
    [[nodiscard]] int CategoryCount() const;
    [[nodiscard]] int CurrentCategory() const noexcept { return current_; }
    [[nodiscard]] QString CategoryLabel(int index) const;
    //! **見えているボタンを実際に押す。**見えていなければ偽。
    [[nodiscard]] bool ClickCategory(const QString& labelJa);
    [[nodiscard]] bool ClickTool(const QString& labelJa);
    //! いま下段に出ている道具の言葉(順に)。
    [[nodiscard]] std::vector<QString> ToolLabels() const;
    //! その道具が下段に見えていて押せるか。
    [[nodiscard]] bool ToolEnabled(const QString& labelJa) const;
    //! その道具のツールチップ(押せないときは理由)。
    [[nodiscard]] QString ToolTip(const QString& labelJa) const;
    //! その命令がいまのモードの帯のどこかにあり、押せる形か。
    [[nodiscard]] bool CommandAvailable(std::string_view commandId) const;
    //! 下段のボタンが1つも右端で切れていないか(幅の検査)。
    [[nodiscard]] bool ToolsFitInWidth() const;

private:
    void RebuildTools();
    [[nodiscard]] QToolButton* ToolButton(const QString& labelJa) const;

    kachakacha::v2::app::UiMode mode_ = kachakacha::v2::app::UiMode::Drawing;
    int current_ = 0;
    std::map<kachakacha::v2::app::UiMode, int> remembered_;
    QWidget* categoryRow_ = nullptr;
    QHBoxLayout* categoryLayout_ = nullptr;
    QWidget* toolRow_ = nullptr;
    QHBoxLayout* toolLayout_ = nullptr;
    std::vector<QToolButton*> categoryButtons_;
    struct ToolEntry {
        QToolButton* button = nullptr;
        QAction* ownAction = nullptr;   //!< 作り方つき / 押せない道具の自前の QAction
        kachakacha::v2::app::RibbonTool tool;
    };
    std::vector<ToolEntry> toolButtons_;
    int currentSurfaceMethod_ = -1;
    int currentMeasureMode_ = -1;
    std::function<QAction*(std::string_view)> lookup_;
    std::function<void(const kachakacha::v2::app::RibbonTool&)> variantHandler_;
    std::function<void(const kachakacha::v2::app::RibbonTool&)> blockedHandler_;
};
