//! C面取り / R丸めの人の道(HP-CN)。引継ぎ 2026-09-17 の 6。
//!
//! 面取りを押す → 面取りの道具になる → 3D で線を押すと A、次に押すと B → 実際に計算した
//! 結果が下見に出る(文書は変わらない)→ Enter で確定、1回の取り消しで戻る。
//!
//! 選ぶのは実際に押す道(`ClickAt`)だけ。ID の注入も、見えない widget を叩くこともしない。

#include "V2SelfTest.h"

#include "V2CornerDock.h"
#include "V2MainWindow.h"
#include "V2SurfaceDock.h"
#include "V2Viewport.h"

#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/SurfaceInputState.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/ToolController.h"

#include <QPointF>
#include <QString>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::modeling::DrawingTool;

//! 上から見て、画面の割合で指した2点を直線で結ぶ。引いた線の番号(失敗なら Nil)。
[[nodiscard]] EntityId DrawLineAtByHand(V2MainWindow& window, double x0, double y0, double x1,
    double y1)
{
    auto& viewport = window.Viewport();
    const int before = CountOfKind(window, EntityKind::Wire);
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter(kachakacha::v2::geometry::Vector3{});
    viewport.SetVisibleWidthMm(200.0);
    window.SelectTool(DrawingTool::Line);
    viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(viewport.width() * x0, viewport.height() * y0));
    viewport.HoverAt(QPointF(viewport.width() * x1, viewport.height() * y1));
    viewport.ClickAt(QPointF(viewport.width() * x1, viewport.height() * y1));
    viewport.SetSnapSuppressed(false);
    window.SelectTool(DrawingTool::Select);
    if (CountOfKind(window, EntityKind::Wire) != before + 1) {
        return EntityId{};
    }
    EntityId newest;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Wire) {
            newest = entity.id;
        }
    }
    return newest;
}

//! その線の上を、いま持っている道具のまま押す(`ClickAt`)。真ん中(t=0.5)を押す。
[[nodiscard]] bool PressCurveOf(V2MainWindow& window, const EntityId& id)
{
    auto& viewport = window.Viewport();
    for (const auto& curve : window.Session().Scene().curves) {
        if (!(curve.entityId == id)) {
            continue;
        }
        const auto screen = viewport.Mapping().Project(curve.segment.Evaluate(0.5));
        if (!screen.has_value()) {
            continue;
        }
        viewport.ClickAt(QPointF(screen->x, screen->y));
        return true;
    }
    return false;
}

//! `PressCurveOf` と同じだが、真ん中(t=0.5)ではなく指定した助変数の場所を押す。
//! 交点がちょうど真ん中に来る形(例: 3点円弧の通過点)では、真ん中を押すと
//! どちら側を残したいのかが曖昧になるので、こちらで片側をはっきり示す。
[[nodiscard]] bool PressCurveAt(V2MainWindow& window, const EntityId& id, double t)
{
    auto& viewport = window.Viewport();
    for (const auto& curve : window.Session().Scene().curves) {
        if (!(curve.entityId == id)) {
            continue;
        }
        const auto screen = viewport.Mapping().Project(curve.segment.Evaluate(t));
        if (!screen.has_value()) {
            continue;
        }
        viewport.ClickAt(QPointF(screen->x, screen->y));
        return true;
    }
    return false;
}

//! 上から見て、画面の割合で指した3点(始点・通過点・終点)を3点円弧で結んで手で引く。
//! `DrawLineAtByHand` と同じ道(道具を持つ→動かす→押す)を円弧向けに真似たもの。
[[nodiscard]] EntityId DrawArcAtByHand(V2MainWindow& window, double x0, double y0, double x1,
    double y1, double x2, double y2)
{
    auto& viewport = window.Viewport();
    const int before = CountOfKind(window, EntityKind::Wire);
    auto settings = window.DrawingDock().Settings();
    settings.arcMode = kachakacha::v2::modeling::ArcMode::ThreePoints;
    window.DrawingDock().SetSettings(settings);
    window.SelectTool(DrawingTool::Arc);
    viewport.SetSnapSuppressed(true);
    for (const QPointF& at : {QPointF(x0, y0), QPointF(x1, y1), QPointF(x2, y2)}) {
        const QPointF screen(viewport.width() * at.x(), viewport.height() * at.y());
        viewport.HoverAt(screen);
        viewport.ClickAt(screen);
    }
    viewport.SetSnapSuppressed(false);
    window.SelectTool(DrawingTool::Select);
    if (CountOfKind(window, EntityKind::Wire) != before + 1) {
        return EntityId{};
    }
    EntityId newest;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Wire) {
            newest = entity.id;
        }
    }
    return newest;
}

