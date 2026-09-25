//! 「面にする」(線から面)の人の道(HP-LF、オーナー要望 2026-09-22、UI 設計 2026-09-23)。
//!
//! 線を選んで「面にする」を押すだけで、端点のつながりから閉じた輪を全部見つけて
//! 面にする(V2LoopFacesTool)。端が離れていれば黙って寄せず、どこが何 mm 離れて
//! いるかを言い、Enter で寄せてから作る・[そのまま]で寄せない・Esc でやめるを人が選ぶ。
//! 線の端が別の線の途中に乗っていれば(T 字)、Enter のときにその線を分けてから作る。
//! 棚の輪の表で、作らない輪を外せる。元の線は残す。作る・寄せる・分けるはまとめて 1 回の取り消しで戻る。
//!
//! 選ぶのは実際に画面へ引く道(ClickAt / HoverAt)だけ。

#include "V2SelfTest.h"

#include "V2DrawingDock.h"
#include "V2EditDock.h"
#include "V2LoopFacesTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/LoopFaces.h"
#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/ToolController.h"
#include "kachakacha/app/DirectWireEntry.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <QPointF>
#include <QString>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::LoopFace;
using kachakacha::v2::app::LoopFaceMethod;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::modeling::DrawingTool;

//! 上から見た画面に、割合で指した2点を直線で結ぶ(人の道)。引いた線の番号(失敗なら Nil)。
[[nodiscard]] EntityId DrawLineAtByHand(V2MainWindow& window, double x0, double y0, double x1,
    double y1)
{
    auto& viewport = window.Viewport();
    const int before = CountOfKind(window, EntityKind::Wire);
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter(Vector3{});
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

//! いま文書にある線を全部選ぶ(3D の素のクリックの代わり。輪を探すには線が要る)。
void SelectAllWires(V2MainWindow& window)
{
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::Wire));
}

//! その線の線分(1 本目)。
[[nodiscard]] std::optional<CurveSegment> FirstSegmentOf(V2MainWindow& window, const EntityId& id)
{
    for (const auto& curve : window.Session().Scene().curves) {
        if (curve.entityId == id) {
            return curve.segment;
        }
    }
    return std::nullopt;
}

//! いま文書にある線が、端点でつながって輪になっているか(許容差 1e-6 mm)。
//! 「線から面」でずれを寄せたあと、全部の端がどれかの相手の端に届いているかを見る。
[[nodiscard]] bool WiresFormClosedLoop(V2MainWindow& window)
{
    std::vector<std::pair<Vector3, Vector3>> ends;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind != EntityKind::Wire) {
            continue;
        }
        const auto segment = FirstSegmentOf(window, entity.id);
        if (!segment.has_value()) {
            return false;
        }
        ends.emplace_back(segment->StartPoint(), segment->EndPoint());
    }
    if (ends.size() < 2) {
        return false;
    }
    const auto hasNeighbor = [&](std::size_t index, const Vector3& point) {
        for (std::size_t other = 0; other < ends.size(); ++other) {
            if (other == index) {
                continue;
            }
            if (geometry::Distance(point, ends[other].first) < 1.0e-6
                || geometry::Distance(point, ends[other].second) < 1.0e-6) {
                return true;
            }
        }
        return false;
    };
    for (std::size_t index = 0; index < ends.size(); ++index) {
        if (!hasNeighbor(index, ends[index].first) || !hasNeighbor(index, ends[index].second)) {
            return false;
        }
    }
    return true;
}

//! HP-LF-01。端点がぴったり重なる三角形。輪を1つ平面として見つけ、Enterで作る。
//! 元の線は残り、1回の取り消しで形状ガイドが戻る。
[[nodiscard]] bool CaseLoopFacesMakesFaceKeepsLines(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId first = DrawLineAtByHand(window, 0.30, 0.30, 0.70, 0.30);
    const EntityId second = DrawLineAtByHand(window, 0.70, 0.30, 0.50, 0.70);
    const EntityId third = DrawLineAtByHand(window, 0.50, 0.70, 0.30, 0.30);
    if (!Explain("端点がそろう三角形を手で引ける",
            !first.IsNil() && !second.IsNil() && !third.IsNil())) {
        return false;
    }
    SelectAllWires(window);
    window.RunCommand("surface.from_lines");
    auto& tool = window.LoopFacesTool();
    if (!Explain("面にするの道具が構える", tool.Active())
        || !Explain("計画が出る", tool.Plan().has_value())) {
        return false;
    }
    const auto& plan = *tool.Plan();
    if (!Explain("輪が1つ、平面、3本の線から", plan.faces.size() == 1
            && plan.faces.front().method == LoopFaceMethod::Planar
            && plan.faces.front().selections.size() == 3)
        || !Explain("ずれは無い", plan.gaps.empty())
        || !Explain((std::string("一番下の一行に「面にする」が出る(")
                        + window.ToolFooterTextJa().toStdString() + ")").c_str(),
            window.ToolFooterTextJa().contains(QStringLiteral("面にする")))
        || !Explain("棚の輪の表に 1 行", tool.Dock() != nullptr && tool.Dock()->FaceRowCount() == 1)) {
        return false;
    }
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    const int surfacesBefore = CountOfKind(window, EntityKind::GuideSurface);
    if (!Explain("Enterで作れる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("作ると道具は構えを解く", !window.LoopFacesTool().Active())
        || !Explain("元の線は残る(本数は変わらない)",
            CountOfKind(window, EntityKind::Wire) == wiresBefore)
        || !Explain("形状ガイドが1枚増える",
            CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore + 1)
        || !Explain((std::string("状態行に「作りました」が出る(")
                        + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("作りました")))) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1回の取り消しで形状ガイドが元の数へ戻る",
        CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore);
}

