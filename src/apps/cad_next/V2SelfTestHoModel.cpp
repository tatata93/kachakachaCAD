//! HO 流線形前頭部を使った総合試験(オーナー指示 2026-09-14 §36〜42)。
//!
//! ここで見るのは単体機能の有無ではない。
//! **「実際に鉄道模型を作ろうとすると使えない」を見つけること** である(§39)。
//! したがって、見本を開いて終わりにせず、
//! 正対 → 面 → 押し出し → 板材近似 → 曲げ → 出力 まで通す。
//!
//! AUTOMOC を使っていないので Q_OBJECT は付けない。

#include "V2SelfTest.h"

#include "V2EntityTree.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/GroupTree.h"
#include "kachakacha/app/RailwayNoseHoSample.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/fabrication/BendRadius.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"

#include <QString>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QPointF>

#include <cmath>
#include <cstddef>
#include <variant>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;

//! 見本を開く。開けなければ空。
[[nodiscard]] bool OpenHoSample(V2MainWindow& window)
{
    return window.ApplyManualState(QStringLiteral("railway-nose-ho"));
}

//! 名前で物を引く。無ければ Nil。
[[nodiscard]] EntityId ByName(V2MainWindow& window, const char* name)
{
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.displayName == name) {
            return entity.id;
        }
    }
    return EntityId{};
}

[[nodiscard]] int CountKind(V2MainWindow& window, EntityKind kind)
{
    int count = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kind) {
            ++count;
        }
    }
    return count;
}

//! 近似モデルが無いときに、何が作れなかったのかを言う。
//! 「近似モデルが見本に入っている」とだけ書いて落ちると、原因が残らない。
[[nodiscard]] std::string WhyNoModel(V2MainWindow& window)
{
    std::string text = "近似モデルが見本に入っている";
    if (window.FabricationModelCount() >= 1) {
        return text;
    }
    const std::string problems = window.RebuildProblems().toStdString();
    text += problems.empty() ? "(作り直せなかったものは無い。元の面が空か)"
                             : "(作り直せなかったもの: " + problems + ")";
    text += " 帯は " + window.StatusText().toStdString();
    return text;
}

} // namespace

//! TM-01/04〜09。見本が開けて、中身とまとまりがそろっている。
[[nodiscard]] bool CaseHoSampleOpens(V2MainWindow& window)
{
    if (!Explain("HO の見本を開ける", OpenHoSample(window))) {
        return false;
    }
    const auto& snapshot = window.Session().GetDocument().Snapshot();
    const std::size_t planes = kachakacha::v2::app::HoNoseSectionStations().size();
    if (!Explain((std::string("作業平面がそろう(実際 ")
                     + std::to_string(CountKind(window, EntityKind::WorkPlane)) + ")")
                     .c_str(),
            CountKind(window, EntityKind::WorkPlane) >= static_cast<int>(planes))) {
        return false;
    }
    for (const double station : kachakacha::v2::app::HoNoseSectionStations()) {
        const std::string wire = kachakacha::v2::app::HoNoseSectionName(station);
        if (!Explain((wire + " がある").c_str(), !ByName(window, wire.c_str()).IsNil())) {
            return false;
        }
    }
    // 見本が「開けた」と言えるのは、面が本当に出来たときだけである。
    // 物が並んでいるだけで通すと、面が作れないことに気づかないまま
    // ほかの試験が総崩れになる(2026-09-14 に実際そうなった)。
    if (!Explain((std::string("面が本当に作れる(作り直せなかったもの: ")
                     + window.RebuildProblems().toStdString() + ")").c_str(),
            window.RebuildProblems().isEmpty())) {
        return false;
    }
    if (!Explain("面がある", !ByName(window, "NoseSurface").IsNil())) {
        return false;
    }
    // まとまりの形。左の一覧に入れ子で出ていること。
    bool nested = false;
    for (const auto& group : snapshot.groups) {
        if (kachakacha::v2::app::GroupPathJa(snapshot, group.id)
            == "RailwayNose_HO/Sections") {
            nested = true;
        }
    }
    if (!Explain("まとまりが入れ子で出る", nested)) {
        return false;
    }
    return Explain("左の一覧にまとまりの行がある", !window.GroupItems().empty());
}

