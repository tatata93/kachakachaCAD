//! 近似(製作モデル)と曲げ状態のケース(工程3)。
//!
//! 曲がった面を近似モデルにし、曲げ具合を変え、固定して工程2へ戻し、
//! 曲面へ落とした窓が型紙へ届くところまでを、画面の道で確かめる。
//! V2SelfTestModeling.cpp から分けたのは、ファイルの長さの門(1500行)を守るため。

#include "V2SelfTest.h"

#include "V2MainWindow.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/io/AtomicFile.h"

#include <filesystem>
#include <system_error>

#include <QPointF>
#include <QString>

#include <algorithm>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {

[[nodiscard]] bool MakeCurvedGuideSurface(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Arc);
    viewport.ClickAt(QPointF(viewport.width() * 0.30, viewport.height() * 0.60));
    viewport.ClickAt(QPointF(viewport.width() * 0.50, viewport.height() * 0.35));
    viewport.ClickAt(QPointF(viewport.width() * 0.70, viewport.height() * 0.60));
    // 30mm 離した平面を作って、同じ円弧をもう1本引く。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::WorkPlane));
    if (viewport.Selection().entityIds.empty()) {
        window.RunCommand("workplane.create");
        viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
            window.Session().GetDocument().Snapshot(),
            kachakacha::v2::domain::EntityKind::WorkPlane));
    }
    window.SetWorkPlaneChooser([](const WorkPlaneChoice&,
                                   const kachakacha::v2::app::WorkPlaneFacts&) {
        WorkPlaneChoice choice;
        choice.method = kachakacha::v2::modeling::WorkPlaneMethod::OffsetFromPlane;
        choice.offsetMm = 30.0;
        return std::optional<WorkPlaneChoice>(choice);
    });
    window.RunCommand("workplane.create");
    if (!Explain((std::string("離した平面が作れる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("平面から離す")))) {
        return false;
    }
    // 2本目は吸着を止めて引く。上から見ると1本目と同じ場所なので、
    // 止めないと1本目の端点へ吸着して z=0 へ落ち、同じ円弧が2本になる。
    // 人が引くときも Ctrl を押して同じことをする。
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Arc);
    viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(viewport.width() * 0.30, viewport.height() * 0.60));
    viewport.ClickAt(QPointF(viewport.width() * 0.50, viewport.height() * 0.35));
    viewport.ClickAt(QPointF(viewport.width() * 0.70, viewport.height() * 0.60));
    viewport.SetSnapSuppressed(false);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Wire));
    if (!Explain((std::string("円弧が2本ある(実際は ")
                     + std::to_string(viewport.Selection().entityIds.size()) + ")").c_str(),
            viewport.Selection().entityIds.size() == 2)) {
        return false;
    }
    window.RunCommand("guide.create");
    if (!Explain((std::string("曲がった面ができる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.StatusText().contains(QStringLiteral("面を作りました")))) {
        return false;
    }
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::GuideSurface));
    return true;
}

