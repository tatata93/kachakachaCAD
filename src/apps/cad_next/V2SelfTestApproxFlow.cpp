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
#include <cmath>
#include <cstddef>
#include <cstdint>
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
    window.RunCommand("fabrication.create");   // 一度目は構えて下見
    window.RunCommand("fabrication.create");   // 二度目で確定
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
    window.RunCommand("fabrication.create");   // 一度目は構えて下見
    window.RunCommand("fabrication.create");   // 二度目で確定
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
    window.RunCommand("fabrication.create");   // 一度目は構えて下見
    window.RunCommand("fabrication.create");   // 二度目で確定
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
    window.RunCommand("fabrication.create");   // 一度目は構えて下見
    window.RunCommand("fabrication.create");   // 二度目で確定
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
    window.RunCommand("fabrication.create");   // 一度目は構えて下見
    window.RunCommand("fabrication.create");   // 二度目で確定
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
    // **見ているのは1つだけ: 当たっていないこと。**
    // 古い案が当たれば枚数が減る。減っていなければ、見せ直している。
    if (!Explain((std::string("値が変わったら古い案は当てない(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            static_cast<int>(window.FabricationPanelCount()) == before)) {
        return false;
    }
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Line);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    // 値を戻しておく。以降の枚数の見比べを、この寄り道で狂わせない。
    window.FabricationDock().SetPartNumbersText(QString());
    window.FabricationDock().SetAssemblyPercent(0.0);
    window.FabricationDock().PressApplyAssembly();
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

//! Codex Q1-Q5-R6 B1。読めない部材番号で、別の部材の半径を見せないこと。
//!
//! 読めない字は番号の並びとしては空になる。空欄と同じ道を通していたので、
//! `abc` と書いても部材1の半径が出ていた。見るのは **棚の実際の更新** である。
[[nodiscard]] bool CaseUnreadablePartNumbersShowNoRadius(V2MainWindow& window)
{
    if (!MakeSurfaceFromScratch(window)) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface));
    window.RunCommand("fabrication.create");   // 一度目は構えて下見
    window.RunCommand("fabrication.create");   // 二度目で確定
    if (!Explain("近似ができる", window.FabricationModelCount() == 1)) {
        return false;
    }
    auto& dock = window.FabricationDock();
    // まず空欄。1枚目の半径が出る。
    dock.SetPartNumbersText(QString());
    window.RefreshBendRadius();
    if (!Explain("空欄なら半径の欄は触れる", dock.RadiusUsable())) {
        return false;
    }
    // 読めない字を書く。**値ではなく理由が出る。**
    dock.SetPartNumbersText(QStringLiteral("abc"));
    window.RefreshBendRadius();
    if (!Explain((std::string("読めない番号では半径を出さない(欄は ")
                     + (dock.RadiusUsable() ? "触れる" : "触れない") + ")").c_str(),
            !dock.RadiusUsable())) {
        return false;
    }
    if (!Explain((std::string("読めない理由が出ている(")
                     + dock.RadiusStateTextJa().toStdString() + ")").c_str(),
            !dock.RadiusStateTextJa().isEmpty())) {
        return false;
    }
    // 読める番号へ戻すと、また出る。塞ぎっぱなしにしない。
    dock.SetPartNumbersText(QStringLiteral("1"));
    window.RefreshBendRadius();
    return Explain("読める番号へ戻せば、また半径が出る", dock.RadiusUsable());
}