//! HP-LF-02。3本目の始点だけ少しずらす。ずれを言い、Enterで直線の端を寄せてから作る。
//! 寄せると作るはまとめて1つの取り消しで戻る(寄せた線も含めて)。
[[nodiscard]] bool CaseLoopFacesReportsGapAndCloses(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId first = DrawLineAtByHand(window, 0.30, 0.30, 0.70, 0.30);
    const EntityId second = DrawLineAtByHand(window, 0.70, 0.30, 0.50, 0.70);
    const EntityId third = DrawLineAtByHand(window, 0.52, 0.70, 0.30, 0.30);
    if (!Explain("端点を少しずらした3本を手で引ける",
            !first.IsNil() && !second.IsNil() && !third.IsNil())) {
        return false;
    }
    const auto endOfSecond = FirstSegmentOf(window, second);
    const auto startOfThird = FirstSegmentOf(window, third);
    if (!Explain("2本目・3本目の線分が拾える",
            endOfSecond.has_value() && startOfThird.has_value())) {
        return false;
    }
    const double gapMm = geometry::Distance(endOfSecond->EndPoint(), startOfThird->StartPoint());
    if (!Explain((std::string("実際のずれが 0.05 mm より大きい(実際 ")
                    + std::to_string(gapMm) + " mm)").c_str(), gapMm > 0.05)) {
        return false;
    }
    SelectAllWires(window);
    window.RunCommand("surface.from_lines");
    auto& tool = window.LoopFacesTool();
    if (!Explain("面にするの道具が構える", tool.Active())
        || !Explain("計画が出る", tool.Plan().has_value())) {
        return false;
    }
    const auto& plan = *tool.Plan();
    if (!Explain("ずれが1つだけ見つかる", plan.gaps.size() == 1)
        || !Explain("直線どうしなので寄せられる", plan.gaps.front().movable)
        || !Explain("輪はまだ無い(閉じていない)", plan.faces.empty())
        || !Explain((std::string("状態行に「離れて」が出る(")
                        + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("離れて")))
        || !Explain("棚のずれの行に mm の数が出る", tool.Dock() != nullptr
            && tool.Dock()->GapRowCount() == 1
            && tool.Dock()->GapRowTextJa(0).contains(QStringLiteral("mm")))) {
        return false;
    }
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    const int surfacesBefore = CountOfKind(window, EntityKind::GuideSurface);
    if (!Explain("Enterで寄せてから作れる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("作ると道具は構えを解く", !window.LoopFacesTool().Active())
        || !Explain("線の本数は変わらない(寄せても3本のまま)",
            CountOfKind(window, EntityKind::Wire) == wiresBefore)
        || !Explain("形状ガイドが1枚増える",
            CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore + 1)
        || !Explain((std::string("状態行に「作りました」が出る(")
                        + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("作りました")))
        || !Explain("寄せたあと3本は端点でつながって輪になる", WiresFormClosedLoop(window))) {
        return false;
    }
    window.RunCommand("edit.undo");
    if (!Explain("1回の取り消しで形状ガイドが元の数へ戻る(寄せも一緒に戻る)",
            CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore)) {
        return false;
    }
    const auto restoredSecond = FirstSegmentOf(window, second);
    const auto restoredThird = FirstSegmentOf(window, third);
    return Explain("取り消しでずれも元へ戻る(端点がまた離れている)",
        restoredSecond.has_value() && restoredThird.has_value()
            && geometry::Distance(restoredSecond->EndPoint(), restoredThird->StartPoint()) > 0.05);
}

//! HP-LF-03。Esc でやめると、何も変えない。
[[nodiscard]] bool CaseLoopFacesEscapeChangesNothing(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId first = DrawLineAtByHand(window, 0.30, 0.30, 0.70, 0.30);
    const EntityId second = DrawLineAtByHand(window, 0.70, 0.30, 0.50, 0.70);
    const EntityId third = DrawLineAtByHand(window, 0.50, 0.70, 0.30, 0.30);
    if (!Explain("三角形を手で引ける", !first.IsNil() && !second.IsNil() && !third.IsNil())) {
        return false;
    }
    SelectAllWires(window);
    window.RunCommand("surface.from_lines");
    if (!Explain("面にするの道具が構える", window.LoopFacesTool().Active())) {
        return false;
    }
    const std::uint64_t revision = window.Session().GetDocument().Revision();
    if (!Explain("Escでやめられる", window.HandleToolKey(Qt::Key_Escape, nullptr))
        || !Explain("道具の構えが解ける", !window.LoopFacesTool().Active())
        || !Explain("文書の版は変わらない", window.Session().GetDocument().Revision() == revision)
        || !Explain((std::string("状態行に「やめました」が出る(")
                        + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("やめました")))) {
        return false;
    }
    return true;
}


//! HP-LF-04。四角の底辺の途中から上辺の途中へ線を 1 本。端が別の線の途中に乗る(T 字)。
//! 系が底辺と上辺をそこで分け、輪を 2 つ見つける。Enter で分けてから 2 枚作る。
//! 分けた線は 2 本ずつになるが形は変わらず、1 回の取り消しで線も面も戻る。
[[nodiscard]] bool CaseLoopFacesSplitsTJunction(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId bottom = DrawLineAtByHand(window, 0.30, 0.30, 0.70, 0.30);
    const EntityId right = DrawLineAtByHand(window, 0.70, 0.30, 0.70, 0.70);
    const EntityId top = DrawLineAtByHand(window, 0.70, 0.70, 0.30, 0.70);
    const EntityId left = DrawLineAtByHand(window, 0.30, 0.70, 0.30, 0.30);
    const EntityId middle = DrawLineAtByHand(window, 0.50, 0.30, 0.50, 0.70);
    if (!Explain("四角と、底辺の途中から上辺の途中への線を手で引ける",
            !bottom.IsNil() && !right.IsNil() && !top.IsNil() && !left.IsNil()
                && !middle.IsNil())) {
        return false;
    }
    SelectAllWires(window);
    window.RunCommand("surface.from_lines");
    auto& tool = window.LoopFacesTool();
    if (!Explain("面にするの道具が構える", tool.Active())
        || !Explain("計画が出る", tool.Plan().has_value())) {
        return false;
    }
    const auto& plan = *tool.Plan();
    if (!Explain((std::string("T 字が 2 つ(底辺と上辺)見つかる(実際 ")
                    + std::to_string(plan.splits.size()) + ")").c_str(), plan.splits.size() == 2)
        || !Explain((std::string("輪が 2 つ、どちらも平面(実際 ")
                        + std::to_string(plan.faces.size()) + ")").c_str(),
            plan.faces.size() == 2 && plan.faces[0].method == LoopFaceMethod::Planar
                && plan.faces[1].method == LoopFaceMethod::Planar)
        || !Explain("ずれは無い", plan.gaps.empty())
        || !Explain("棚に T 字の一文が出る", tool.Dock() != nullptr
            && tool.Dock()->SplitTextJa().contains(QStringLiteral("途中に乗って")))) {
        return false;
    }
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    const int surfacesBefore = CountOfKind(window, EntityKind::GuideSurface);
    if (!Explain("Enterで分けてから作れる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("作ると道具は構えを解く", !window.LoopFacesTool().Active())
        || !Explain("分けた 2 本が 2 本ずつになる(5 → 7 本)",
            CountOfKind(window, EntityKind::Wire) == wiresBefore + 2)
        || !Explain("形状ガイドが 2 枚増える",
            CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore + 2)
        || !Explain((std::string("状態行に「作りました」が出る(")
                        + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("作りました")))) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1 回の取り消しで線の本数も形状ガイドも元へ戻る",
        CountOfKind(window, EntityKind::Wire) == wiresBefore
            && CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore);
}

//! HP-LF-05。棚の輪の表: ひし形と対角線(輪 2 つ)。2 つ目を「作らない」にすると Enter で 1 枚だけ。
//! ずれの [そのまま] は寄せない(HP-LF-02 の形で、Enter しても面は作らず文書も変わらない)。
[[nodiscard]] bool CaseLoopFacesDockChoices(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId a = DrawLineAtByHand(window, 0.30, 0.50, 0.50, 0.30);
    const EntityId b = DrawLineAtByHand(window, 0.50, 0.30, 0.70, 0.50);
    const EntityId c = DrawLineAtByHand(window, 0.70, 0.50, 0.50, 0.70);
    const EntityId d = DrawLineAtByHand(window, 0.50, 0.70, 0.30, 0.50);
    const EntityId diagonal = DrawLineAtByHand(window, 0.50, 0.30, 0.50, 0.70);
    if (!Explain("ひし形と対角線を手で引ける",
            !a.IsNil() && !b.IsNil() && !c.IsNil() && !d.IsNil() && !diagonal.IsNil())) {
        return false;
    }
    SelectAllWires(window);
    window.RunCommand("surface.from_lines");
    auto& tool = window.LoopFacesTool();
    if (!Explain("面にするの道具が構える", tool.Active() && tool.Plan().has_value())
        || !Explain("輪が 2 つ(対角線を挟む三角 2 つ)", tool.Plan()->faces.size() == 2)
        || !Explain("棚の輪の表に 2 行", tool.Dock() != nullptr && tool.Dock()->FaceRowCount() == 2)
        || !Explain("2 つ目の輪を「作らない」にできる", tool.Dock()->ToggleMake(1))
        || !Explain("1 つ目の作り方を境界面に変えられる", tool.Dock()->ChooseMethod(0, 1)
            && tool.MethodOf(0) == LoopFaceMethod::BoundaryFill)
        || !Explain("平面に戻せる", tool.Dock()->ChooseMethod(0, 0)
            && tool.MethodOf(0) == LoopFaceMethod::Planar)) {
        return false;
    }
    const int surfacesBefore = CountOfKind(window, EntityKind::GuideSurface);
    if (!Explain("Enterで作れる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("外した輪は作らないので 1 枚だけ増える",
            CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore + 1)) {
        return false;
    }
    window.RunCommand("edit.undo");
    // ずれの [そのまま]。
    window.RunCommand("file.new");
    const EntityId first = DrawLineAtByHand(window, 0.30, 0.30, 0.70, 0.30);
    const EntityId second = DrawLineAtByHand(window, 0.70, 0.30, 0.50, 0.70);
    const EntityId third = DrawLineAtByHand(window, 0.52, 0.70, 0.30, 0.30);
    if (!Explain("端をずらした 3 本を手で引ける", !first.IsNil() && !second.IsNil() && !third.IsNil())) {
        return false;
    }
    SelectAllWires(window);
    window.RunCommand("surface.from_lines");
    auto& gapTool = window.LoopFacesTool();
    if (!Explain("ずれが 1 つ見つかる", gapTool.Active() && gapTool.Plan().has_value()
            && gapTool.Plan()->gaps.size() == 1)
        || !Explain("[そのまま] を押せる", gapTool.Dock() != nullptr && gapTool.Dock()->ClickLeaveGap(0))) {
        return false;
    }
    const std::uint64_t revision = window.Session().GetDocument().Revision();
    return Explain("Enter しても寄せず、輪が無いので面は作らない", window.HandleToolKey(Qt::Key_Return, nullptr))
        && Explain("道具の構えが解ける", !window.LoopFacesTool().Active())
        && Explain("文書の版は変わらない(寄せていない)",
            window.Session().GetDocument().Revision() == revision)
        && Explain((std::string("状態行に「作りません」が出る(") + window.StatusText().toStdString()
                       + ")").c_str(),
            window.StatusText().contains(QStringLiteral("作りません")));
}

//! HP-LF-06。事実の行(UI 設計 2-5)。線を 1 本選ぶと、編集の棚に 載る面・長さ・端のつながりが出る。
//! 端が離れていれば何 mm かを言い、[始点を寄せる] でその場で寄る。1 回の取り消しで戻る。
[[nodiscard]] bool CaseWireFactsRowShowsGapAndCloses(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId first = DrawLineAtByHand(window, 0.30, 0.30, 0.70, 0.30);
    const EntityId second = DrawLineAtByHand(window, 0.70, 0.30, 0.50, 0.70);
    const EntityId third = DrawLineAtByHand(window, 0.52, 0.70, 0.30, 0.30);
    if (!Explain("端をずらした 3 本を手で引ける", !first.IsNil() && !second.IsNil() && !third.IsNil())) {
        return false;
    }
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(third);
    window.Viewport().SetSelection(one);
    auto& dock = window.EditDock();
    const QString facts = dock.FactsTextJa();
    if (!Explain((std::string("事実の行に 載る面 が出る(") + facts.toStdString() + ")").c_str(),
            facts.contains(QStringLiteral("載る面")))
        || !Explain("載る面は 上面 XY(上から見て引いた線)", facts.contains(QStringLiteral("上面 XY")))
        || !Explain("長さが出る", facts.contains(QStringLiteral("長さ")))
        || !Explain("始点は 2 本目の終点まで何 mm 離れているかが出る",
            facts.contains(QStringLiteral("始点 →")) && facts.contains(QStringLiteral("離れています")))
        || !Explain("終点は 1 本目の始点につながっている(0.000 mm)",
            facts.contains(QStringLiteral("終点 →")) && facts.contains(QStringLiteral("(0.000 mm)")))) {
        return false;
    }
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    if (!Explain("[始点を寄せる] が押せる", dock.PressCloseGap(false))
        || !Explain("寄せると 3 本は端点でつながって輪になる", WiresFormClosedLoop(window))
        || !Explain("線の本数は変わらない", CountOfKind(window, EntityKind::Wire) == wiresBefore)
        || !Explain((std::string("寄せたあとの事実の行は つながっている(") + dock.FactsTextJa().toStdString() + ")").c_str(),
            !dock.FactsTextJa().contains(QStringLiteral("離れています")))
        || !Explain("[寄せる] はもう出ない(押しても偽)", !dock.PressCloseGap(false))) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1 回の取り消しで端がまた離れる", !WiresFormClosedLoop(window));
}

//! HP-LF-07。辺の連続(UI 設計 2-3/2-4)。先に四角 A を面にしてから、A の右辺を共有する四角 B を
//! 面にする。B の共有辺には「すでにある面の縁」の欄が出て、G0 → G1 → G2 と回せる(棚の欄と 3D の Tab)。
//! 平面の輪では回せない(四辺面に変えると回せる)。Enter で作ると、連続を付けた札の面が増える。
[[nodiscard]] bool CaseLoopFacesEdgeContinuity(V2MainWindow& window)
{
    using kachakacha::v2::modeling::SurfaceContinuity;
    window.RunCommand("file.new");
    const EntityId a1 = DrawLineAtByHand(window, 0.30, 0.30, 0.50, 0.30);
    const EntityId shared = DrawLineAtByHand(window, 0.50, 0.30, 0.50, 0.70);
    const EntityId a3 = DrawLineAtByHand(window, 0.50, 0.70, 0.30, 0.70);
    const EntityId a4 = DrawLineAtByHand(window, 0.30, 0.70, 0.30, 0.30);
    if (!Explain("四角 A を手で引ける", !a1.IsNil() && !shared.IsNil() && !a3.IsNil() && !a4.IsNil())) {
        return false;
    }
    SelectAllWires(window);
    window.RunCommand("surface.from_lines");
    const int surfacesStart = CountOfKind(window, EntityKind::GuideSurface);
    if (!Explain("A を面にできる", window.LoopFacesTool().Active()
            && window.HandleToolKey(Qt::Key_Return, nullptr)
            && CountOfKind(window, EntityKind::GuideSurface) == surfacesStart + 1)) {
        return false;
    }
    const EntityId b1 = DrawLineAtByHand(window, 0.50, 0.30, 0.70, 0.30);
    const EntityId b2 = DrawLineAtByHand(window, 0.70, 0.30, 0.70, 0.70);
    const EntityId b3 = DrawLineAtByHand(window, 0.70, 0.70, 0.50, 0.70);
    if (!Explain("四角 B の 3 本を手で引ける", !b1.IsNil() && !b2.IsNil() && !b3.IsNil())) {
        return false;
    }
    kachakacha::v2::app::SelectionSet four;
    four.entityIds = {shared, b1, b2, b3};
    window.Viewport().SetSelection(four);
    window.RunCommand("surface.from_lines");
    auto& tool = window.LoopFacesTool();
    if (!Explain("B の輪が 1 つ見つかる", tool.Active() && tool.Plan().has_value()
            && tool.Plan()->faces.size() == 1 && tool.Plan()->faces[0].edges.size() == 4)) {
        return false;
    }
    int edgeWithNeighbor = -1;
    for (std::size_t e = 0; e < 4; ++e) {
        if (tool.Plan()->faces[0].edges[e].neighborSurface.has_value()) {
            edgeWithNeighbor = static_cast<int>(e);
        }
    }
    if (!Explain("共有辺だけが すでにある面 A の縁 と分かる", edgeWithNeighbor >= 0)
        || !Explain((std::string("棚にその辺の連続の欄が出る(")
                        + tool.Dock()->EdgeCellTextJa(0, edgeWithNeighbor).toStdString() + ")").c_str(),
            tool.Dock()->EdgeCellTextJa(0, edgeWithNeighbor).contains(QStringLiteral("G0")))
        || !Explain("平面の輪では回せない(押しても偽)", !tool.Dock()->CycleContinuity(0, edgeWithNeighbor))
        || !Explain("四辺面に変えられる", tool.Dock()->ChooseMethod(0, 1)
            && tool.MethodOf(0) == LoopFaceMethod::FourEdge)
        || !Explain("欄を押すと G1 になる", tool.Dock()->CycleContinuity(0, edgeWithNeighbor)
            && tool.ContinuityOf(0, static_cast<std::size_t>(edgeWithNeighbor)) == SurfaceContinuity::G1)
        || !Explain("欄の文が G1 に変わる",
            tool.Dock()->EdgeCellTextJa(0, edgeWithNeighbor).contains(QStringLiteral("G1")))) {
        return false;
    }
    // 3D で共有辺の上に置いて Tab。
    auto& viewport = window.Viewport();
    viewport.HoverAt(QPointF(viewport.width() * 0.50, viewport.height() * 0.50));
    if (!Explain("辺の上で Tab を押すと G2 になる", window.HandleToolKey(Qt::Key_Tab, nullptr)
            && tool.ContinuityOf(0, static_cast<std::size_t>(edgeWithNeighbor)) == SurfaceContinuity::G2)
        || !Explain("もう一度 Tab で G0 に戻る", window.HandleToolKey(Qt::Key_Tab, nullptr)
            && tool.ContinuityOf(0, static_cast<std::size_t>(edgeWithNeighbor)) == SurfaceContinuity::G0)
        || !Explain("さらに Tab で G1", window.HandleToolKey(Qt::Key_Tab, nullptr)
            && tool.ContinuityOf(0, static_cast<std::size_t>(edgeWithNeighbor)) == SurfaceContinuity::G1)) {
        return false;
    }
    const int surfacesBefore = CountOfKind(window, EntityKind::GuideSurface);
    if (!Explain("Enter で G1 の辺を持つ四辺面が作れる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain((std::string("形状ガイドが 1 枚増える(") + window.StatusText().toStdString() + ")").c_str(),
            CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore + 1)) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1 回の取り消しで戻る", CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore);
}

//! HP-LF-08。直前の操作(UI 設計 2-6)。作った直後、棚に「直前: 面にする(n 枚)」と [開いて直す] が出る。
//! 開くと作ったものが 1 回の取り消しで戻り、同じ線で構え直す(値を変えて Enter で作り直せる)。
//! そのあとに別の変更があれば開けない(黙って別の変更を戻さない)。
[[nodiscard]] bool CaseLoopFacesRecentReopens(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId first = DrawLineAtByHand(window, 0.30, 0.30, 0.70, 0.30);
    const EntityId second = DrawLineAtByHand(window, 0.70, 0.30, 0.50, 0.70);
    const EntityId third = DrawLineAtByHand(window, 0.50, 0.70, 0.30, 0.30);
    if (!Explain("三角形を手で引ける", !first.IsNil() && !second.IsNil() && !third.IsNil())) {
        return false;
    }
    SelectAllWires(window);
    window.RunCommand("surface.from_lines");
    auto& tool = window.LoopFacesTool();
    const int surfacesBefore = CountOfKind(window, EntityKind::GuideSurface);
    if (!Explain("Enter で作れる", tool.Active() && window.HandleToolKey(Qt::Key_Return, nullptr)
            && CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore + 1)
        || !Explain("作ったあと道具は構えを解く", !tool.Active())
        || !Explain("直前の操作が残る", tool.HasRecent())
        || !Explain((std::string("棚に「直前: 面にする(1 枚)」が出る(") + tool.Dock()->RecentTextJa().toStdString() + ")").c_str(),
            tool.Dock()->RecentTextJa().contains(QStringLiteral("直前")) && tool.Dock()->RecentTextJa().contains(QStringLiteral("1 枚")))
        || !Explain("直前の操作の棚が出ている", window.ShelfShown(kachakacha::v2::app::Shelf::LoopFaces))) {
        return false;
    }
    if (!Explain("[開いて直す] が押せる", tool.Dock()->ClickReopen())
        || !Explain("作ったものは戻る(形状ガイドが元の数)", CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore)
        || !Explain("同じ線で構え直す(輪 1 つ)", tool.Active() && tool.Plan().has_value() && tool.Plan()->faces.size() == 1)
        || !Explain("作り方を境界面に変えて Enter で作り直せる", tool.Dock()->ChooseMethod(0, 1)
            && tool.MethodOf(0) == LoopFaceMethod::BoundaryFill
            && window.HandleToolKey(Qt::Key_Return, nullptr)
            && CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore + 1)) {
        return false;
    }
    // そのあとに別の変更(線を 1 本足す)があれば、開けない。
    const EntityId extra = DrawLineAtByHand(window, 0.10, 0.10, 0.20, 0.10);
    if (!Explain("別の線を足せる", !extra.IsNil())) {
        return false;
    }
    window.SelectTool(DrawingTool::Select);
    const int wiresNow = CountOfKind(window, EntityKind::Wire);
    const bool reopened = tool.HasRecent() && tool.ReopenRecent();
    return Explain("別の変更のあとは開けない(黙って別の変更を戻さない)", !reopened)
        && Explain("線は減っていない", CountOfKind(window, EntityKind::Wire) == wiresNow)
        && Explain((std::string("状態行に「開けません」が出る(") + window.StatusText().toStdString() + ")").c_str(),
            !tool.HasRecent() || window.StatusText().contains(QStringLiteral("開けません")));
}

//! HP-LF-09。削除はワイヤー以外(面・作業平面)にも効く(オーナー指摘 2026-09-24)。
//! 面を選んで Del → 消える(線は残る)。面に使われている線を選んで Del → 理由を言って断る。
//! 原点の平面を選んで Del → 「原点の基準平面は消せません」。
[[nodiscard]] bool CaseDeleteWorksForNonWires(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId first = DrawLineAtByHand(window, 0.30, 0.30, 0.70, 0.30);
    const EntityId second = DrawLineAtByHand(window, 0.70, 0.30, 0.50, 0.70);
    const EntityId third = DrawLineAtByHand(window, 0.50, 0.70, 0.30, 0.30);
    if (!Explain("三角形を手で引ける", !first.IsNil() && !second.IsNil() && !third.IsNil())) {
        return false;
    }
    SelectAllWires(window);
    window.RunCommand("surface.from_lines");
    const int surfacesBefore = CountOfKind(window, EntityKind::GuideSurface);
    if (!Explain("面にするで 1 枚作れる", window.LoopFacesTool().Active()
            && window.HandleToolKey(Qt::Key_Return, nullptr)
            && CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore + 1)) {
        return false;
    }
    EntityId surface;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::GuideSurface) {
            surface = entity.id;
        }
    }
    // 面に使われている線は断る(理由つき)。
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(first);
    window.Viewport().SetSelection(one);
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    window.RunCommand("edit.delete");
    if (!Explain("面に使われている線は消えない", CountOfKind(window, EntityKind::Wire) == wiresBefore)
        || !Explain((std::string("状態行に「使っている」の理由が出る(") + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("使っている")))) {
        return false;
    }
    // 面を選んで Del。
    one.entityIds = {surface};
    window.Viewport().SetSelection(one);
    QString reason;
    if (!Explain((std::string("面を選ぶと削除が押せる(") + reason.toStdString() + ")").c_str(),
            window.CommandEnabled("edit.delete", &reason))) {
        return false;
    }
    window.RunCommand("edit.delete");
    if (!Explain("面が消える", CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore)
        || !Explain("線は残る", CountOfKind(window, EntityKind::Wire) == wiresBefore)) {
        return false;
    }
    // 原点の平面は断る。
    const auto top = kachakacha::v2::app::OriginPlaneId(window.Session().GetDocument().Snapshot(),
        kachakacha::v2::modeling::StandardPlaneKind::XY);
    if (!Explain("原点の平面 top_XY がある", top.has_value())) {
        return false;
    }
    one.entityIds = {*top};
    window.Viewport().SetSelection(one);
    const int planesBefore = CountOfKind(window, EntityKind::WorkPlane);
    window.RunCommand("edit.delete");
    if (!Explain("原点の平面は消えない", CountOfKind(window, EntityKind::WorkPlane) == planesBefore)
        || !Explain((std::string("状態行に「原点の基準平面は消せません」が出る(") + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("原点の基準平面は消せません")))) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("面の削除は 1 回の取り消しで戻る",
        CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore + 1);
}