//! HP-CN-01。面取りは道具 → A → B → 下見 → Enter。
[[nodiscard]] bool CaseHumanPathChamferPreviewThenConfirm(V2MainWindow& window)
{
    window.RunCommand("file.new");
    // 交わる2本。押す場所(真ん中)が交点と重ならないように置く。
    const EntityId across = DrawLineAtByHand(window, 0.20, 0.50, 0.80, 0.50);
    const EntityId upright = DrawLineAtByHand(window, 0.70, 0.30, 0.70, 0.90);
    if (!Explain("手で線を2本引ける", !across.IsNil() && !upright.IsNil())) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);   // 空所を押して選択を外す
    window.RunCommand("wire.chamfer");
    if (!Explain("面取りを押すと面取りの道具になる",
            window.Session().CurrentTool() == DrawingTool::ChamferOrFilletPair)
        || !Explain("面取りの棚が見えている", window.ShelfShown(Shelf::Corner))
        || !Explain("押しただけでは下見は出ない", !window.CornerPreviewShown())) {
        return false;
    }
    auto& dock = window.CornerDock();
    const std::uint64_t revision = window.Session().GetDocument().Revision();
    if (!Explain("1本目を画面で押せる", PressCurveOf(window, across))
        || !Explain((std::string("A の欄に名前が出る(") + dock.FirstText().toStdString() + ")")
                        .c_str(),
            dock.FirstText() != QStringLiteral("(未選択)"))
        || !Explain("1本では下見は出ない", !window.CornerPreviewShown())
        || !Explain("2本目を画面で押せる", PressCurveOf(window, upright))
        || !Explain((std::string("B の欄に名前が出る(") + dock.SecondText().toStdString() + ")")
                        .c_str(),
            dock.SecondText() != QStringLiteral("(未選択)"))) {
        return false;
    }
    const std::string footer = window.ToolFooterTextJa().toStdString();
    bool sawA = false;
    bool sawB = false;
    for (const auto& label : window.Viewport().ToolRoleLabels()) {
        sawA = sawA || label.text == QStringLiteral("A");
        sawB = sawB || label.text == QStringLiteral("B");
    }
    if (!Explain("2本そろうと下見が出る", window.CornerPreviewShown())
        || !Explain("実際に計算した線が 3D に出ている", !window.Viewport().ToolPreview().empty())
        || !Explain("3D に A と B の札が出る", sawA && sawB)
        || !Explain((std::string("一番下の一行に A・B・量が出る(") + footer + ")").c_str(),
            footer.find("A=") != std::string::npos && footer.find("B=") != std::string::npos
                && footer.find("SIZE=") != std::string::npos
                && footer.find("Preview only") != std::string::npos)
        || !Explain("下見の間、文書は変わらない",
            window.Session().GetDocument().Revision() == revision)) {
        return false;
    }
    // 1本目をもう一度押すと外れ、下見も消える。
    if (!Explain("A をもう一度押せる", PressCurveOf(window, across))
        || !Explain("押し直すと外れて下見が消える", !window.CornerPreviewShown())
        || !Explain("もう一度押すと戻る", PressCurveOf(window, across))
        || !Explain("下見が戻る", window.CornerPreviewShown())) {
        return false;
    }
    const std::size_t before = window.Session().Scene().curves.size();
    if (!Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain((std::string("2本が 3本(A'・面取り・B')になる(")
                        + std::to_string(window.Session().Scene().curves.size()) + ")").c_str(),
            window.Session().Scene().curves.size() == before + 1)
        || !Explain("確定すると下見は消える", !window.CornerPreviewShown())) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1回の取り消しで2本へ戻る", window.Session().Scene().curves.size() == before);
}

