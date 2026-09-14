//! 近似の一連の作業を、本番の指示だけで通す(Codex Q1-Q5 B4)。
//!
//! これまでの Q4 は、近似済みの見本を開いて「近似モデルが1つ以上ある」と
//! 数えるだけだった。それでは受入試験にならない。**出来上がりの持ち物を
//! 数えるのではなく、人が押す順に押して、通るかを見る。**
//!
//! 通す順(オーナー指示の目標操作列):
//!   何も無い文書 → 作業平面 → 線 → 形状ガイドの面
//!   → 近似の方式を見比べる → 近似を作る → 曲げる → いまの状態から線と面を作る
//!
//! ここで落ちたら、それは「機能はあるが使えない」である。

#include "V2SelfTest.h"

#include "V2FabricationDock.h"

#include "kachakacha/app/FabricationEvaluate.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/modeling/ToolController.h"
#include "kachakacha/domain/Feature.h"

#include <QString>

#include <algorithm>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::CreateFabricationModelDefinition;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::domain::Feature;
using kachakacha::v2::domain::Visibility;

[[nodiscard]] int CountKind(V2MainWindow& window, EntityKind kind)
{
    return CountOfKind(window, kind);
}

[[nodiscard]] int CountVisible(V2MainWindow& window, EntityKind kind)
{
    int count = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kind && entity.visibility == Visibility::Visible) {
            ++count;
        }
    }
    return count;
}

//! いまの文書にある近似の作り方。無ければ空。
[[nodiscard]] const Feature* FabricationFeature(V2MainWindow& window)
{
    const auto& snapshot = window.Session().GetDocument().Snapshot();
    for (const auto& feature : snapshot.features) {
        if (std::get_if<CreateFabricationModelDefinition>(&feature.definition) != nullptr) {
            return &feature;
        }
    }
    return nullptr;
}

//! 何も無いところから、曲がった面を1枚作る。近似する相手になる。
[[nodiscard]] bool MakeSurfaceFromScratch(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("何も無いところから始める", CountKind(window, EntityKind::GuideSurface) == 0)) {
        return false;
    }
    if (!Explain("近似モデルは1つも無い", window.FabricationModelCount() == 0)) {
        return false;
    }
    if (!MakeCurvedGuideSurface(window)) {
        return false;
    }
    return Explain("本番の指示だけで曲がった面ができる",
        CountKind(window, EntityKind::GuideSurface) >= 1);
}

//! Q4。何も無いところから面を作り、近似の方式を見比べて採用する。
[[nodiscard]] bool CaseApproximationFromScratch(V2MainWindow& window)
{
    if (!MakeSurfaceFromScratch(window)) {
        return false;
    }
    auto& viewport = window.Viewport();
    const auto surfaces = kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface);
    const EntityId surface = surfaces.entityIds.front();

    // 方式を切り替えて、両方の結果を見比べる。人はここで「どちらで作るか」を決める。
    viewport.SetSelection(surfaces);
    window.RunCommand("fabrication.create");
    const std::string firstWay = window.StatusText().toStdString();
    if (!Explain((std::string("1つ目の方式で近似できる(") + firstWay + ")").c_str(),
            window.FabricationModelCount() == 1)) {
        return false;
    }
    const int firstPanels = static_cast<int>(window.FabricationPanelCount());

    window.RunCommand("edit.undo");
    if (!Explain("見比べるためにいったん戻せる", window.FabricationModelCount() == 0)) {
        return false;
    }
    window.RunCommand("fabrication.set_method");
    viewport.SetSelection(surfaces);
    window.RunCommand("fabrication.create");
    const std::string secondWay = window.StatusText().toStdString();
    if (!Explain((std::string("2つ目の方式でも近似できる(") + secondWay + ")").c_str(),
            window.FabricationModelCount() == 1)) {
        return false;
    }
    const int secondPanels = static_cast<int>(window.FabricationPanelCount());
    if (!Explain((std::string("方式で結果が変わる(部材 ") + std::to_string(firstPanels)
                     + " 枚 → " + std::to_string(secondPanels) + " 枚)").c_str(),
            firstPanels != secondPanels || firstWay != secondWay)) {
        return false;
    }

    // 採用した近似が、元の面を入力として覚えていること。
    // 覚えていないと、面を直しても近似が計算し直されない。
    const Feature* feature = FabricationFeature(window);
    if (!Explain("近似の作り方が文書にある", feature != nullptr)) {
        return false;
    }
    const bool linked = std::find(feature->inputEntityIds.begin(),
                            feature->inputEntityIds.end(), surface)
        != feature->inputEntityIds.end();
    return Explain("近似が元の面を入力として覚えている", linked);
}