//! 数値の線の道(右の棚「数値で線を作る」に値を入れて [線を作る] を押す)で 1 本置く。
//! 平面の円弧は始点・通過点・終点、3D の直線は 2 点。いまの作業平面の上に置く。
[[nodiscard]] EntityId AddWireByNumbers(V2MainWindow& window, const char* label,
    kachakacha::v2::app::DirectWireKind kind, std::vector<Vector3> points)
{
    const int before = CountOfKind(window, EntityKind::Wire);
    kachakacha::v2::app::DirectWireRequest request;
    request.kind = kind;
    request.points = std::move(points);
    window.DrawingDock().SetDirectWire(request, QString::fromUtf8(label));
    window.DrawingDock().PressCreateWire();
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

//! HP-LF-10。オーナーの atama.kcd2(2026-09-24「全然作れない」)と同じ 8 本: 裾の円弧 3 本(z = 0)、
//! 断面の直線 4 本(y = 0)、真ん中の肋の円弧(x = 0、裾の大円弧の途中に T 字で乗る)。線の向きは
//! 引いたまま。側 3 の行で 2 本目の円弧がつながらず UI-R005(2.449 mm)で 1 枚も作れなかった。
//! 直したあと: 四辺面 2・T 字 1 → Enter で核(四辺面)が実際に 2 枚作る。
[[nodiscard]] bool CaseLoopFacesOwnersAtamaWires(V2MainWindow& window)
{
    window.RunCommand("file.new");
    using kachakacha::v2::app::DirectWireKind;
    kachakacha::v2::modeling::WorkPlaneFrame top;   // 上面 XY(裾の円弧 3 本)
    window.Viewport().SetWorkPlane(top);
    const EntityId big = AddWireByNumbers(window, "円弧", DirectWireKind::PlanarArc,
        {{-3.5, 1.936491673, 0}, {0, 2.5, 0}, {3.5, 1.936491673, 0}});
    const EntityId leftArc = AddWireByNumbers(window, "円 1", DirectWireKind::PlanarArc,
        {{-3.5, 1.936491673, 0}, {-4.581139, 1.224745, 0}, {-5, 0, 0}});
    const EntityId rightArc = AddWireByNumbers(window, "円 2", DirectWireKind::PlanarArc,
        {{5, 0, 0}, {4.581139, 1.224745, 0}, {3.5, 1.936491673, 0}});
    const EntityId l1 = AddWireByNumbers(window, "直線 1", DirectWireKind::SpatialLine,
        {{5, 0, 0}, {3, 0, 3}});
    const EntityId l2 = AddWireByNumbers(window, "直線 2", DirectWireKind::SpatialLine,
        {{-5, 0, 0}, {-3, 0, 3}});
    const EntityId l3 = AddWireByNumbers(window, "直線 3", DirectWireKind::SpatialLine,
        {{3, 0, 3}, {0, 0, 3.5}});
    const EntityId l4 = AddWireByNumbers(window, "直線 4", DirectWireKind::SpatialLine,
        {{0, 0, 3.5}, {-3, 0, 3}});
    kachakacha::v2::modeling::WorkPlaneFrame side;   // 側面 YZ(肋の円弧): u = y, v = z
    side.uAxis = Vector3{0, 1, 0};
    side.vAxis = Vector3{0, 0, 1};
    side.normal = Vector3{1, 0, 0};
    window.Viewport().SetWorkPlane(side);
    const EntityId rib = AddWireByNumbers(window, "肋", DirectWireKind::PlanarArc,
        {{0, 3.5, 0}, {2.105897, 2.361355, 0}, {2.5, 0, 0}});
    window.Viewport().SetWorkPlane(top);
    if (!Explain("オーナーの 8 本を数値の線で置ける",
            !big.IsNil() && !leftArc.IsNil() && !rightArc.IsNil() && !l1.IsNil() && !l2.IsNil()
                && !l3.IsNil() && !l4.IsNil() && !rib.IsNil())) {
        return false;
    }
    SelectAllWires(window);
    window.RunCommand("surface.from_lines");
    auto& tool = window.LoopFacesTool();
    if (!Explain("面にするの道具が構える", tool.Active())
        || !Explain("計画が出る", tool.Plan().has_value())) {
        return false;
    }
    const auto& plan = *tool.Plan();
    if (!Explain((std::string("四辺面 2・T 字 1(実際: ") + plan.summaryJa + ")").c_str(),
            plan.faces.size() == 2 && plan.splits.size() == 1
                && plan.faces[0].method == LoopFaceMethod::FourEdge
                && plan.faces[1].method == LoopFaceMethod::FourEdge)
        || !Explain("ずれは無い", plan.gaps.empty())) {
        return false;
    }
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    const int surfacesBefore = CountOfKind(window, EntityKind::GuideSurface);
    const bool entered = window.HandleToolKey(Qt::Key_Return, nullptr);
    // 落ちたときに理由が残るよう、状態行と知らせを一文に入れる。
    const std::string after = "(線 " + std::to_string(CountOfKind(window, EntityKind::Wire)) + "/"
        + std::to_string(wiresBefore) + "、面 " + std::to_string(CountOfKind(window, EntityKind::GuideSurface))
        + "/" + std::to_string(surfacesBefore) + "、状態行: " + window.StatusText().toStdString()
        + "、知らせ: " + window.DiagnosticText().toStdString() + ")";
    if (!Explain("Enterで分けてから作れる", entered)
        || !Explain("作ると道具は構えを解く", !window.LoopFacesTool().Active())
        || !Explain(("四辺面が 2 枚できて大円弧が 2 本になる(8 → 9 本)" + after).c_str(),
            CountOfKind(window, EntityKind::Wire) == wiresBefore + 1
                && CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore + 2
                && window.StatusText().contains(QStringLiteral("2 枚作りました")))) {
        return false;
    }
    // 作った面には U/V の格子が乗る(オーナー指示 2026-09-25: 形が分かりにくい)。
    int gridded = 0;
    for (const auto& view : window.Viewport().ShapeViews()) {
        if (view.surface && !view.mesh.isoLines.empty()) {
            ++gridded;
        }
    }
    if (!Explain((std::string("作った 2 枚の面の両方に格子(U/V 線)が乗る(実際 ")
                    + std::to_string(gridded) + " 枚)").c_str(), gridded == 2)) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1 回の取り消しで線の本数も面も元へ戻る",
        CountOfKind(window, EntityKind::Wire) == wiresBefore
            && CountOfKind(window, EntityKind::GuideSurface) == surfacesBefore);
}

} // namespace