//! UI-TM-01/03。見本の作業平面と面へ正対する。中央と大きさも合う。
[[nodiscard]] bool CaseHoFacingWorkPlanesAndSurface(V2MainWindow& window)
{
    if (!Explain("HO の見本を開ける", OpenHoSample(window))) {
        return false;
    }
    auto& viewport = window.Viewport();
    for (const char* name : {"WP_X000", "WP_X010"}) {
        const EntityId plane = ByName(window, name);
        if (!Explain((std::string(name) + " がある").c_str(), !plane.IsNil())) {
            return false;
        }
        viewport.SetViewCenter(kachakacha::v2::geometry::Vector3{900.0, 900.0, 900.0});
        viewport.SetVisibleWidthMm(4000.0);
        kachakacha::v2::app::SelectionSet one;
        one.entityIds.push_back(plane);
        viewport.SetSelection(one);
        window.RunCommand("view.align_selection");
        const auto center = viewport.ViewCenter();
        if (!Explain((std::string(name) + ": 画面の真ん中へ来る").c_str(),
                std::abs(center.x) < 200.0 && std::abs(center.y) < 200.0
                    && std::abs(center.z) < 200.0)) {
            return false;
        }
        if (!Explain((std::string(name) + ": 大きさも合う").c_str(),
                viewport.VisibleWidthMm() < 500.0)) {
            return false;
        }
        if (!Explain((std::string(name) + ": 選んだままにする").c_str(),
                kachakacha::v2::app::IsSelected(viewport.Selection(), plane))) {
            return false;
        }
    }
    // 曲がった面(NoseSurface)へも正対できる。
    const EntityId surface = ByName(window, "NoseSurface");
    if (!Explain("面がある", !surface.IsNil())) {
        return false;
    }
    viewport.SetViewCenter(kachakacha::v2::geometry::Vector3{900.0, 900.0, 900.0});
    viewport.SetVisibleWidthMm(4000.0);
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(surface);
    viewport.SetSelection(one);
    window.RunCommand("view.align_selection");
    if (!Explain((std::string("曲がった面でも断らない(作り直せなかったもの: "
                     + window.RebuildProblems().toStdString() + " / 帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            !window.StatusText().contains(QStringLiteral("選んでください")))) {
        return false;
    }
    const auto center = viewport.ViewCenter();
    return Explain("曲がった面も画面の真ん中へ来る",
        std::abs(center.x) < 200.0 && std::abs(center.y) < 200.0
            && std::abs(center.z) < 200.0);
}

//! UI-TM-06。見本の閉じた輪郭を押し出して立体にする(FloorProfile)。
[[nodiscard]] bool CaseHoExtrudeFromSampleProfile(V2MainWindow& window)
{
    if (!Explain("HO の見本を開ける", OpenHoSample(window))) {
        return false;
    }
    const EntityId profile = ByName(window, "WindowProfile");
    if (!Explain("切削用の窓の輪郭がある", !profile.IsNil())) {
        return false;
    }
    const int before = CountKind(window, EntityKind::Part);
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(profile);
    window.Viewport().SetSelection(one);
    window.RunCommand("part.extrude");   // 下見
    if (!Explain((std::string("下見が出る(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            window.Viewport().ExtrudeHandleShown())) {
        return false;
    }
    window.ExtrudeDock().TypeDistanceMm(1.0);
    window.RunCommand("part.extrude");   // 確定
    return Explain((std::string("立体が増える(帯は ")
                       + window.StatusText().toStdString() + ")(" + std::to_string(before)
                       + " → "
                       + std::to_string(CountKind(window, EntityKind::Part)) + ")")
                       .c_str(),
        CountKind(window, EntityKind::Part) > before);
}

//! UI-TM-09/12。見本の面を板材近似し、曲げ具合を動かす。
[[nodiscard]] bool CaseHoApproximateAndBend(V2MainWindow& window)
{
    if (!Explain("HO の見本を開ける", OpenHoSample(window))) {
        return false;
    }
    if (!Explain(WhyNoModel(window).c_str(), window.FabricationModelCount() >= 1)) {
        return false;
    }
    // 0 → 25 → 50 → 75 → 100 と動かす。どれも通ること。
    for (const double percent : {0.0, 25.0, 50.0, 75.0, 100.0}) {
        window.SetAssemblyChooser([percent](double) {
            return std::optional<double>(percent);
        });
        window.RunCommand("fabrication.set_assembly");
        const std::string want = std::to_string(static_cast<int>(percent)) + "%";
        if (!Explain((std::string("組立 ") + want + " にできる(帯は "
                         + window.StatusText().toStdString() + ")")
                         .c_str(),
                window.StatusText().contains(QString::fromStdString(want)))) {
            return false;
        }
    }
    return Explain("曲げ具合を一通り動かせる", true);
}

//! UI-TM-15/16/17。0% / 中間 / 100% から、普通の線と面を作る。
[[nodiscard]] bool CaseHoOutputsFromBendStates(V2MainWindow& window)
{
    if (!Explain("HO の見本を開ける", OpenHoSample(window))) {
        return false;
    }
    const auto countVisible = [&window](EntityKind kind) {
        int count = 0;
        for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
            if (entity.kind == kind
                && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
                ++count;
            }
        }
        return count;
    };
    // 作るものを「両方」にする(線のみ → 部品のみ → 両方)。
    window.RunCommand("fabrication.freeze_output");
    window.RunCommand("fabrication.freeze_output");
    for (const double percent : {0.0, 50.0, 100.0}) {
        window.SetAssemblyChooser([percent](double) {
            return std::optional<double>(percent);
        });
        window.RunCommand("fabrication.set_assembly");
        const int wiresBefore = countVisible(EntityKind::Wire);
        const int surfacesBefore = countVisible(EntityKind::GuideSurface);
        window.RunCommand("fabrication.freeze_state");
        const std::string at = std::to_string(static_cast<int>(percent)) + "%";
        if (!Explain((at + ": 固定できる(帯は " + window.StatusText().toStdString()
                         + ")").c_str(),
                window.StatusText().contains(QStringLiteral("現在状態を固定(")))) {
            return false;
        }
        if (!Explain((at + ": 線が増える").c_str(),
                countVisible(EntityKind::Wire) > wiresBefore)) {
            return false;
        }
        if (!Explain((at + ": 面が増える").c_str(),
                countVisible(EntityKind::GuideSurface) > surfacesBefore)) {
            return false;
        }
    }
    // 元の近似モデルは消えない(§34)。
    return Explain("元の近似モデルは残る", window.FabricationModelCount() >= 1);
}

//! UI-TM-13/14。実寸の半径を入れて固定でき、作り直しても戻らない。
[[nodiscard]] bool CaseHoRadiusAutoAndLock(V2MainWindow& window)
{
    if (!Explain("HO の見本を開ける", OpenHoSample(window))) {
        return false;
    }
    if (!Explain(WhyNoModel(window).c_str(), window.FabricationModelCount() >= 1)) {
        return false;
    }
    // まず 100% にして、自動の半径が出ることを見る。
    window.SetAssemblyChooser([](double) { return std::optional<double>(100.0); });
    window.RunCommand("fabrication.set_assembly");
    // 半径は部材1枚ずつ決める。相手の番号を書く。丸めないので、書かないと断られる。
    window.FabricationDock().SetPartNumbersText(QStringLiteral("1"));
    window.RefreshBendRadius();
    const auto measured = window.BendRadiusNow();
    if (!Explain((std::string("自動の半径が出る(R = ")
                     + std::to_string(measured.radiusMm) + "mm)").c_str(),
            measured.radiusMm > 0.0
                && measured.lock == kachakacha::v2::fabrication::ValueLock::Auto)) {
        return false;
    }
    // 手元の丸棒の径へ合わせる。22.00mm で固定。
    window.ApplyBendRadius(22.0, true);
    if (!Explain((std::string("固定できる(帯は ")
                     + window.StatusText().toStdString() + ")").c_str(),
            window.BendRadiusNow().lock
                == kachakacha::v2::fabrication::ValueLock::Locked)) {
        return false;
    }
    const auto locked = kachakacha::v2::fabrication::RadiusAtPercent(
        window.BendRadiusNow(), 100.0);
    if (!Explain("入れた値になる",
            locked.has_value() && std::abs(*locked - 22.0) < 1.0e-6)) {
        return false;
    }
    // 近似を測り直しても戻らない。
    window.RefreshBendRadius();
    const auto after = kachakacha::v2::fabrication::RadiusAtPercent(
        window.BendRadiusNow(), 100.0);
    if (!Explain("作り直しても自動値へ戻らない",
            after.has_value() && std::abs(*after - 22.0) < 1.0e-6)) {
        return false;
    }
    // 固定を外すと自動へ戻る。
    window.ApplyBendRadius(0.0, false);
    return Explain("固定を外すと自動へ戻る",
        window.BendRadiusNow().lock == kachakacha::v2::fabrication::ValueLock::Auto);
}

//! Q1-Q5 B2。部材ごとに違う半径を固定でき、保存して開き直しても残り、
//! **形の座標が実際に変わる**。表示だけ変わって形が同じ、を通さない。
[[nodiscard]] bool CaseHoRadiusIsPerPartAndPersisted(V2MainWindow& window)
{
    if (!Explain("HO の見本を開ける", OpenHoSample(window))) {
        return false;
    }
    if (!Explain(WhyNoModel(window).c_str(), window.FabricationModelCount() >= 1)) {
        return false;
    }
    window.SetAssemblyChooser([](double) { return std::optional<double>(100.0); });
    window.RunCommand("fabrication.set_assembly");

    const auto measured = window.BendRadiiNow();
    if (!Explain((std::string("部材ごとの半径が出る(") + std::to_string(measured.size())
                     + " 枚)").c_str(),
            measured.size() >= 2)) {
        return false;
    }
    const auto shapeOf = [&window]() {
        return window.FabricationShapeSignature();
    };
    const auto before = shapeOf();
    if (!Explain("形の指紋が取れる", !before.empty())) {
        return false;
    }

    // 1枚目と2枚目に違う半径を入れる。棚の「曲げる部材」で相手を選ぶ。
    window.FabricationDock().SetPartNumbersText(QStringLiteral("1"));
    window.ApplyBendRadius(22.0, true);
    window.FabricationDock().SetPartNumbersText(QStringLiteral("2"));
    window.ApplyBendRadius(9.0, true);

    window.FabricationDock().SetPartNumbersText(QStringLiteral("1"));
    const auto first = window.BendRadiusNow();
    window.FabricationDock().SetPartNumbersText(QStringLiteral("2"));
    const auto second = window.BendRadiusNow();
    if (!Explain((std::string("部材ごとに違う値を持てる(1枚目 ")
                     + std::to_string(first.radiusMm) + " / 2枚目 "
                     + std::to_string(second.radiusMm) + ")").c_str(),
            std::abs(first.radiusMm - second.radiusMm) > 1.0)) {
        return false;
    }
    if (!Explain("どちらも固定になる",
            first.lock == kachakacha::v2::fabrication::ValueLock::Locked
                && second.lock == kachakacha::v2::fabrication::ValueLock::Locked)) {
        return false;
    }

    // 形が本当に変わること。ここを見ないと「表示だけ」を見逃す。
    const auto after = shapeOf();
    if (!Explain("固定すると形の座標が変わる", !after.empty() && after != before)) {
        return false;
    }

    // 1回の取り消しで、直前に入れた 2枚目ぶんだけが戻る。
    window.RunCommand("edit.undo");
    window.FabricationDock().SetPartNumbersText(QStringLiteral("2"));
    if (!Explain("取り消すと2枚目が自動へ戻る",
            window.BendRadiusNow().lock
                == kachakacha::v2::fabrication::ValueLock::Auto)) {
        return false;
    }
    window.RunCommand("edit.redo");
    window.FabricationDock().SetPartNumbersText(QStringLiteral("2"));
    if (!Explain("やり直すと2枚目が戻る",
            window.BendRadiusNow().lock
                == kachakacha::v2::fabrication::ValueLock::Locked)) {
        return false;
    }

    // 保存して開き直しても残ること。画面が覚えているだけなら、ここで消える。
    if (!Explain("保存して開き直せる",
            window.SaveAndReopen(QStringLiteral("kacha_selftest_ho_radius.kcd2")))) {
        return false;
    }
    window.FabricationDock().SetPartNumbersText(QStringLiteral("1"));
    const auto firstBack = window.BendRadiusNow();
    window.FabricationDock().SetPartNumbersText(QStringLiteral("2"));
    const auto secondBack = window.BendRadiusNow();
    if (!Explain((std::string("開き直しても1枚目の半径が残る(")
                     + std::to_string(firstBack.radiusMm) + ")").c_str(),
            firstBack.lock == kachakacha::v2::fabrication::ValueLock::Locked
                && std::abs(firstBack.radiusMm - first.radiusMm) < 1.0e-6)) {
        return false;
    }
    if (!Explain("開き直しても2枚目の半径が残る",
            secondBack.lock == kachakacha::v2::fabrication::ValueLock::Locked
                && std::abs(secondBack.radiusMm - second.radiusMm) < 1.0e-6)) {
        return false;
    }
    return Explain("開き直しても形が同じ", shapeOf() == after);
}

//! UI-TM-19。見本を保存して開き直しても、まとまりと中身が残る。
[[nodiscard]] bool CaseHoSampleSurvivesSaveAndOpen(V2MainWindow& window)
{
    if (!Explain("HO の見本を開ける", OpenHoSample(window))) {
        return false;
    }
    const std::size_t entitiesBefore =
        window.Session().GetDocument().Snapshot().entities.size();
    const std::size_t groupsBefore =
        window.Session().GetDocument().Snapshot().groups.size();
    if (!Explain("保存して開き直せる", window.SaveAndReopen("kacha_selftest_ho.kcd2"))) {
        return false;
    }
    const auto& after = window.Session().GetDocument().Snapshot();
    if (!Explain((std::string("物の数が残る(") + std::to_string(entitiesBefore) + " → "
                     + std::to_string(after.entities.size()) + ")").c_str(),
            after.entities.size() == entitiesBefore)) {
        return false;
    }
    return Explain((std::string("まとまりの数が残る(") + std::to_string(groupsBefore)
                       + " → " + std::to_string(after.groups.size()) + ")").c_str(),
        after.groups.size() == groupsBefore);
}

//! §42。見本を開いた状態でも、道具を替えたときに前の道具の画面が残らない。
[[nodiscard]] bool CaseHoToolSwitchStaysClean(V2MainWindow& window)
{
    using kachakacha::v2::modeling::DrawingTool;
    if (!Explain("HO の見本を開ける", OpenHoSample(window))) {
        return false;
    }
    constexpr DrawingTool kChain[] = {DrawingTool::Line, DrawingTool::Arc,
        DrawingTool::Bezier, DrawingTool::Spline, DrawingTool::Select};
    auto& viewport = window.Viewport();
    for (const DrawingTool tool : kChain) {
        window.SelectTool(tool);
        const std::string name(kachakacha::v2::modeling::DrawingToolNameJa(tool));
        const auto mismatches
            = kachakacha::v2::app::DiagnosticMismatches(window.DiagnosticSnapshotNow());
        if (!mismatches.empty()) {
            (void)Explain((name + ": " + mismatches.front()).c_str(), false);
            window.SelectTool(DrawingTool::Select);
            return false;
        }
    }
    window.SelectTool(DrawingTool::Select);
    return Explain("選択へ戻ったら途中経過も残らない", !viewport.HasPreview());
}

[[nodiscard]] bool CaseHiddenExtrudeProfile(V2MainWindow& window)
{
    if (!CaseHoExtrudeFromSampleProfile(window)) return false;
    const auto before = window.Viewport().ShapeViews().size();
    kachakacha::v2::app::SelectionSet selection;
    selection.entityIds.push_back(ByName(window, "WindowProfile"));
    window.Viewport().SetSelection(selection);
    window.RunCommand("view.hide_selected");
    if (!window.SaveAndReopen("kacha_hidden_extrude.kcd2")) return false;
    return Explain("非表示の輪郭でも押し出しを再生成できる",
        window.RebuildProblems().isEmpty() && window.Viewport().ShapeViews().size() == before);
}

void HeavyOrbitTiming(V2Viewport& viewport, const char* label)
{
    const auto before = viewport.Orientation();
    QElapsedTimer timer;
    timer.start();
    QImage frame(viewport.size(),QImage::Format_ARGB32);
    std::vector<double> frames;
    const int count=qEnvironmentVariableIsSet("KACHACAD_PERFORMANCE_ONLY")?120:12;
    for (int i = 0; i < count; ++i) {
        QElapsedTimer frameTimer; frameTimer.start();
        viewport.OrbitByPixels(12,3);
        viewport.render(&frame);
        frames.push_back(double(frameTimer.nsecsElapsed())/1e6);
    }
    std::sort(frames.begin(),frames.end());
    Note((std::string(label) + " frames=" + std::to_string(count)+" ms=" + std::to_string(timer.elapsed())
        + " p95="+std::to_string(frames[frames.size()*95/100])+" max="+std::to_string(frames.back())
        + " backend="+viewport.RenderBackend()).c_str());
    viewport.SetOrientation(before);
}

//! 明示した外部モデルだけを重負荷検証する。通常の自己診断には追加しない。
[[nodiscard]] bool HeavyModelShots(V2MainWindow& window, const QString& out)
{
    auto& viewport = window.Viewport();
    const auto save = [&](const QString& name) {
        QImage image(viewport.size(), QImage::Format_ARGB32);
        image.fill(Qt::white);
        viewport.render(&image);
        return image.save(out + "/" + name + ".png");
    };
    for (const auto direction : {ViewDirection::Isometric, ViewDirection::Front, ViewDirection::Left, ViewDirection::Top, ViewDirection::Bottom}) {
        viewport.SetViewDirection(direction);
        viewport.FitToDocument();
        if (!save("view-" + QString::number(static_cast<int>(direction)))) return false;
    }
    viewport.SetOrientation(view::OrientationForZone({-1,-1,1}).Value());
    viewport.FitToDocument();
    if (!save("front-quarter")) return false;
    viewport.SetViewCenter({14,0,26});
    viewport.SetVisibleWidthMm(75);
    if (!save("front-detail")) return false;
    const auto& snapshot = window.Session().GetDocument().Snapshot();
    for (const auto& group : snapshot.groups) {
        if (group.displayName != "前台車" && group.displayName != "客室・運転室") continue;
        app::SelectionSet selected;
        for (const auto& id : app::EntitiesUnderGroup(snapshot,group.id)) {
            const auto* entity = window.Session().GetDocument().FindEntity(id);
            if (entity && entity->kind == EntityKind::Part) selected.entityIds.push_back(id);
        }
        viewport.SetSelection(selected);
        window.RunCommand("view.isolate");
        viewport.SetSelection({});
        viewport.FitToDocument();
        if (!save(group.displayName == "前台車" ? "bogie" : "interior")) return false;
        if (group.displayName == "前台車") HeavyOrbitTiming(viewport,"bogie orbit/render");
        window.RunCommand("view.restore_isolation");
    }
    viewport.FitToDocument();
    HeavyOrbitTiming(viewport,"heavy orbit/render");
    viewport.SetViewDirection(ViewDirection::Isometric);
    return true;
}

[[nodiscard]] bool HeavyRoofManufacturing(V2MainWindow& window, const QString& out)
{
    const auto source = ByName(window, "屋根 製作用参照面");
    if (source.IsNil()) return false;
    app::SelectionSet selection;
    selection.entityIds.push_back(source);
    window.Viewport().SetSelection(selection);
    auto choice = window.FabricationDock().Choice();
    choice.equalPartCount = 4;
    choice.minimumPartWidthMm = 4;
    choice.maximumPartCount = 12;
    choice.adaptiveSpacing = true;
    window.FabricationDock().SetChoice(choice);
    window.RunCommand("fabrication.create");
    const auto outcomes = window.ApproxOutcomes();
    for (const auto& outcome : outcomes) {
        Note(("roof candidate: available=" + std::to_string(outcome.available)
            + " parts=" + std::to_string(outcome.partCount)
            + " max deviation mm=" + std::to_string(outcome.maximumDeviationMm)
            + " " + outcome.refusalJa).c_str());
    }
    if (outcomes.size() < 2 || !outcomes[1].available || !window.FabricationDock().ClickCandidate(1)) return false;
    window.RunCommand("fabrication.create");
    if (!Explain("115屋根を部材近似", window.FabricationModelCount() == 1)) return false;
    for (double percent : {0., 50., 100.}) {
        window.SetAssemblyChooser([percent](double) { return std::optional<double>(percent); });
        window.RunCommand("fabrication.set_assembly");
        Note(window.StatusText().toUtf8().constData());
    }
    window.RunCommand("fabrication.create_pattern");
    window.RefreshExportCounts();
    auto& dock = window.ExportDock();
    if (!dock.ChooseTarget(app::ExportTarget::CurrentPattern) || !dock.ChooseFormat(app::ExportFormat::Svg)) return false;
    dock.ChoosePath(out + "/roof-pattern.svg");
    dock.SetOverwrite(true);
    if (!dock.RunNow()) { Note(dock.LastMessage().toUtf8().constData()); return false; }
    if (!dock.ChooseTarget(app::ExportTarget::Project) || !dock.ChooseFormat(app::ExportFormat::Kcd2)) return false;
    dock.ChoosePath(out + "/series115-with-roof-approx.kcd2");
    dock.SetOverwrite(true);
    if (!dock.RunNow()) { Note(dock.LastMessage().toUtf8().constData()); return false; }
    if (!window.OpenDocumentFile(out + "/series115-with-roof-approx.kcd2")) { Note(window.StatusText().toUtf8().constData()); return false; }
    return Explain("近似を含む別KCDも再生成", window.FabricationModelCount() == 1 && window.RebuildProblems().isEmpty());
}

[[nodiscard]] bool CaseHeavyRailway(V2MainWindow& window)
{
    const auto path = qEnvironmentVariable("KACHACAD_HEAVY_MODEL");
    const auto out = qEnvironmentVariable("KACHACAD_HEAVY_OUTPUT", "_claudeout/115-heavy");
    QDir().mkpath(out);
    QElapsedTimer timer;
    timer.start();
    if (!window.OpenDocumentFile(path)) return false;
    Note(("heavy open ms=" + std::to_string(timer.elapsed())).c_str());
    Note(window.RebuildProblems().toUtf8().constData());
    const auto shapes = window.Viewport().ShapeViews().size();
    Note(("heavy shapes=" + std::to_string(shapes)).c_str());
    Note(("heavy triangles=" + std::to_string(window.Viewport().ShapeTriangleCount())).c_str());
    if(qEnvironmentVariableIsSet("KACHACAD_PERFORMANCE_ONLY")) {
        window.resize(1800,1000); window.Viewport().FitToDocument(); QApplication::processEvents();
        auto& v=window.Viewport();
        HeavyOrbitTiming(v,"performance orbit/render");
        QImage frame(v.size(),QImage::Format_ARGB32);
        for(int i=0;i<10;++i) {
            timer.restart(); v.HoverAt(QPointF(v.width()*(.3+.04*i),v.height()*.5)); v.render(&frame);
            Note(("performance hover/render ms="+std::to_string(timer.elapsed())).c_str());
        }
        frame.save(out + "/performance-frame.png");
        return window.RebuildProblems().isEmpty() && shapes>=1000;
    }
    window.SetMode(app::UiMode::Part);
    window.resize(1800, 1000);
    QApplication::processEvents();
    if (!HeavyModelShots(window,out)) return false;
    if (!Explain("重量級モデルを欠落なく再生成", window.RebuildProblems().isEmpty()
            && shapes >= 1000 && shapes == static_cast<std::size_t>(CountKind(window, EntityKind::Part)))) return false;
    timer.restart();
    if (!window.SaveAndReopen("kacha_series115_heavy.kcd2")) return false;
    Note(("heavy save/reopen ms=" + std::to_string(timer.elapsed())).c_str());
    if (!Explain("保存後も形状数一致", window.RebuildProblems().isEmpty()
            && window.Viewport().ShapeViews().size() == shapes)) return false;
    auto& dock = window.ExportDock();
    window.RefreshExportCounts();
    dock.SetInteractiveHandler({});
    if (!dock.ChooseTarget(kachakacha::v2::app::ExportTarget::VisibleParts)) return false;
    for (const auto format : {kachakacha::v2::app::ExportFormat::Stl, kachakacha::v2::app::ExportFormat::Step}) {
        timer.restart();
        if (!dock.ChooseFormat(format)) return false;
        dock.ChoosePath(out + "/series115" + QString::fromStdString(std::string(kachakacha::v2::app::ExportFormatExtension(format))));
        dock.SetOverwrite(true);
        if (!dock.RunNow()) { Note(dock.LastMessage().toUtf8().constData()); return false; }
        Note(("heavy export ms=" + std::to_string(timer.elapsed())).c_str());
    }
    return HeavyRoofManufacturing(window, out);
}

std::vector<SelfTestCase> HoModelCases()
{
    std::vector<SelfTestCase> cases = {
        {"HP-HEAVY 非表示の押し出し輪郭を開き直す", CaseHiddenExtrudeProfile},
        {"HOの見本が開けて中身がそろう", CaseHoSampleOpens},
        {"HOの見本で作業平面と面へ正対できる", CaseHoFacingWorkPlanesAndSurface},
        {"HOの見本の輪郭を押し出せる", CaseHoExtrudeFromSampleProfile},
        {"HOの見本で曲げ具合を0から100まで動かせる", CaseHoApproximateAndBend},
        {"HOの見本の曲げ状態から線と面を作れる", CaseHoOutputsFromBendStates},
        {"HOの見本で実寸半径を入れて固定できる", CaseHoRadiusAutoAndLock},
        {"HOの見本が保存して開き直しても残る", CaseHoSampleSurvivesSaveAndOpen},
        {"曲げ半径は部材ごとに持ち、保存して開き直しても形ごと残る",
            CaseHoRadiusIsPerPartAndPersisted},
        {"HOの見本の上でも道具替えが綺麗", CaseHoToolSwitchStaysClean},
    };
    if (!qEnvironmentVariableIsEmpty("KACHACAD_HEAVY_MODEL")) {
        cases.push_back({"HP-HEAVY 重量級115系モデル", CaseHeavyRailway});
    }
    return cases;
}

} // namespace kachakacha::v2::selftest
