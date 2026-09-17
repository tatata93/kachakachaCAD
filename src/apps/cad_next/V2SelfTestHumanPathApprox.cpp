//! 近似の人の道(HP-AP)。引継ぎ 2026-09-17 の 3。
//!
//! 近似を押す → 3D で面を押す(もう一度押すと外れる)→ 3つの候補が **実際に作られて**
//! 部材数とずれが並ぶ → 候補を押すと下見が変わる → Enter で確定、1回の取り消しで消える。
//!
//! 選ぶのは実際に拾う道(`SelectAt`)だけ。ID の注入も、見えない widget を叩くこともしない。

#include "V2SelfTest.h"

#include "V2FabricationDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/ApproxInput.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/domain/Feature.h"

#include <QPointF>
#include <QString>

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::domain::EntityKind;

//! 画面に見えている形状ガイドの塗りの真ん中を、素のクリックで押す。
//! 選ばれたかどうかは見ない(押し直して外す道にも使う)。当たり判定があるかだけ確かめる。
[[nodiscard]] bool PressAnyGuideSurface(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    for (const auto& shape : viewport.ShapeViews()) {
        if (!shape.surface || shape.mesh.Empty()) {
            continue;
        }
        const auto center = kachakacha::v2::geometry::Vector3{
            (shape.mesh.minimum.x + shape.mesh.maximum.x) * 0.5,
            (shape.mesh.minimum.y + shape.mesh.maximum.y) * 0.5,
            (shape.mesh.minimum.z + shape.mesh.maximum.z) * 0.5};
        const auto screen = viewport.Mapping().Project(center);
        if (!screen.has_value()
            || !viewport.PickShapeAt(QPointF(screen->x, screen->y)).has_value()) {
            continue;
        }
        viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
        return true;
    }
    return false;
}

//! 矩形から平らな面を1枚作り、選択を空にして近似を構えるところまで。
[[nodiscard]] bool ArmApproxOnFreshSurface(V2MainWindow& window)
{
    window.RunCommand("file.new");
    if (!Explain("手で矩形を引ける", DrawRectangleByHand(window))
        || !Explain("境界を画面から拾える", ClickOnAnyCurve(window, Qt::NoModifier))) {
        return false;
    }
    window.RunCommand("surface.create");
    if (!Explain("面の下見が見える", window.SurfacePreviewShown())
        || !Explain("Enterで面を確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("形状ガイドができる", CountOfKind(window, EntityKind::GuideSurface) == 1)) {
        return false;
    }
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Isometric);
    viewport.FitToDocument();
    viewport.SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);   // 空所を押して選択を外す
    window.RunCommand("fabrication.create");
    return Explain("近似を押すと製作の棚が構える",
               window.ApproxShelfShown() && window.ShelfShown(Shelf::Fabrication))
        && Explain("構えただけでは何も作らない", window.FabricationModelCount() == 0)
        && Explain("対象の欄は空", window.FabricationDock().SourcesTextJa().isEmpty())
        && Explain("一番下の一行に SOURCES=0",
            window.ToolFooterTextJa().contains(QStringLiteral("SOURCES=0")));
}