//! HP-CN-02。R丸めも同じ道。Esc でやめると何も変わらない。
[[nodiscard]] bool CaseHumanPathFilletCancelLeavesNothing(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId across = DrawLineAtByHand(window, 0.20, 0.50, 0.80, 0.50);
    const EntityId upright = DrawLineAtByHand(window, 0.70, 0.30, 0.70, 0.90);
    if (!Explain("手で線を2本引ける", !across.IsNil() && !upright.IsNil())) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    window.RunCommand("wire.fillet");
    auto& dock = window.CornerDock();
    if (!Explain("丸めを押すと棚の種類が R丸め になる", dock.Choice().fillet)
        || !Explain("1本目を画面で押せる", PressCurveOf(window, across))
        || !Explain("2本目を画面で押せる", PressCurveOf(window, upright))
        || !Explain("下見が出る", window.CornerPreviewShown())
        || !Explain("一番下の一行は R丸め",
            window.ToolFooterTextJa().startsWith(QStringLiteral("R丸め")))) {
        return false;
    }
    const std::size_t before = window.Session().Scene().curves.size();
    if (!Explain("Escでやめられる", window.HandleToolKey(Qt::Key_Escape, nullptr))) {
        return false;
    }
    return Explain("やめると選択道具へ戻る",
               window.Session().CurrentTool() == DrawingTool::Select)
        && Explain("下見も札も一行も消える",
            !window.CornerPreviewShown() && window.Viewport().ToolRoleLabels().empty()
                && window.ToolFooterTextJa().isEmpty())
        && Explain("線は2本のまま", window.Session().Scene().curves.size() == before);
}

//! HP-CN-03。面取り(丸め)は直線と円弧の角も落とせる。角は共有する端点ではなく
//! 曲線どうしの交わりで決まり、押した側が残る。
[[nodiscard]] bool CaseHumanPathFilletLineArcCorner(V2MainWindow& window)
{
    using kachakacha::v2::app::ParameterId;
    using kachakacha::v2::app::ParameterValueOf;
    using kachakacha::v2::geometry::CurveKind;
    // 面取り量 / 丸め半径。CornerSizeMm() は V2MainWindow の private な近道で、道具の
    // 中からしか呼べない。試験は道具の外から見ているので、数の棚(ParameterDock)から
    // 同じ値を読み直す(V2MainWindow::CornerSizeMm 自身と同じ式)。
    const auto cornerSizeMm = [&window] {
        return ParameterValueOf(window.ParameterDock().Values(), ParameterId::CornerSize);
    };
    window.RunCommand("file.new");
    // 水平な直線。
    const EntityId line = DrawLineAtByHand(window, 0.20, 0.50, 0.80, 0.50);
    // 右へ膨らむ3点円弧。通過点 (0.75, 0.50) がちょうど直線の上に来て交わる。
    const EntityId arc = DrawArcAtByHand(window, 0.60, 0.25, 0.75, 0.50, 0.60, 0.75);
    if (!Explain("手で直線と円弧を1本ずつ引ける", !line.IsNil() && !arc.IsNil())) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);   // 空所を押して選択を外す
    window.RunCommand("wire.fillet");
    // 直線は真ん中(交点より左)を押して左側を残す。円弧は通過点(t=0.5)がそのまま
    // 交点なので、そこを押すと片側を選んだことにならない。始点寄りの t=0.25(上側)を押す。
    if (!Explain("直線を画面で押せる(左側を残す)", PressCurveOf(window, line))
        || !Explain("円弧を画面で押せる(交点を避けて上側を残す)",
            PressCurveAt(window, arc, 0.25))
        || !Explain("2つそろうと下見が出る", window.CornerPreviewShown())
        || !Explain("実際に計算した線が3Dに出ている", !window.Viewport().ToolPreview().empty())
        || !Explain("一番下の一行は R丸め",
            window.ToolFooterTextJa().startsWith(QStringLiteral("R丸め")))) {
        return false;
    }
    // 元の直線の長さを覚えておく。角を落とした後、残る側はこれより短くなるはず。
    double originalLineLengthMm = 0.0;
    for (const auto& curve : window.Session().Scene().curves) {
        if (curve.entityId == line) {
            originalLineLengthMm = kachakacha::v2::geometry::Distance(
                curve.segment.StartPoint(), curve.segment.EndPoint());
        }
    }
    const std::size_t before = window.Session().Scene().curves.size();
    if (!Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain((std::string("2本が3本(直線'・面取り弧・円弧')になる(")
                        + std::to_string(window.Session().Scene().curves.size()) + ")").c_str(),
            window.Session().Scene().curves.size() == before + 1)
        || !Explain("確定すると下見は消える", !window.CornerPreviewShown())) {
        return false;
    }
    bool hasFilletArc = false;
    bool sawLine = false;
    bool lineIsShorter = true;
    for (const auto& curve : window.Session().Scene().curves) {
        if (curve.segment.Kind() == CurveKind::CircularArc
            && std::abs(curve.segment.Radius() - cornerSizeMm()) < 1.0e-6) {
            hasFilletArc = true;
        }
        if (curve.segment.Kind() == CurveKind::Line) {
            sawLine = true;
            const double lengthMm = kachakacha::v2::geometry::Distance(
                curve.segment.StartPoint(), curve.segment.EndPoint());
            // 押した側(左)が残る: 交点で切られているので元より短いはず。
            // ここが崩れる(元と同じ長さのまま)なら、右側(交点の向こう側)を
            // 落とせておらず、押した側が残るという約束が破れている。
            if (lengthMm >= originalLineLengthMm - 1.0e-6) {
                lineIsShorter = false;
            }
        }
    }
    if (!Explain((std::string("面取り弧の半径が角の量と同じ(角の量=")
                     + std::to_string(cornerSizeMm()) + ")").c_str(),
            hasFilletArc)
        || !Explain("直線は残っている", sawLine)
        || !Explain("残った直線は交点の向こう側を持たず、元より短い(押した左側が残る)",
            lineIsShorter)) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1回の取り消しで2本へ戻る", window.Session().Scene().curves.size() == before);
}