std::vector<SelfTestCase> LoopFacesCases()
{
    return {
        {"HP-LF-01 面にするは閉じた輪を全部見つけて面にし、元の線を残す",
            CaseLoopFacesMakesFaceKeepsLines},
        {"HP-LF-02 面にするはずれを言い、Enterで直線の端を寄せてから作る",
            CaseLoopFacesReportsGapAndCloses},
        {"HP-LF-03 面にするはEscで何も変えない",
            CaseLoopFacesEscapeChangesNothing},
        {"HP-LF-04 面にするはT字で線を分けてから輪を全部作り、1回の取り消しで戻る",
            CaseLoopFacesSplitsTJunction},
        {"HP-LF-05 面にするの棚で輪を外す・作り方を変える・ずれをそのままにできる",
            CaseLoopFacesDockChoices},
        {"HP-LF-06 線を選ぶと事実の行(載る面・長さ・端のつながり)が出て、[寄せる]でその場で寄る",
            CaseWireFactsRowShowsGapAndCloses},
        {"HP-LF-07 面にするは共有辺に連続の欄を出し、欄と Tab で G0→G1→G2 を回して作る",
            CaseLoopFacesEdgeContinuity},
        {"HP-LF-08 作った直後に「直前の操作」が出て、開いて直すと 1 回の取り消しで戻って構え直す",
            CaseLoopFacesRecentReopens},
        {"HP-LF-09 削除は面・作業平面にも効き、使われている線と原点の平面は理由を言って断る",
            CaseDeleteWorksForNonWires},
        {"HP-LF-10 オーナーの atama の 8 本(円弧 4・直線 4、T 字)を面にすると四辺面が 2 枚でき、面に格子が乗る",
            CaseLoopFacesOwnersAtamaWires},
    };
}

} // namespace kachakacha::v2::selftest