[[nodiscard]] bool CaseFabricationModelIsInTheDocument(V2MainWindow& window)
{
    // 近似モデルは文書のもの。作ると一覧に出て、保存して開き直しても戻る。
    // これまでは画面の配列にあるだけで、保存すると消えていた。
    if (!MakeCurvedGuideSurface(window)) {
        return false;
    }
    window.RunCommand("fabrication.create");
    if (!Explain((std::string("近似モデルができる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.FabricationModelCount() == 1)) {
        return false;
    }
    int models = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == kachakacha::v2::domain::EntityKind::FabricationModel) {
            ++models;
        }
    }
    if (!Explain("文書に近似モデルがある", models == 1)) {
        return false;
    }
    const std::string path = kachakacha::v2::io::FromPath(
        std::filesystem::temp_directory_path() / "kacha_selftest_fabrication.kcd2");
    std::error_code code;
    std::filesystem::remove(kachakacha::v2::io::MakePath(path), code);
    window.SetPathChooser([&path](bool) { return QString::fromStdString(path); });
    window.RunCommand("file.save_as");
    if (!Explain("保存できる", window.StatusText().contains(QStringLiteral("保存しました")))) {
        return false;
    }
    window.RunCommand("file.new");
    if (!Explain("新しい文書では消える", window.FabricationModelCount() == 0)) {
        return false;
    }
    const bool opened = window.OpenDocumentFile(QString::fromStdString(path));
    std::filesystem::remove(kachakacha::v2::io::MakePath(path), code);
    if (!Explain("開き直せる", opened)) {
        return false;
    }
    return Explain((std::string("近似モデルが作り直される(") + window.StatusText().toStdString()
                       + ")").c_str(),
        window.FabricationModelCount() == 1);
}

[[nodiscard]] bool CaseAssemblyPercentActuallyBends(V2MainWindow& window)
{
    // 組立率を変えると、形が本当に動く。これまでは数字が変わるだけだった。
    if (!MakeCurvedGuideSurface(window)) {
        return false;
    }
    window.RunCommand("fabrication.create");
    if (!Explain("近似モデルができる", window.FabricationModelCount() == 1)) {
        return false;
    }
    auto& viewport = window.Viewport();
    if (!Explain((std::string("曲げ状態の姿勢が画面に出る(レール ")
                     + std::to_string(viewport.FoldPreviewRailCount()) + " 本)").c_str(),
            viewport.FoldPreviewRailCount() >= 2)) {
        return false;
    }
    const std::uint64_t revision = window.Session().GetDocument().Revision();
    window.SetAssemblyChooser([](double) { return std::optional<double>(0.0); });
    window.RunCommand("fabrication.set_assembly");
    if (!Explain((std::string("組立率が文書に入る(") + window.StatusText().toStdString()
                     + ")").c_str(),
            window.Session().GetDocument().Revision() != revision
                && window.StatusText().contains(QStringLiteral("0%")))) {
        return false;
    }
    // 元に戻せる。文書の作り方を書き換えているからである。
    window.RunCommand("edit.undo");
    return Explain("元に戻せる", window.Session().GetDocument().Revision() == revision);
}

[[nodiscard]] bool CaseFabricationMethodCanBeSwitched(V2MainWindow& window)
{
    // V1 方式と V2 方式を切り替えられる。既定は V1 方式(帯)。
    if (!Explain("既定は帯近似",
            window.FabricationMethodInUse()
                == kachakacha::v2::app::FabricationMethod::BandApproximation)) {
        return false;
    }
    window.RunCommand("fabrication.set_method");
    if (!Explain((std::string("切り替わる(") + window.StatusText().toStdString() + ")").c_str(),
            window.FabricationMethodInUse()
                == kachakacha::v2::app::FabricationMethod::ClassifyFaces)) {
        return false;
    }
    window.RunCommand("fabrication.set_method");
    return Explain("戻る",
        window.FabricationMethodInUse()
            == kachakacha::v2::app::FabricationMethod::BandApproximation);
}

[[nodiscard]] bool CaseWindowProjectedOntoCurvedSurfaceOpensInBands(V2MainWindow& window)
{
    // 曲がった面に窓を開ける道。前面(ZX 面)に描いた四角を、面へ落として開口にする。
    // 落ちた線は帯の型紙へ切り出される(またぐなら、またぐ全ての帯へ)。
    if (!MakeCurvedGuideSurface(window)) {
        return false;
    }
    window.RunCommand("fabrication.create");
    if (!Explain("近似モデルができる", window.FabricationModelCount() == 1)) {
        return false;
    }
    // 前から見る面(ZX)を作って、その上に四角を描く。
    window.SetWorkPlaneChooser([](const WorkPlaneChoice&,
                                   const kachakacha::v2::app::WorkPlaneFacts&) {
        WorkPlaneChoice choice;
        choice.method = kachakacha::v2::modeling::WorkPlaneMethod::Standard;
        choice.standard = kachakacha::v2::modeling::StandardPlaneKind::ZX;
        return std::optional<WorkPlaneChoice>(choice);
    });
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("workplane.create");
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Front);
    viewport.SetVisibleWidthMm(200.0);
    const int wiresBefore = static_cast<int>(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), kachakacha::v2::domain::EntityKind::Wire)
                                                 .entityIds.size());
    // 画面の縦横比は環境で違う(窓のある PC では縦長)。mm で指して画面の点に直す。
    // 壁は z = 0..30 なので、四角は z = 8..20、x = ±8 に置く。
    const double heightMm = 200.0 * viewport.height() / std::max(1, viewport.width());
    const auto at = [&](double xMm, double zMm) {
        return QPointF(viewport.width() * (0.5 + xMm / 200.0),
            viewport.height() * (0.5 - zMm / heightMm));
    };
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Rectangle);
    viewport.ClickAt(at(-8.0, 8.0));
    viewport.HoverAt(at(8.0, 20.0));
    viewport.ClickAt(at(8.0, 20.0));
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    const auto wires = kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), kachakacha::v2::domain::EntityKind::Wire);
    if (!Explain((std::string("四角が描ける(") + window.StatusText().toStdString() + ")")
                     .c_str(),
            static_cast<int>(wires.entityIds.size()) == wiresBefore + 1)) {
        return false;
    }
    // 四角と面を選んで、曲面へ投影。
    kachakacha::v2::app::SelectionSet both;
    both.entityIds.push_back(wires.entityIds.back());
    for (const auto& id : kachakacha::v2::app::SelectAllOfKind(
             window.Session().GetDocument().Snapshot(),
             kachakacha::v2::domain::EntityKind::GuideSurface)
             .entityIds) {
        both.entityIds.push_back(id);
    }
    viewport.SetSelection(both);
    if (!Explain("曲面へ投影が押せる", window.CommandEnabled("wire.project_surface", nullptr))) {
        return false;
    }
    window.RunCommand("wire.project_surface");
    if (!window.StatusText().contains(QStringLiteral("面へ落とし"))) {
        // 落ちないときは、四角と面がどこにあるかを出す。PC でしか出ない失敗を推測で直さないため。
        std::string where = "四角:";
        for (const auto& curve : window.Session().Scene().curves) {
            if (curve.entityId == wires.entityIds.back()) {
                const auto p = curve.segment.StartPoint();
                where += " (" + std::to_string(p.x) + "," + std::to_string(p.y) + ","
                    + std::to_string(p.z) + ")";
            }
        }
        where += " 画面 " + std::to_string(viewport.width()) + "x"
            + std::to_string(viewport.height());
        return Explain((std::string("面へ落ちる(") + window.StatusText().toStdString() + " "
                           + where + ")").c_str(),
            false);
    }
    // 落ちた線を開口にする。
    const auto after = kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), kachakacha::v2::domain::EntityKind::Wire);
    kachakacha::v2::app::SelectionSet projected;
    projected.entityIds.push_back(after.entityIds.back());
    viewport.SetSelection(projected);
    window.RunCommand("fabrication.assign_role");
    if (!Explain((std::string("開口になる(") + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("開口を 1 つ")))) {
        return false;
    }
    return Explain((std::string("帯の型紙に窓の取り分がある(")
                       + std::to_string(window.FabricationOpeningCount()) + ")").c_str(),
        window.FabricationOpeningCount() >= 1);
}