//! HP-SF-08。回転体は 道具 → 断面 → 軸(自動遷移)→ 下見 → Enter。ガイドの欄が「軸」になる。
[[nodiscard]] bool CaseHumanPathRevolveSectionThenAxis(V2MainWindow& window)
{
    using kachakacha::v2::modeling::ChainRole;
    using kachakacha::v2::modeling::GuideSurfaceMethod;
    window.RunCommand("file.new");
    // 断面: 軸から離れた縦線。軸: 中央の縦線。どちらも上面 XY の中。
    const EntityId section = DrawLineAtByHand(window, 0.65, 0.40, 0.65, 0.60);
    const EntityId axis = DrawLineAtByHand(window, 0.50, 0.30, 0.50, 0.70);
    if (!Explain("手で線を2本引ける", !section.IsNil() && !axis.IsNil())) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    window.RunCommand("guide.revolve");
    auto& dock = window.SurfaceDock();
    if (!Explain("回転体を押すと面を作るの棚が構える",
            window.ShelfShown(Shelf::Surface) && !window.SurfacePreviewShown())
        || !Explain("作り方は回転体",
            window.SurfaceInput().method == GuideSurfaceMethod::Revolve)
        || !Explain("最初のクリックは断面へ", dock.ActiveSlotShown() == ChainRole::Section)
        || !Explain("断面を画面で拾える", ClickOnCurveOf(window, section))
        || !Explain("断面の欄に入る",
            window.SurfaceInput().sections.size() == 1
                && window.SurfaceInput().sections.front() == section)
        || !Explain("自動で軸待ちへ移る", dock.ActiveSlotShown() == ChainRole::GuideU)
        || !Explain("一番下の一行が NEXT=軸",
            window.ToolFooterTextJa().contains(QStringLiteral("NEXT=軸")))
        || !Explain("軸を画面で拾える", ClickOnCurveOf(window, axis))
        || !Explain("軸の欄(ガイドの欄)に入る",
            window.SurfaceInput().guides.size() == 1
                && window.SurfaceInput().guides.front() == axis)
        || !Explain("実際に回した面が下見に出る", window.SurfacePreviewShown())
        || !Explain("下見の間、形状ガイドは無い",
            CountOfKind(window, EntityKind::GuideSurface) == 0)) {
        return false;
    }
    if (!Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("形状ガイドが1つできる", CountOfKind(window, EntityKind::GuideSurface) == 1)
        || !Explain("作り方は回転体で保存される",
            window.GuideRoleTable().method == GuideSurfaceMethod::Revolve
                && window.GuideRoleTable().revolveAngleRad > 6.28)
        || !Explain("線は2本のまま", CountOfKind(window, EntityKind::Wire) == 2)) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1回の取り消しで消える", CountOfKind(window, EntityKind::GuideSurface) == 0);
}

} // namespace

std::vector<SelfTestCase> HumanPathCornerCases()
{
    return {
        {"HP-SF-08 回転体は道具 → 断面 → 軸(自動遷移)→ 下見 → Enter",
            CaseHumanPathRevolveSectionThenAxis},
        {"HP-CN-01 面取りは道具 → A → B → 下見 → Enter、押し直すと外れる",
            CaseHumanPathChamferPreviewThenConfirm},
        {"HP-CN-02 丸めも同じ道で、Esc でやめると何も変わらない",
            CaseHumanPathFilletCancelLeavesNothing},
        {"HP-CN-03 面取りは直線と円弧の角も落とせる(押した側を残す)",
            CaseHumanPathFilletLineArcCorner},
    };
}

} // namespace kachakacha::v2::selftest
