//! 展開の「作り方」カード(HP-UF)。正本 fabrication mock、matrix F-11/F-12。
//!
//! 「曲げ・展開」節は 自動展開/基準辺指定/複数部材配置 の3枚のカードから選び、
//! 「展開」ボタンで走らせる。自動展開はそのまま fabrication.create_pattern、
//! 基準辺指定は fabrication.set_unfold_base の後に create_pattern を通す
//! (set_unfold_base が実際に読むのは「対象部材」欄の番号であって、3D の線選びではない。
//! V2BendRadiusCommands.cpp の SetUnfoldBaseRail を見て、無い道を見せないようにした)。
//! 複数部材配置と、展開先の紙以外・表裏の反転は核に道が無く、disabled のまま理由を出す。

#include "V2SelfTest.h"

#include "V2FabricationDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/domain/Entity.h"

#include <QString>

#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::SelectAllOfKind;
using kachakacha::v2::domain::EntityKind;

//! 矩形から面を1枚作り、既定の候補(帯 = B)で製作モデルを1つ確定するところまで。
//! V2SelfTestGenerate.cpp の ArmFabricationModel と同じ最小手順(別ファイルの
//! 匿名名前空間からは呼べないので、ここに持つ)。
[[nodiscard]] bool ArmFabricationModel(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))
        || !Explain("境界を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.RunCommand("surface.create");
    if (!Explain("Enterで面を確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("形状ガイドができる", CountOfKind(window, EntityKind::GuideSurface) == 1)) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Isometric);
    viewport.FitToDocument();
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});   // 選択を外して構える
    window.RunCommand("fabrication.create");
    if (!Explain("形状ガイドを画面から拾える", ClickOnAnyGuideSurface(window))
        || !Explain("Enterで近似を確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))) {
        return false;
    }
    return Explain("製作モデルが1つできる", window.FabricationModelCount() == 1);
}

//! 製作モデルを1つ選び、棚を「2 部材の編集・曲げ確認」(展開のカードがある段)へ切り替える。
[[nodiscard]] bool SelectFabricationModelAndShowUnfoldStage(V2MainWindow& window)
{
    window.Viewport().SetSelection(
        SelectAllOfKind(window.Session().GetDocument().Snapshot(), EntityKind::FabricationModel));
    window.FabricationDock().SetStageIndex(1);
    return Explain("展開のカードがある段へ切り替えられる",
        window.FabricationDock().StageIndex() == 1);
}

//! HP-UF-01。展開のカードは 自動展開/基準辺指定/複数部材配置 の3枚で、
//! 複数部材配置は理由付きで無効、展開先は紙だけである理由がいつも出ている。
//! 「自動展開」を選んで「展開」を押すと、既存の面→展開→型紙のテストと同じ
//! 確かめ方(StatusText に「原寸」が出る = create_pattern が通った)で型紙になる。
[[nodiscard]] bool CaseUnfoldCardsListMethodsAndAutoRunsPattern(V2MainWindow& window)
{
    if (!ArmFabricationModel(window)
        || !SelectFabricationModelAndShowUnfoldStage(window)) {
        return false;
    }
    auto& dock = window.FabricationDock();
    const std::vector<QString> labels = dock.UnfoldCardLabels();
    const std::vector<QString> expected{QStringLiteral("自動展開"), QStringLiteral("基準辺指定"),
        QStringLiteral("複数部材配置")};
    if (!Explain("展開のカードは 自動展開/基準辺指定/複数部材配置 の3枚", labels == expected)) {
        return false;
    }
    if (!Explain("複数部材配置は無効", !dock.UnfoldCardEnabled(QStringLiteral("複数部材配置")))
        || !Explain((std::string("無効の理由に「まだ」がある(実際は ")
                         + dock.UnfoldCardTip(QStringLiteral("複数部材配置")).toStdString() + ")")
                         .c_str(),
            dock.UnfoldCardTip(QStringLiteral("複数部材配置")).contains(QStringLiteral("まだ")))) {
        return false;
    }
    if (!Explain("展開先が紙だけである理由がいつも出ている", !dock.UnfoldTargetReasonJa().isEmpty())) {
        return false;
    }
    if (!Explain("「自動展開」が見えていて押せる",
            dock.ClickUnfoldCard(QStringLiteral("自動展開")))) {
        return false;
    }
    dock.PressUnfold();
    return Explain(
        (std::string("型紙になる(") + window.StatusText().toStdString() + ")").c_str(),
        window.StatusText().contains(QStringLiteral("原寸")));
}

} // namespace

std::vector<SelfTestCase> UnfoldCases()
{
    return {
        {"HP-UF-01 展開のカードは3枚で、自動展開は展開ボタンから型紙まで通る",
            CaseUnfoldCardsListMethodsAndAutoRunsPattern},
    };
}

} // namespace kachakacha::v2::selftest