[[nodiscard]] bool CaseFreezeAtBendStateMakesWiresSurfaceAndPart(V2MainWindow& window)
{
    // 工程3 → 工程2 へ戻る道。いまの曲げ状態(50%)で固定すると、
    // 線・面・部品が普通のものとして文書に入り、その部品を STEP で出せる。
    if (!MakeCurvedGuideSurface(window)) {
        return false;
    }
    window.RunCommand("fabrication.create");
    if (!Explain("近似モデルができる", window.FabricationModelCount() == 1)) {
        return false;
    }
    window.SetAssemblyChooser([](double) { return std::optional<double>(50.0); });
    window.RunCommand("fabrication.set_assembly");
    if (!Explain("50% にできる", window.StatusText().contains(QStringLiteral("50%")))) {
        return false;
    }
    // 作るものを「両方」にする(ワイヤーのみ → 部品のみ → 両方)。
    window.RunCommand("fabrication.freeze_output");
    window.RunCommand("fabrication.freeze_output");
    if (!Explain("両方を作る指定にできる",
            window.FreezeOutputInUse() == kachakacha::v2::fabrication::FreezeOutput::Both)) {
        return false;
    }
    const auto countKind = [&window](kachakacha::v2::domain::EntityKind kind) {
        int count = 0;
        for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
            if (entity.kind == kind
                && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
                ++count;
            }
        }
        return count;
    };
    const int wiresBefore = countKind(kachakacha::v2::domain::EntityKind::Wire);
    const int surfacesBefore = countKind(kachakacha::v2::domain::EntityKind::GuideSurface);
    const int partsBefore = CountOfKind(window, kachakacha::v2::domain::EntityKind::Part);
    window.RunCommand("fabrication.freeze_state");
    const QString status = window.StatusText();
    if (!Explain((std::string("固定できる(") + status.toStdString() + ")").c_str(),
            status.contains(QStringLiteral("現在状態を固定(")))) {
        return false;
    }
    if (!Explain("線が増える", countKind(kachakacha::v2::domain::EntityKind::Wire) > wiresBefore)) {
        return false;
    }
    if (!Explain("面が増える",
            countKind(kachakacha::v2::domain::EntityKind::GuideSurface) > surfacesBefore)) {
        return false;
    }
    if (!Explain((std::string("部品が増える(") + std::to_string(partsBefore) + " → "
                     + std::to_string(CountOfKind(window, kachakacha::v2::domain::EntityKind::Part)) + ")").c_str(),
            CountOfKind(window, kachakacha::v2::domain::EntityKind::Part) > partsBefore)) {
        return false;
    }
    // 固定した名前に曲げ状態が入る(V1 と同じ:「(組立 50%)」)。
    bool named = false;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.displayName.find("50%") != std::string::npos) {
            named = true;
        }
    }
    if (!Explain("名前に曲げ状態が入る", named)) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(),
        kachakacha::v2::domain::EntityKind::Part));
    return Explain("固定した部品を STEP で出せる", window.CanExportSelectedParts());
}

std::vector<SelfTestCase> FabricationCases()
{
    return {
        {"近似モデルは文書に入り開き直しても戻る", &CaseFabricationModelIsInTheDocument},
        {"組立率を変えると本当に曲がる", &CaseAssemblyPercentActuallyBends},
        {"近似の方式を切り替えられる", &CaseFabricationMethodCanBeSwitched},
        {"曲げ状態で固定すると線と面と部品になる", &CaseFreezeAtBendStateMakesWiresSurfaceAndPart},
        {"曲面へ落とした窓が帯の型紙に開く", &CaseWindowProjectedOntoCurvedSurfaceOpensInBands},
    };
}

} // namespace kachakacha::v2::selftest