//! HP-AP-01。3D で対象を拾うと候補が作られて並び、押し直すと外れる。
[[nodiscard]] bool CaseHumanPathApproxSourcesFollowClicks(V2MainWindow& window)
{
    if (!ArmApproxOnFreshSurface(window)
        || !Explain("構えてから面を画面で拾える", ClickOnAnyGuideSurface(window))) {
        return false;
    }
    auto& dock = window.FabricationDock();
    const std::string sources = dock.SourcesTextJa().toStdString();
    if (!Explain((std::string("対象の欄に名前が出る(") + sources + ")").c_str(),
            !sources.empty())
        || !Explain("3D に SOURCE の札が出る",
            !window.Viewport().ToolRoleLabels().empty()
                && window.Viewport().ToolRoleLabels().front().text.startsWith(
                    QStringLiteral("SOURCE")))) {
        return false;
    }
    // 3つの候補は見積もりではなく、実際に作った結果。部材数とずれが行に出る。
    const auto& outcomes = window.ApproxOutcomes();
    if (!Explain("候補は3つとも作ってある",
            outcomes.size() == 3 && outcomes[0].evaluated && outcomes[1].evaluated
                && outcomes[2].evaluated)) {
        return false;
    }
    for (int candidate = 0; candidate < 3; ++candidate) {
        const std::string line = dock.CandidateTextJa(candidate).toStdString();
        const bool tellsResult = line.find("部材") != std::string::npos
            || line.find("作れません") != std::string::npos
            || line.find("×") != std::string::npos;
        if (!Explain((std::string("候補の行が結果を言う(") + line + ")").c_str(),
                tellsResult)) {
            return false;
        }
    }
    const std::string footer = window.ToolFooterTextJa().toStdString();
    if (!Explain((std::string("一番下の一行に対象と候補が出る(") + footer + ")").c_str(),
            footer.find("SOURCES=1") != std::string::npos
                && footer.find("CANDIDATE=") != std::string::npos
                && footer.find("Preview only") != std::string::npos)
        || !Explain("下見が 3D に出ている", !window.Viewport().ToolPreview().empty())
        || !Explain("下見の間、文書に近似モデルは無い", window.FabricationModelCount() == 0)) {
        return false;
    }
    // もう一度同じ面を押すと外れる。棚も札も一行も空へ戻る。
    if (!Explain("同じ面をもう一度押せる", PressAnyGuideSurface(window))
        || !Explain("押し直すと対象から外れる",
            window.ApproxInput().sources.empty() && dock.SourcesTextJa().isEmpty())
        || !Explain("一行も SOURCES=0 へ戻る",
            window.ToolFooterTextJa().contains(QStringLiteral("SOURCES=0")))
        || !Explain("札も消える", window.Viewport().ToolRoleLabels().empty())) {
        return false;
    }
    // Esc でやめると、文書は始める前とまったく同じ。
    if (!Explain("Escでやめられる", window.HandleToolKey(Qt::Key_Escape, nullptr))) {
        return false;
    }
    return Explain("やめると棚の構えが解ける", !window.ApproxShelfShown())
        && Explain("文書には面が1枚あるだけ",
            CountOfKind(window, EntityKind::GuideSurface) == 1
                && window.FabricationModelCount() == 0)
        && Explain("一行も消える", window.ToolFooterTextJa().isEmpty());
}

//! HP-AP-02。候補を押すと下見が変わり、Enter で下見と同じ作り方が文書へ入る。1回で戻せる。
[[nodiscard]] bool CaseHumanPathApproxCandidateThenConfirm(V2MainWindow& window)
{
    if (!ArmApproxOnFreshSurface(window)
        || !Explain("構えてから面を画面で拾える", ClickOnAnyGuideSurface(window))) {
        return false;
    }
    auto& dock = window.FabricationDock();
    if (!Explain("既定の候補は棚の方式どおり(帯 = B)", dock.SelectedCandidateShown() == 1)) {
        return false;
    }
    // 人が候補 A(面ごとに展開)を押す。棚のボタンで、見えているもの。
    if (!Explain("候補 A のボタンを押せる", dock.ClickCandidate(0))
        || !Explain("押した候補が選ばれて見える", dock.SelectedCandidateShown() == 0)
        || !Explain("一番下の一行も CANDIDATE=A",
            window.ToolFooterTextJa().contains(QStringLiteral("CANDIDATE=A")))) {
        return false;
    }
    const auto& outcomes = window.ApproxOutcomes();
    if (!Explain("平らな面は候補 A で作れる", outcomes.size() == 3 && outcomes[0].available)) {
        return false;
    }
    const std::size_t partsShown = outcomes[0].partCount;
    if (!Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("近似モデルが1つできる", window.FabricationModelCount() == 1)
        || !Explain("確定すると構えが解ける", !window.ApproxShelfShown())
        || !Explain("確定した部材数は下見と同じ",
            window.FabricationPanelCount() == partsShown)) {
        return false;
    }
    // 作り方は選んだ候補(面ごと = 方式 0)そのもの。下見と別の作り方で作り直していない。
    bool methodMatches = false;
    for (const auto& feature : window.Session().GetDocument().Snapshot().features) {
        if (const auto* made =
                std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(
                    &feature.definition)) {
            methodMatches = made->method == 0;
        }
    }
    if (!Explain("文書の作り方は押した候補のまま(方式 0)", methodMatches)) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1回の取り消しで近似モデルが消える", window.FabricationModelCount() == 0)
        && Explain("面は残る", CountOfKind(window, EntityKind::GuideSurface) == 1);
}

} // namespace

std::vector<SelfTestCase> HumanPathApproxCases()
{
    return {
        {"HP-AP-01 近似は 3D で押した対象が候補になり、押し直すと外れる",
            CaseHumanPathApproxSourcesFollowClicks},
        {"HP-AP-02 候補を押すと下見が変わり、Enter で同じ作り方が文書へ入る",
            CaseHumanPathApproxCandidateThenConfirm},
    };
}

} // namespace kachakacha::v2::selftest