//! 引継ぎ 2026-09-17 の 5。組立率と半径は同じことの言い換えで、どちらから打ってもよい。
//! 打っただけでは文書は変わらず、「当てる」「固定」で初めて入る。
[[nodiscard]] bool CasePercentAndRadiusSayTheSameThing(V2MainWindow& window)
{
    if (!MakeSurfaceFromScratch(window)) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface));
    window.RunCommand("fabrication.create");   // 一度目は構えて下見
    window.RunCommand("fabrication.create");   // 二度目で確定
    if (!Explain("近似ができる", window.FabricationModelCount() == 1)) {
        return false;
    }
    auto& dock = window.FabricationDock();
    dock.SetStageIndex(1);
    dock.SetPartNumbersText(QStringLiteral("1"));
    window.RefreshBendRadius();
    if (!Explain("半径の欄が出ている", dock.RadiusUsable())) {
        return false;
    }
    const double radiusAt100 = dock.RadiusMm();
    if (!Explain("100% の半径が正の数", radiusAt100 > 0.0)) {
        return false;
    }
    const std::uint64_t revision = window.Session().GetDocument().Revision();
    // 欄は半径 0.01 mm・組立率 0.1% で丸めて持つ。丸めた値の 2 倍と、2 倍を丸めた値は
    // 最後の桁で 1 だけ違うことがある(PC の実画面で 50.545 → 50.55、2 倍 101.09。
    // offscreen では線の位置が違い、たまたま割り切れていた)。許しは最後の桁 1 つ分。
    constexpr double kRadiusStepMm = 0.011;
    constexpr double kPercentStep = 0.11;
    dock.TypeAssemblyPercent(50.0);
    if (!Explain((std::string("50% と打つと半径が2倍になる(") + std::to_string(dock.RadiusMm())
                     + " mm)").c_str(),
            std::abs(dock.RadiusMm() - radiusAt100 * 2.0) < kRadiusStepMm)) {
        return false;
    }
    dock.TypeRadiusMm(radiusAt100 * 4.0);
    if (!Explain((std::string("半径を4倍と打つと組立率が 25% になる(")
                     + std::to_string(dock.AssemblyPercent()) + "%)").c_str(),
            std::abs(dock.AssemblyPercent() - 25.0) < kPercentStep)) {
        return false;
    }
    // 100% より小さい半径は、この板ではそれ以上曲げられない。組立率は動かさない。
    const double percentBefore = dock.AssemblyPercent();
    dock.TypeRadiusMm(radiusAt100 * 0.5);
    if (!Explain("曲げられない半径では組立率が動かない",
            std::abs(dock.AssemblyPercent() - percentBefore) < 1.0e-6)) {
        return false;
    }
    if (!Explain("打っただけでは文書は変わらない",
            window.Session().GetDocument().Revision() == revision)) {
        return false;
    }
    dock.TypeAssemblyPercent(50.0);
    dock.PressApplyAssembly();
    return Explain("「当てる」で初めて文書に入る",
        window.Session().GetDocument().Revision() != revision);
}

//! F-04 表示: 元の面と近似の姿を出し分ける。見るだけの切り替えで、文書は変わらない。
[[nodiscard]] bool CaseFabricationDisplayTogglesAreViewOnly(V2MainWindow& window)
{
    if (!MakeSurfaceFromScratch(window)) {
        return false;
    }
    auto& viewport = window.Viewport();
    const auto surfaces = kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface);
    const EntityId surface = surfaces.entityIds.front();
    viewport.SetSelection(surfaces);
    window.RunCommand("fabrication.create");   // 一度目は構えて下見
    window.RunCommand("fabrication.create");   // 二度目で確定
    window.SetMode(kachakacha::v2::app::UiMode::Fabrication);
    if (!Explain("近似ができる", window.FabricationModelCount() == 1)) {
        return false;
    }
    const auto drawn = [&viewport, &surface] {
        const auto& shapes = viewport.ShapeViews();
        return std::any_of(shapes.begin(), shapes.end(),
            [&surface](const auto& shape) { return shape.entityId == surface; });
    };
    const std::uint64_t revision = window.Session().GetDocument().Revision();
    if (!Explain("はじめは元の面も近似の姿も出ている", drawn() && viewport.FoldPreviewRailCount() > 0)) {
        return false;
    }
    window.FabricationDock().SetShowSource(false);
    if (!Explain("「元の面」を外すと元の面が消える", !drawn())) {
        return false;
    }
    window.FabricationDock().SetShowApprox(false);
    if (!Explain("「近似の姿」を外すと近似の姿が消える", viewport.FoldPreviewRailCount() == 0)) {
        return false;
    }
    window.FabricationDock().SetShowSource(true);
    window.FabricationDock().SetShowApprox(true);
    return Explain("戻すと両方出て、文書は一度も変わっていない",
        drawn() && viewport.FoldPreviewRailCount() > 0
            && window.Session().GetDocument().Revision() == revision);
}

