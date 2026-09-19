//! 生成の「作り方」カード(HP-GN)。正本 fabrication mock、matrix F-13/F-14。
//!
//! 「生成」節は 現在状態 → Flat 0% → Target 100% の3枚のカードで、
//! それぞれ fabrication.freeze_state / freeze_flat / freeze_target を通す。
//! 押すたびに文書が変わり、1回の取り消しで戻ること(§34 の考え方どおり)。
//! HP-GN-03 は帯の「輪郭 Wire」(fabrication.freeze_wires)。固定で作るもの
//! (Wire/Part/両方)の設定に関わらず線だけを作ることを見る。

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
//! ArmApproxOnFreshSurface(V2SelfTestHumanPathApprox.cpp)と同じ最小手順を、
//! 別ファイルの匿名名前空間から呼べないのでここに持つ。
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

//! 製作モデルを1つ選び、棚を「2 部材の編集・曲げ確認」(生成の3枚がある段)へ切り替える。
[[nodiscard]] bool SelectFabricationModelAndShowGenerateStage(V2MainWindow& window)
{
    window.Viewport().SetSelection(
        SelectAllOfKind(window.Session().GetDocument().Snapshot(), EntityKind::FabricationModel));
    window.FabricationDock().SetStageIndex(1);
    return Explain("曲げ確認の段(生成のカードがある)へ切り替えられる",
        window.FabricationDock().StageIndex() == 1);
}

//! HP-GN-01。生成のカードは 現在状態/Flat 0%/Target 100% の3枚で、Target 100% は
//! 固定で作るものの設定どおりに固定物を作り、近似モデルは残り、1回の取り消しで戻る。
[[nodiscard]] bool CaseGenerateCardsListStatesAndTargetFreezes(V2MainWindow& window)
{
    if (!ArmFabricationModel(window)
        || !SelectFabricationModelAndShowGenerateStage(window)) {
        return false;
    }
    auto& dock = window.FabricationDock();
    const std::vector<QString> labels = dock.GenerateCardLabels();
    const std::vector<QString> expected{
        QStringLiteral("現在状態"), QStringLiteral("Flat 0%"), QStringLiteral("Target 100%")};
    if (!Explain("生成のカードは 現在状態/Flat 0%/Target 100% の3枚", labels == expected)) {
        return false;
    }
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    const int partsBefore = CountOfKind(window, EntityKind::Part);
    const int surfacesBefore = CountOfKind(window, EntityKind::GuideSurface);
    if (!Explain("「Target 100%」のカードが見えていて押せる",
            dock.ClickGenerateCard(QStringLiteral("Target 100%")))) {
        return false;
    }
    const bool grew = CountOfKind(window, EntityKind::Wire) > wiresBefore
        || CountOfKind(window, EntityKind::Part) > partsBefore
        || CountOfKind(window, EntityKind::GuideSurface) > surfacesBefore;
    if (!Explain((std::string("固定物が増える(帯は ") + window.StatusText().toStdString() + ")")
                     .c_str(),
            grew)
        || !Explain("目標100%を固定したという帯が出る",
            window.StatusText().contains(QStringLiteral("目標形状(100%)を固定")))
        || !Explain("近似モデルは1つのまま残る", window.FabricationModelCount() == 1)) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1回の取り消しで固定物が消える(線/面/部品とも元どおり)",
        CountOfKind(window, EntityKind::Wire) == wiresBefore
            && CountOfKind(window, EntityKind::Part) == partsBefore
            && CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore);
}

//! HP-GN-02。「Flat 0%」のカードは線だけを作り、近似モデルは壊さずに残す。
[[nodiscard]] bool CaseFlatCardMakesWiresAndKeepsModel(V2MainWindow& window)
{
    if (!ArmFabricationModel(window)
        || !SelectFabricationModelAndShowGenerateStage(window)) {
        return false;
    }
    auto& dock = window.FabricationDock();
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    if (!Explain("「Flat 0%」のカードが見えていて押せる",
            dock.ClickGenerateCard(QStringLiteral("Flat 0%")))) {
        return false;
    }
    return Explain("線が増える", CountOfKind(window, EntityKind::Wire) > wiresBefore)
        && Explain("近似モデルは残る(壊さない)", window.FabricationModelCount() == 1);
}

//! HP-GN-03。「輪郭 Wire」(fabrication.freeze_wires)は、固定で作るものが
//! 「部品のみ」でも線だけを作る(F-14。以前は fabrication.freeze_state と
//! 同じ中身の張りぼてボタンだった)。
[[nodiscard]] bool CaseContourWiresIgnoreFreezeOutputSetting(V2MainWindow& window)
{
    using kachakacha::v2::fabrication::FreezeOutput;

    if (!ArmFabricationModel(window)
        || !SelectFabricationModelAndShowGenerateStage(window)) {
        return false;
    }
    // 固定で作るものを「部品のみ」にする(既定の「線のみ」から1回切り替え)。
    window.RunCommand("fabrication.freeze_output");
    if (!Explain("固定で作るものが部品のみになる",
            window.FreezeOutputInUse() == FreezeOutput::PartsOnly)) {
        return false;
    }
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    const int partsBefore = CountOfKind(window, EntityKind::Part);
    window.RunCommand("fabrication.freeze_wires");
    if (!Explain("線が増える", CountOfKind(window, EntityKind::Wire) > wiresBefore)
        || !Explain("部品は増えない(固定で作るものが部品のみでも線だけを作る)",
            CountOfKind(window, EntityKind::Part) == partsBefore)
        || !Explain("固定で作るものの設定は書き換わらずに残る",
            window.FreezeOutputInUse() == FreezeOutput::PartsOnly)) {
        return false;
    }
    return Explain("近似モデルは残る(壊さない)", window.FabricationModelCount() == 1);
}

} // namespace

std::vector<SelfTestCase> GenerateCases()
{
    return {
        {"HP-GN-01 生成の作り方カードは 現在/Flat/Target で、Target 100% は固定物を作る",
            CaseGenerateCardsListStatesAndTargetFreezes},
        {"HP-GN-02 Flat 0% は線を作り、近似モデルは残る", CaseFlatCardMakesWiresAndKeepsModel},
        {"HP-GN-03 輪郭 Wire は固定で作るものが部品でも線だけを作る",
            CaseContourWiresIgnoreFreezeOutputSetting},
    };
}

} // namespace kachakacha::v2::selftest