//! Q4。作った近似を 0 / 50 / 70 / 100% へ動かし、途中の状態から線と面を作る。
[[nodiscard]] bool CaseApproximationBendsAndOutputs(V2MainWindow& window)
{
    if (!MakeSurfaceFromScratch(window)) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface));
    window.RunCommand("fabrication.create");
    if (!Explain("近似ができる", window.FabricationModelCount() == 1)) {
        return false;
    }
    const int panelsBefore = static_cast<int>(window.FabricationPanelCount());

    for (const double percent : {0.0, 50.0, 70.0, 100.0}) {
        window.SetAssemblyChooser([percent](double) {
            return std::optional<double>(percent);
        });
        window.RunCommand("fabrication.set_assembly");
        const std::string want = std::to_string(static_cast<int>(percent)) + "%";
        if (!Explain((std::string("組立 ") + want + " にできる(帯は "
                         + window.StatusText().toStdString() + ")").c_str(),
                window.StatusText().contains(QString::fromStdString(want)))) {
            return false;
        }
    }

    // 70% へ戻して、その状態から線と面を作る。
    window.SetAssemblyChooser([](double) { return std::optional<double>(70.0); });
    window.RunCommand("fabrication.set_assembly");
    // 作るものを「両方」にする(線のみ → 部品のみ → 両方)。
    window.RunCommand("fabrication.freeze_output");
    window.RunCommand("fabrication.freeze_output");
    const int wiresBefore = CountVisible(window, EntityKind::Wire);
    const int surfacesBefore = CountVisible(window, EntityKind::GuideSurface);
    window.RunCommand("fabrication.freeze_state");
    if (!Explain((std::string("70% の状態から作れる(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("現在状態を固定(")))) {
        return false;
    }
    if (!Explain("線が増える", CountVisible(window, EntityKind::Wire) > wiresBefore)) {
        return false;
    }
    if (!Explain("面が増える",
            CountVisible(window, EntityKind::GuideSurface) > surfacesBefore)) {
        return false;
    }
    // §34。元の近似は壊さない。作ったものは別の物として増える。
    if (!Explain("元の近似モデルは残る", window.FabricationModelCount() == 1)) {
        return false;
    }
    return Explain("元の部材の数も変わらない",
        static_cast<int>(window.FabricationPanelCount()) == panelsBefore);
}

//! Q4。近似を作るのは1回の取り消しで戻り、保存して開き直しても残る。
[[nodiscard]] bool CaseApproximationUndoAndReopen(V2MainWindow& window)
{
    if (!MakeSurfaceFromScratch(window)) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface));
    window.RunCommand("fabrication.create");
    if (!Explain("近似ができる", window.FabricationModelCount() == 1)) {
        return false;
    }
    const int panels = static_cast<int>(window.FabricationPanelCount());

    window.RunCommand("edit.undo");
    if (!Explain("1回の取り消しで近似が消える", window.FabricationModelCount() == 0)) {
        return false;
    }
    window.RunCommand("edit.redo");
    if (!Explain("1回のやり直しで近似が戻る", window.FabricationModelCount() == 1)) {
        return false;
    }
    if (!Explain("部材の数も戻る",
            static_cast<int>(window.FabricationPanelCount()) == panels)) {
        return false;
    }

    if (!Explain("保存して開き直せる",
            window.SaveAndReopen(QStringLiteral("kacha_selftest_approx_flow.kcd2")))) {
        return false;
    }
    if (!Explain("開き直しても近似が残る", window.FabricationModelCount() == 1)) {
        return false;
    }
    return Explain((std::string("開き直しても部材の数が同じ(") + std::to_string(panels)
                       + " → " + std::to_string(window.FabricationPanelCount()) + ")")
                       .c_str(),
        static_cast<int>(window.FabricationPanelCount()) == panels);
}

//! §32。部材を1つにする・分けるが、**本当に分け方を変える**。
//! 見せるだけでなく、枚数が変わり、取り消しで戻り、開き直しても残ること。
[[nodiscard]] bool CaseMergeAndSplitChangeThePartition(V2MainWindow& window)
{
    if (!MakeSurfaceFromScratch(window)) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface));
    // 帯近似(V1 方式)でないと、境目のパラメータで分け方を持てない。
    if (window.FabricationMethodInUse()
        != kachakacha::v2::app::FabricationMethod::BandApproximation) {
        window.RunCommand("fabrication.set_method");
        viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
            window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface));
    }
    window.RunCommand("fabrication.create");
    if (!Explain("近似ができる", window.FabricationModelCount() == 1)) {
        return false;
    }
    const int before = static_cast<int>(window.FabricationPanelCount());
    if (!Explain((std::string("部材が2枚以上ある(") + std::to_string(before)
                     + " 枚)").c_str(),
            before >= 2)) {
        return false;
    }

    // 番号の扱いを先に見る。**丸めない。** 無い番号なら何も変わらない。
    window.FabricationDock().SetPartNumbersText(QStringLiteral("999"));
    window.RunCommand("fabrication.merge_parts");
    window.RunCommand("fabrication.merge_parts");
    if (!Explain((std::string("無い番号は断る(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            static_cast<int>(window.FabricationPanelCount()) == before)) {
        return false;
    }
    window.FabricationDock().SetPartNumbersText(QStringLiteral("999"));
    window.RunCommand("fabrication.split_part");
    window.RunCommand("fabrication.split_part");
    if (!Explain("無い番号では分けない",
            static_cast<int>(window.FabricationPanelCount()) == before)) {
        return false;
    }

    // 1枚目と2枚目を1つにする。
    // **1度目は見せるだけ。** 押した瞬間に変わってはいけない。
    window.FabricationDock().SetPartNumbersText(QStringLiteral("1"));
    window.RunCommand("fabrication.merge_parts");
    if (!Explain((std::string("1度目は見せるだけで変わらない(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            static_cast<int>(window.FabricationPanelCount()) == before
                && window.PendingPartitionShown())) {
        return false;
    }
    // やめれば何も起きない。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    if (!Explain("道具を替えると案が消える",
            !window.PendingPartitionShown()
                && static_cast<int>(window.FabricationPanelCount()) == before)) {
        return false;
    }
    // 案を見せたあとで**値を変えたら、その案は当てない。**
    // 番号と指示名しか見ていなかったので、見せていない形が確定し得た
    // (Codex Q1-Q5-R4 B1)。
    window.RunCommand("fabrication.merge_parts");   // 1度目: 見せる
    if (!Explain("案が出ている", window.PendingPartitionShown())) {
        return false;
    }
    window.FabricationDock().SetPartNumbersText(QString());
    window.FabricationDock().SetAssemblyPercent(40.0);
    window.FabricationDock().PressApplyAssembly();
    window.FabricationDock().SetPartNumbersText(QStringLiteral("1"));
    window.RunCommand("fabrication.merge_parts");   // 2度目だが値が変わっている
    if (!Explain((std::string("値が変わったら当てずに出し直す(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            static_cast<int>(window.FabricationPanelCount()) == before
                && window.PendingPartitionShown())) {
        return false;
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);

    // もう一度出して、2度目で決める。
    window.FabricationDock().SetPartNumbersText(QStringLiteral("1"));
    window.RunCommand("fabrication.merge_parts");
    window.RunCommand("fabrication.merge_parts");
    const int merged = static_cast<int>(window.FabricationPanelCount());
    if (!Explain((std::string("1つにすると枚数が減る(") + std::to_string(before)
                     + " → " + std::to_string(merged) + " 枚。帯は "
                     + window.StatusText().toStdString() + ")").c_str(),
            merged == before - 1)) {
        return false;
    }

    // 取り消すと戻る。分け方は文書のものなので、履歴に乗っている。
    window.RunCommand("edit.undo");
    if (!Explain("取り消すと枚数が戻る",
            static_cast<int>(window.FabricationPanelCount()) == before)) {
        return false;
    }
    window.RunCommand("edit.redo");
    if (!Explain("やり直すと枚数がまた減る",
            static_cast<int>(window.FabricationPanelCount()) == merged)) {
        return false;
    }

    // 1枚目を2つに分ける。枚数が1増える。
    window.FabricationDock().SetPartNumbersText(QStringLiteral("1"));
    window.RunCommand("fabrication.split_part");
    window.RunCommand("fabrication.split_part");
    const int split = static_cast<int>(window.FabricationPanelCount());
    if (!Explain((std::string("分けると枚数が増える(") + std::to_string(merged) + " → "
                     + std::to_string(split) + " 枚。帯は "
                     + window.StatusText().toStdString() + ")").c_str(),
            split == merged + 1)) {
        return false;
    }

    // 保存して開き直しても、決めた分け方が残る。
    // 「決めたのに開き直すと戻る」を作らない。
    if (!Explain("保存して開き直せる",
            window.SaveAndReopen(QStringLiteral("kacha_selftest_partition.kcd2")))) {
        return false;
    }
    return Explain((std::string("開き直しても決めた分け方が残る(")
                       + std::to_string(window.FabricationPanelCount()) + " 枚)").c_str(),
        static_cast<int>(window.FabricationPanelCount()) == split);
}

} // namespace

std::vector<SelfTestCase> ApproximationFlowCases()
{
    return {
        {"何も無いところから面を作り、方式を見比べて近似できる",
            CaseApproximationFromScratch},
        {"近似を0/50/70/100%へ動かし、途中の状態から線と面を作れる",
            CaseApproximationBendsAndOutputs},
        {"近似は1回の取り消しで戻り、保存して開き直しても残る",
            CaseApproximationUndoAndReopen},
        {"部材を1つにする・分けるが本当に分け方を変える",
            CaseMergeAndSplitChangeThePartition},
    };
}

} // namespace kachakacha::v2::selftest