//! 入力の数(製作): 挙げた部材を何枚にでも等分でき、隣り合う何枚でも 1 枚にできる。
//! どちらも 1 度目は見せるだけ・2 度目で当てる・1 回の元に戻すで戻る(既存の約束のまま)。
[[nodiscard]] bool CaseSplitIntoPiecesAndMergeRange(V2MainWindow& window)
{
    if (!MakeSurfaceFromScratch(window)) {
        return false;
    }
    auto& viewport = window.Viewport();
    const auto selectSurfaces = [&window, &viewport] {
        viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
            window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface));
    };
    selectSurfaces();
    if (window.FabricationMethodInUse()
        != kachakacha::v2::app::FabricationMethod::BandApproximation) {
        window.RunCommand("fabrication.set_method");
        selectSurfaces();
    }
    // 細い帯の基準を下げておく(ここで見るのは枚数の扱い。基準で断ることは別の試験が見る)。
    auto choice = window.FabricationDock().Choice();
    choice.minimumPartWidthMm = 0.5;
    window.FabricationDock().SetChoice(choice);
    window.RunCommand("fabrication.create");   // 一度目は構えて下見
    window.RunCommand("fabrication.create");   // 二度目で確定
    const int before = static_cast<int>(window.FabricationPanelCount());
    if (!Explain((std::string("部材が 2 枚以上ある(") + std::to_string(before) + " 枚)").c_str(),
            window.FabricationModelCount() == 1 && before >= 2)) {
        return false;
    }
    const auto panels = [&window] { return static_cast<int>(window.FabricationPanelCount()); };
    const auto twice = [&window](const char* command) {
        window.RunCommand(command);   // 1 度目: 見せる
        window.RunCommand(command);   // 2 度目: 当てる
    };
    window.FabricationDock().SetPartNumbersText(QStringLiteral("1"));
    window.FabricationDock().SetSplitPieces(3);
    twice("fabrication.split_part");
    if (!Explain((std::string("部材1 を 3 枚に等分できる(") + std::to_string(before) + " → "
                     + std::to_string(panels()) + "、帯は " + window.StatusText().toStdString()
                     + ")").c_str(),
            panels() == before + 2)) {
        return false;
    }
    window.FabricationDock().SetPartNumbersText(QStringLiteral("1, 2, 3"));
    twice("fabrication.merge_parts");
    if (!Explain((std::string("隣り合う 3 枚を 1 枚にできる(") + std::to_string(panels()) + " 枚)")
                .c_str(),
            panels() == before)) {
        return false;
    }
    window.FabricationDock().SetPartNumbersText(QStringLiteral("1, 3"));
    twice("fabrication.merge_parts");
    if (!Explain("離れた番号は 1 枚にせず、隣り合う番号を書くよう言う",
            panels() == before
                && window.StatusText().contains(QStringLiteral("隣り合う番号")))) {
        return false;
    }
    window.FabricationDock().SetPartNumbersText(QStringLiteral("1, 2"));
    window.FabricationDock().SetSplitPieces(2);
    twice("fabrication.split_part");
    if (!Explain((std::string("挙げた 2 枚をそれぞれ 2 枚に分ける(") + std::to_string(panels())
                     + " 枚)").c_str(),
            panels() == before + 2)) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1 回の元に戻すで分ける前へ戻る", panels() == before);
}

} // namespace

std::vector<SelfTestCase> ApproximationFlowCases()
{
    return {
        {"組立率と半径はどちらから打っても同じことを言い、当てるまで文書は変わらない",
            CasePercentAndRadiusSayTheSameThing},
        {"読めない部材番号では半径を出さず、理由を出す",
            CaseUnreadablePartNumbersShowNoRadius},
        {"何も無いところから面を作り、方式を見比べて近似できる",
            CaseApproximationFromScratch},
        {"近似を0/50/70/100%へ動かし、途中の状態から線と面を作れる",
            CaseApproximationBendsAndOutputs},
        {"近似は1回の取り消しで戻り、保存して開き直しても残る",
            CaseApproximationUndoAndReopen},
        {"部材は何枚にでも等分でき隣り合う何枚でも1枚にでき1回で戻る",
            CaseSplitIntoPiecesAndMergeRange},
        {"製作の表示は元の面と近似の姿を出し分け文書は変えない",
            CaseFabricationDisplayTogglesAreViewOnly},
        {"部材を1つにする・分けるが本当に分け方を変える",
            CaseMergeAndSplitChangeThePartition},
    };
}

} // namespace kachakacha::v2::selftest
