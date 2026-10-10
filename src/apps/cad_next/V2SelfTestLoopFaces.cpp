#include <QComboBox>
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
#include "V2FabricationDock.h"
#include "V2EditDock.h"
#include "V2EntityTree.h"
#include "V2LoopFacesTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2Ribbon.h"

#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/app/LoopFaces.h"
#include "kachakacha/app/OriginPlanes.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include "kachakacha/modeling/ToolController.h"
#include "kachakacha/app/DirectWireEntry.h"
#include "kachakacha/modeling/WorkPlane.h"

#include <QApplication>
#include <QTreeWidgetItem>
#include <QPointF>
#include <QString>
#include <QPixmap>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
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
    QApplication::processEvents(); // カテゴリの折返し後の描画領域でクリックする。
    viewport.SetSnapSuppressed(true);
    // 棚の高さが変わっても、同じモデル座標の端をクリックする。
    const auto start = viewport.Mapping().Project({(x0-0.5)*200, (0.5-y0)*200, 0});
    const auto end = viewport.Mapping().Project({(x1-0.5)*200, (0.5-y1)*200, 0});
    if (!start || !end) return {};
    viewport.ClickAt(QPointF(start->x, start->y));
    viewport.HoverAt(QPointF(end->x, end->y));
    viewport.ClickAt(QPointF(end->x, end->y));
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
        || !Explain((std::string("形状ガイドが1枚増える: ")+window.StatusText().toStdString()).c_str(),
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
        || !Explain((std::string("形状ガイドが1枚増える: ")+window.StatusText().toStdString()).c_str(),
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
        || !Explain("面の道具を維持する", window.LoopFacesTool().Active())
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
    if (!Explain((std::string("ずれが 1 つ見つかる: ")+window.StatusText().toStdString()+" / gaps="+(gapTool.Plan()?std::to_string(gapTool.Plan()->gaps.size()):"no plan")).c_str(), gapTool.Active() && gapTool.Plan().has_value()
            && gapTool.Plan()->gaps.size() == 1)
        || !Explain("[そのまま] を押せる", gapTool.Dock() != nullptr && gapTool.Dock()->ClickLeaveGap(0))) {
        return false;
    }
    const std::uint64_t revision = window.Session().GetDocument().Revision();
    return Explain("Enter しても寄せず、輪が無いので面は作らない", window.HandleToolKey(Qt::Key_Return, nullptr))
        && Explain("作れない場合は道具と入力を維持する", window.LoopFacesTool().Active()
            && window.Viewport().Selection().entityIds.size()==3)
        && Explain("文書の版は変わらない(寄せていない)",
            window.Session().GetDocument().Revision() == revision)
        && Explain((std::string("状態行に「作りません」が出る(") + window.StatusText().toStdString()
                       + ")").c_str(),
            window.StatusText().contains(QStringLiteral("作りません")))
        && Explain("そのままを解除して同じ入力から再試行できる",gapTool.Dock()->ClickLeaveGap(0)
            &&window.HandleToolKey(Qt::Key_Return,nullptr)&&CountOfKind(window,EntityKind::GuideSurface)==1);
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
    const EntityId second = DrawLineAtByHand(window, 0.70, 0.30, 0.70, 0.70);
    const EntityId third = DrawLineAtByHand(window, 0.70, 0.70, 0.30, 0.70);
    const EntityId fourth = DrawLineAtByHand(window, 0.30, 0.70, 0.30, 0.30);
    if (!Explain("四角形を手で引ける", !first.IsNil() && !second.IsNil() && !third.IsNil() && !fourth.IsNil())) {
        return false;
    }
    SelectAllWires(window);
    window.RunCommand("surface.from_lines");
    auto& tool = window.LoopFacesTool();
    if (!tool.Dock()->ChooseMethod(0,1) || !tool.Dock()->ChooseStyle(0,1)) return false;
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
        || !Explain("開き直しても張り方を保持する", tool.Dock()->FaceRowTextJa(0).contains(QStringLiteral("平坦優先")))
        || !Explain("作り方を平面に変えて Enter で作り直せる", tool.Dock()->ChooseMethod(0, 0)
            && tool.MethodOf(0) == LoopFaceMethod::Planar
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
//! オーナーの atama.kcd2 と同じ 8 本を数値の線で置く(失敗なら偽)。
[[nodiscard]] bool PlaceAtamaWires(V2MainWindow& window)
{
    window.RunCommand("file.new");
    auto* scope = window.findChild<QComboBox*>(QStringLiteral("drawingScope"));
    if (!scope) return false;
    scope->setCurrentIndex(1); // These fixture lines intentionally leave the current workplane.
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
    return !big.IsNil() && !leftArc.IsNil() && !rightArc.IsNil() && !l1.IsNil() && !l2.IsNil()
        && !l3.IsNil() && !l4.IsNil() && !rib.IsNil();
}

[[nodiscard]] bool CaseLoopFacesOwnersAtamaWires(V2MainWindow& window)
{
    if (!Explain("オーナーの 8 本を数値の線で置ける", PlaceAtamaWires(window))) {
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


//! HP-AP-05。オーナー指示 2026-09-25「現状の近似はその形で出てこないのでボタン一つで」。
//! atama の 8 本 → 面にする → 面 2 枚を選んで 近似 を押すだけで、既定(縦に割る・縁の角で割る・
//! 面 1 枚 4 枚)の候補 B が、片側 4 部材 × 2、屋根と側面の境の角(±3,0,3)を通るレール、
//! 裾から上の輪郭へ上下に走るレールになる。Enter で面ごとに近似モデルが 1 つずつでき、1 回で戻る。
[[nodiscard]] bool CaseApproxOneButtonOnAtama(V2MainWindow& window)
{
    if (!Explain("オーナーの 8 本を置ける", PlaceAtamaWires(window))) {
        return false;
    }
    SelectAllWires(window);
    window.RunCommand("surface.from_lines");
    if (!Explain("面にする → Enter で四辺面 2 枚",
            window.HandleToolKey(Qt::Key_Return, nullptr)
                && CountOfKind(window, EntityKind::GuideSurface) == 2)) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface));
    window.RunCommand("fabrication.create");
    if (!Explain("面 2 枚を選んで 近似 を押すと 2 枚とも対象に入る",
            window.ApproxShelfShown() && window.ApproxInput().sources.size() == 2)) {
        return false;
    }
    auto& dock = window.FabricationDock();
    if (!Explain("既定は 縦に割る・角で割る・面 1 枚を 4 枚",
            dock.SplitAxisIndex() == 0 && dock.Choice().splitAtCorners
                && dock.Choice().equalPartCount == 4)) {
        return false;
    }
    const auto& outcomes = window.ApproxOutcomes();
    const auto& evaluations = window.ApproxEvaluations();
    if (!Explain((std::string("候補 B(帯)が作れて 8 部材(実際 ")
                    + (outcomes.size() > 1 ? std::to_string(outcomes[1].partCount) : "-") + ")").c_str(),
            outcomes.size() > 1 && outcomes[1].available && outcomes[1].partCount == 8
                && evaluations.size() > 1 && evaluations[1].has_value()
                && evaluations[1]->bandFaces.size() == 2)) {
        return false;
    }
    // レールは裾(z = 0)から上の輪郭(z ≥ 3)へ上下に走り、角(±3, 0, 3)を通るものがある
    // (四辺面ではその角は面の角なので、端のレール = 側面の縁の線がそこを通る)。
    bool cornerRail = false;
    bool verticalRails = true;
    for (const auto& face : evaluations[1]->bandFaces) {
        if (face.bands.bands.size() != 4) {
            verticalRails = false;
        }
        for (const auto& rail : face.mesh.world) {
            double low = 1.0e9;
            double high = -1.0e9;
            for (const Vector3& point : rail) {
                low = std::min(low, point.z);
                high = std::max(high, point.z);
                const double dx = std::abs(std::abs(point.x) - 3.0);
                if (dx < 0.15 && std::abs(point.y) < 0.15 && std::abs(point.z - 3.0) < 0.15) {
                    cornerRail = true;
                }
            }
            verticalRails = verticalRails && high - low > 2.5;
        }
    }
    if (!Explain("片側 4 部材ずつで、レールは上下に走る", verticalRails)
        || !Explain("屋根と側面の境の角を通るレールがある", cornerRail)
        || !Explain("下見のレールが 3D に出る", !window.Viewport().ToolPreview().empty())) {
        return false;
    }
    // 横に割ると、レールは水平(上下の動きが小さい)になる。
    dock.SetSplitAxisIndex(1);
    const auto& horizontal = window.ApproxEvaluations();
    bool flatRails = horizontal.size() > 1 && horizontal[1].has_value();
    if (flatRails) {
        for (const auto& face : horizontal[1]->bandFaces) {
            for (std::size_t row = 1; row + 1 < face.mesh.world.size(); ++row) {
                double low = 1.0e9;
                double high = -1.0e9;
                for (const Vector3& point : face.mesh.world[row]) {
                    low = std::min(low, point.z);
                    high = std::max(high, point.z);
                }
                flatRails = flatRails && high - low < 1.5;
            }
        }
    }
    if (!Explain("横に割ると内側のレールが水平になる", flatRails)) {
        return false;
    }
    dock.SetSplitAxisIndex(0);
    if (!Explain("Enter で確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("面ごとに近似モデルが 1 つずつできる(2 つ)", window.FabricationModelCount() == 2)) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1 回の取り消しで 2 つとも戻る", window.FabricationModelCount() == 0);
}

//! 近似モデルの作り方の定義(無ければ nullptr)。
[[nodiscard]] const kachakacha::v2::domain::CreateFabricationModelDefinition* ModelDefinition(
    V2MainWindow& window, const EntityId& model)
{
    const auto* entity = window.Session().GetDocument().FindEntity(model);
    const auto* feature = entity == nullptr ? nullptr
        : window.Session().GetDocument().FindFeature(entity->createdBy);
    return feature == nullptr ? nullptr
        : std::get_if<kachakacha::v2::domain::CreateFabricationModelDefinition>(&feature->definition);
}

//! その面の塗りの真ん中を素のクリックで押す(ID を選択へ直接入れない)。
[[nodiscard]] bool ClickOnSurfaceOf(V2MainWindow& window, const EntityId& surface)
{
    auto& viewport = window.Viewport();
    for (const auto& shape : viewport.ShapeViews()) {
        if (!shape.surface || shape.mesh.Empty() || !(shape.entityId == surface)) {
            continue;
        }
        // 曲がった面は外接箱の真ん中が面の上に無いことがある。三角形の真ん中を順に試す。
        for (std::size_t at = 0; at < shape.mesh.triangles.size(); ++at) {
            const auto screen = viewport.Mapping().Project(shape.mesh.triangles[at].Center());
            if (!screen.has_value()
                || !viewport.PickShapeAt(QPointF(screen->x, screen->y)).has_value()) {
                continue;
            }
            viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
            const auto& picked = viewport.Selection().entityIds;
            if (std::find(picked.begin(), picked.end(), surface) != picked.end()) {
                return true;   // 手前に別の面がかぶる三角形なら次を試す
            }
        }
        return false;
    }
    return false;
}

//! HP-AP-07。オーナー報告 2026-09-28「選択した部材がわからない」「選んだ面の曲げ状態を確認したいのに
//! 最後の一つしか曲げられない」。近似モデルが 2 つあるとき、3D で元の面を押すか一覧で「部材 n」を
//! 選ぶと、そのモデルが現在になって曲げがそこへ当たり、下見ではそのモデルの帯が選択色、対象部材の帯が塗られる。
[[nodiscard]] bool CaseApproxSelectedModelAndPartAreVisible(V2MainWindow& window)
{
    if (!Explain("オーナーの 8 本を置ける", PlaceAtamaWires(window))) {
        return false;
    }
    SelectAllWires(window);
    window.RunCommand("surface.from_lines");
    if (!Explain("面にする → Enter で四辺面 2 枚",
            window.HandleToolKey(Qt::Key_Return, nullptr)
                && CountOfKind(window, EntityKind::GuideSurface) == 2)) {
        return false;
    }
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface));
    window.RunCommand("fabrication.create");
    if (!Explain("近似 → Enter で近似モデルが 2 つ", window.ApproxShelfShown()
            && window.HandleToolKey(Qt::Key_Return, nullptr) && window.FabricationModelCount() == 2)) {
        return false;
    }
    std::vector<EntityId> models;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::FabricationModel) {
            models.push_back(entity.id);
        }
    }
    const auto* first = ModelDefinition(window, models[0]);
    const auto* second = ModelDefinition(window, models[1]);
    if (!Explain("2 つとも作り方が読める", models.size() == 2 && first != nullptr && second != nullptr)) {
        return false;
    }
    const auto* firstEntity = window.Session().GetDocument().FindEntity(models[0]);
    const auto* firstFeature = window.Session().GetDocument().FindFeature(firstEntity->createdBy);
    const EntityId firstSource = firstFeature->inputEntityIds.front();
    auto& viewport = window.Viewport();
    viewport.SetSelection(kachakacha::v2::app::SelectionSet{});
    if (!Explain("何も選んでいなければ最後のモデルが現在", window.CurrentFabricationModel() == models[1])
        || !Explain("下見は 8 帯(4 + 4)", viewport.FoldPreviewRailCount() == 16)
        // (対象部材の欄に番号が残っていれば、その帯は 2。どちらでも「強調されている」)
        || !Explain("最後のモデルの帯は強調(1 か 2)、もう 1 つは素(0)",
            viewport.FoldPreviewEmphasis(4) >= 1 && viewport.FoldPreviewEmphasis(0) == 0)) {
        return false;
    }
    // 3D で 1 つ目のモデルの元の面を押す → そのモデルが現在。押した帯が対象部材になり塗られる。
    auto& dock = window.FabricationDock();
    dock.SetStageIndex(1);
    dock.SetShowSource(true);
    // 上から見たままだと側面は線にしか見えず押せない。斜めから全体を見る。
    viewport.SetViewDirection(ViewDirection::Isometric);
    viewport.FitToDocument();
    int surfacesShown = 0;
    bool firstShown = false;
    for (const auto& shape : viewport.ShapeViews()) {
        surfacesShown += shape.surface && !shape.mesh.Empty() ? 1 : 0;
        firstShown = firstShown || shape.entityId == firstSource;
    }
    if (!Explain((std::string("3D で 1 つ目の元の面を押せる(面 ") + std::to_string(surfacesShown)
                    + (firstShown ? " 枚、1 つ目あり" : " 枚、1 つ目なし") + ", 選択 "
                    + std::to_string(viewport.Selection().entityIds.size()) + ")").c_str(),
            ClickOnSurfaceOf(window, firstSource))
        || !Explain("押した面のモデルが現在になる", window.CurrentFabricationModel() == models[0])
        || !Explain((std::string("対象部材に番号が入る(") + dock.PartNumbersText().toStdString() + ")").c_str(),
            !dock.PartNumbersText().trimmed().isEmpty())
        || !Explain("そのモデルの帯が選択色になり、最後のモデルの帯は素に戻る",
            viewport.FoldPreviewEmphasis(0) >= 1 && viewport.FoldPreviewEmphasis(4) == 0)) {
        return false;
    }
    bool painted = false;
    for (int band = 0; band < 4; ++band) {
        painted = painted || viewport.FoldPreviewEmphasis(band) == 2;
    }
    if (!Explain("対象部材の帯が塗られる(2)", painted)) {
        return false;
    }
    // 曲げの基準値 50 を押すと、現在のモデル(1 つ目)にだけ当たる。
    const double secondMaster = second->masterPercent;
    // 基準値のボタンと同じ道(組立率 50 → 当てる)。ボタンの見え方は HP-AP-04 が見ている。
    dock.SetAssemblyPercent(50.0);
    dock.PressApplyAssembly();
    first = ModelDefinition(window, models[0]);
    second = ModelDefinition(window, models[1]);
    bool firstBent = false;
    for (const double progress : first->bandProgress) {
        firstBent = firstBent || std::abs(progress - 0.5) < 1.0e-9;
    }
    if (!Explain("1 つ目のモデルの対象部材が 50% になる", firstBent)
        || !Explain("2 つ目のモデルは変わらない",
            std::abs(second->masterPercent - secondMaster) < 1.0e-9 && second->bandProgress.empty())) {
        return false;
    }
    // 一覧で 2 つ目のモデルの「部材 3」を選ぶ → そのモデルが現在、対象部材 = 3、その帯が塗られる。
    QTreeWidgetItem* modelRow = window.ItemOfEntity(models[1]);
    QTreeWidgetItem* partsRow = nullptr;
    for (int child = 0; modelRow != nullptr && child < modelRow->childCount(); ++child) {
        if (modelRow->child(child)->text(0) == QStringLiteral("部材")) {
            partsRow = modelRow->child(child);
        }
    }
    if (!Explain("一覧に 2 つ目のモデルの「部材」の節がある(4 枚)",
            partsRow != nullptr && partsRow->childCount() == 4)) {
        return false;
    }
    window.EntityTree()->clearSelection();
    partsRow->child(2)->setSelected(true);
    QApplication::processEvents();
    return Explain("「部材 3」を選ぶと 2 つ目のモデルが現在になる",
               window.CurrentFabricationModel() == models[1])
        && Explain((std::string("対象部材が 3(") + dock.PartNumbersText().toStdString() + ")").c_str(),
            dock.PartNumbersText().trimmed() == QStringLiteral("3"))
        && Explain("その帯(通し 7 番目)だけが塗られ、同じモデルの他の帯は選択色",
            viewport.FoldPreviewEmphasis(6) == 2 && viewport.FoldPreviewEmphasis(4) == 1
                && viewport.FoldPreviewEmphasis(5) == 1 && viewport.FoldPreviewEmphasis(0) == 0)
        && Explain("50 を当てると 2 つ目のモデルの部材 3 だけが 50% になる",
            (dock.SetAssemblyPercent(50.0), dock.PressApplyAssembly(), true)
                && ModelDefinition(window, models[1]) != nullptr
                && ModelDefinition(window, models[1])->bandProgress.size() == 4
                && std::abs(ModelDefinition(window, models[1])->bandProgress[2] - 0.5) < 1.0e-9
                && std::abs(ModelDefinition(window, models[1])->bandProgress[0] - 1.0) < 1.0e-9);
}

//! 3D に出ている面(形状ガイド)の枚数。文書に残っていても核の形が無ければ見えない。
[[nodiscard]] int SurfacesShownIn3D(V2MainWindow& window)
{
    int count = 0;
    for (const auto& shape : window.Viewport().ShapeViews()) {
        count += shape.surface && !shape.mesh.Empty() ? 1 : 0;
    }
    return count;
}

//! HP-AP-08。オーナー報告 2026-09-29: atama の 8 本を面にすると四辺面が 2 枚でき、片方に近似
//! (RebuildKernelShapes を通る)をかけると、選んでいない側の面が 3D から消えて一覧にだけ残った。
//! 原因は、輪と逆向きに引かれた最初の辺を持つ四辺面が、保存した作り方から作り直せず(UI-R005)、
//! 核の形が落ちていたこと。作り直しが両方とも通り、2 枚とも 3D に残ることを見る。
[[nodiscard]] bool CaseApproxKeepsOtherFaceVisible(V2MainWindow& window)
{
    if (!Explain("オーナーの 8 本を置ける", PlaceAtamaWires(window))) {
        return false;
    }
    SelectAllWires(window);
    window.RunCommand("surface.from_lines");
    if (!Explain("面にする → Enter で四辺面 2 枚",
            window.HandleToolKey(Qt::Key_Return, nullptr)
                && CountOfKind(window, EntityKind::GuideSurface) == 2)) {
        return false;
    }
    if (!Explain("作った直後は 2 枚とも 3D に出る", SurfacesShownIn3D(window) == 2)) {
        return false;
    }
    // 取り消し → やり直しは、作った面を核から作り直す道(RebuildKernelShapes)を通る。
    // 逆向きの辺で作り直せないと、ここで片方が消えていた。
    window.RunCommand("edit.undo");
    window.RunCommand("edit.redo");
    if (!Explain("取り消し・やり直しのあとも 2 枚とも文書にある",
            CountOfKind(window, EntityKind::GuideSurface) == 2)
        || !Explain((std::string("取り消し・やり直しのあとも 2 枚とも 3D に出る(実際 ")
                        + std::to_string(SurfacesShownIn3D(window)) + " 枚、作り直せなかったもの「"
                        + window.RebuildProblems().toStdString() + "」)").c_str(),
            SurfacesShownIn3D(window) == 2 && window.RebuildProblems().isEmpty())) {
        return false;
    }
    // 片方の面を選んで近似 → 確定(RebuildKernelShapes を通る)。もう片方が消えないこと。
    window.Viewport().SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface));
    // 全部選ぶと 2 枚とも対象になる。1 枚だけにするため、最初の 1 枚に絞る。
    const auto surfaces = kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface);
    kachakacha::v2::app::SelectionSet one;
    one.entityIds = {surfaces.entityIds.front()};
    window.Viewport().SetSelection(one);
    window.RunCommand("fabrication.create");
    if (!Explain("近似 → Enter で近似モデルが 1 つできる",
            window.ApproxShelfShown() && window.HandleToolKey(Qt::Key_Return, nullptr)
                && window.FabricationModelCount() == 1)) {
        return false;
    }
    return Explain((std::string("近似のあとも面は 2 枚とも 3D に残る(実際 ")
                       + std::to_string(SurfacesShownIn3D(window)) + " 枚)").c_str(),
               SurfacesShownIn3D(window) == 2)
        && Explain("面は 2 枚とも文書にある", CountOfKind(window, EntityKind::GuideSurface) == 2);
}

//! HP-AP-09。オーナー報告 2026-09-29「境界辺の色分けが面を消しても残る」。役割表の行の色分け
//! (境界辺 1〜4 の矢印)は、役割表の棚を見ている間だけ 3D に出る。棚から離れたら消える。
[[nodiscard]] bool CaseGuideRowsOnlyWithShelf(V2MainWindow& window)
{
    if (!Explain("役割表の場面が作れる", window.ApplyManualState(QStringLiteral("guide-table")))
        || !Explain("役割表の棚が前に出ている",
            window.ShelfShown(kachakacha::v2::app::Shelf::GuideTable))
        || !Explain("役割表の棚では行の色分けが 3D に出る", window.Viewport().GuideRowsShown() == 4)) {
        return false;
    }
    // 別のモード(作図)へ移ると、役割表の棚は隠れる。色分けも消す(面を消しても残らないのと同じ道)。
    window.SetMode(kachakacha::v2::app::UiMode::Drawing);
    if (!Explain("作図へ移ると役割表の棚は隠れる",
            !window.ShelfShown(kachakacha::v2::app::Shelf::GuideTable))
        || !Explain((std::string("棚を離れると行の色分けは消える(実際 ")
                        + std::to_string(window.Viewport().GuideRowsShown()) + ")").c_str(),
            window.Viewport().GuideRowsShown() == 0)) {
        return false;
    }
    // 表そのものは残っている(棚を開けばまた見える)。
    return Explain("役割表の中身は消えていない(棚を開けばまた出る)", window.GuideRowCount() == 4);
}

bool CaseLoopFacesToolFirst(V2MainWindow& window)
{
    window.resize(1500,900);QApplication::processEvents();
    if (DrawLineAtByHand(window,0.25,0.30,0.65,0.30).IsNil()
        || DrawLineAtByHand(window,0.65,0.30,0.45,0.70).IsNil()
        || DrawLineAtByHand(window,0.45,0.70,0.25,0.30).IsNil()) return false;
    window.Viewport().SetSelection({});
    if (!Explain("未選択から帯の面にするボタンを押す",window.Ribbon().ClickCategory(QStringLiteral("面作成"))
        &&window.Ribbon().ClickTool(QStringLiteral("面にする")))) return false;
    auto& tool=window.LoopFacesTool();
    if(!Explain("対象ゼロで専用ツールと棚を開く",tool.Active()&&window.ShelfShown(kachakacha::v2::app::Shelf::LoopFaces)))return false;
    const auto curves=window.Session().Scene().curves;
    for(const auto& curve:curves){
        const auto point=window.Viewport().Mapping().Project((curve.segment.StartPoint()+curve.segment.EndPoint())*0.5);
        if(!point)return false;
        window.Viewport().SelectAt({point->x,point->y},Qt::NoModifier);
    }
    if(!Explain("修飾キーなしで3本を追加して輪を下見",tool.Plan()&&tool.Plan()->faces.size()==1
        &&window.Viewport().Selection().entityIds.size()==3))return false;
    if(!window.HandleToolKey(Qt::Key_Return,nullptr))return false;
    if(!Explain("ツール先行の1回のEnterで面を作る",CountOfKind(window,EntityKind::GuideSurface)==1))return false;
    window.Viewport().SetSelection({});window.RunCommand("surface.from_lines");
    window.HandleToolKey(Qt::Key_Escape,nullptr);
    SelectAllWires(window);
    return Explain("Esc後も再選択を下見する",tool.Active()&&tool.Plan()&&tool.Plan()->faces.size()==1);
}

bool CaseLoopFacesCompoundBranch(V2MainWindow& window)
{
    const auto line=[](Vector3 a,Vector3 b){return CurveSegment::MakeLine(a,b).Value();};
    const auto wire=window.Session().AddWire({line({0,0,0},{5,0,0}),line({5,0,0},{10,0,0}),
        line({10,0,0},{10,10,0}),line({10,10,0},{5,10,0}),line({5,10,0},{0,10,0}),
        line({0,10,0},{0,0,0}),line({20,0,0},{30,0,0})},false,"複合境界");
    const auto rib=window.Session().AddWire({line({5,0,0},{5,10,0})},false,"中央");
    if(!wire.committed||!rib.committed||!window.SaveAndReopen("compound-loop-fixture.kcd2"))return false;
    const int before=CountOfKind(window,EntityKind::Wire);
    window.Viewport().SetSelection({});window.RunCommand("surface.from_lines");
    SelectAllWires(window);
    auto& tool=window.LoopFacesTool();
    if(!Explain("複合線の途中へ接続して2面と余る枝を認識する",tool.Plan()&&tool.Plan()->faces.size()==2
        &&tool.Plan()->summaryJa.find("輪に使わない区間 1")!=std::string::npos))return false;
    if(!Explain("面計算中と複数候補を表示する",tool.Dock()->SummaryTextJa().contains(QStringLiteral("計算中"))
        &&tool.Dock()->SummaryTextJa().contains(QStringLiteral("複数"))))return false;
    QApplication::processEvents();
    const auto both=window.Viewport().ToolPreviewFaceCount();
    if(!Explain("確定前に面を表示し文書には面を作らない",both>0&&CountOfKind(window,EntityKind::GuideSurface)==0))return false;
    if(!tool.Dock()->ToggleMake(0))return false;
    QApplication::processEvents();
    if(!Explain("作らない候補は面の下見から外れる",window.Viewport().ToolPreviewFaceCount()>0
        &&window.Viewport().ToolPreviewFaceCount()<both))return false;
    if(!tool.Dock()->ToggleMake(0))return false;
    window.HandleToolKey(Qt::Key_Escape,nullptr);
    QApplication::processEvents();
    if(!Explain("Esc後は遅れた下見も残らない",window.Viewport().ToolPreviewFaceCount()==0))return false;
    SelectAllWires(window);
    if(!Explain("Escから同じ入力で再試行する",tool.Active()&&tool.Plan()&&tool.Plan()->faces.size()==2))return false;
    window.HandleToolKey(Qt::Key_Return,nullptr);
    if(!Explain("枝を削除せず2面を作る",CountOfKind(window,EntityKind::GuideSurface)==2
        &&CountOfKind(window,EntityKind::Wire)==before))return false;
    window.RunCommand("edit.undo");
    if(CountOfKind(window,EntityKind::GuideSurface)!=0)return false;
    window.RunCommand("edit.redo");
    return Explain("保存・再読込後も両方の面を復元する",window.SaveAndReopen("compound-loop-result.kcd2")
        &&CountOfKind(window,EntityKind::GuideSurface)==2&&window.RebuildProblems().isEmpty());
}

bool CaseLoopFacesOwner115(V2MainWindow& window)
{
    const QString path=qEnvironmentVariable("KACHACAD_SURFACE_TEST_FILE");
    if(path.isEmpty()||!window.OpenDocumentFile(path))return false;
    window.resize(1600,1000);window.ApplyTheme(UiTheme::Windows95);
    window.Viewport().SetViewDirection(ViewDirection::Isometric);
    window.Viewport().FitToDocument();QApplication::processEvents();
    window.Viewport().SetSelection({});window.RunCommand("surface.from_lines");
    if(!window.LoopFacesTool().Active())return false;
    kachakacha::v2::app::SelectionSet selection;
    for(const auto& entity:window.Session().GetDocument().Snapshot().entities){
        for(const std::string suffix:{"02e","038","03b","302","37e","340","175","304"})
            if(entity.id.ToString().ends_with(suffix))selection.entityIds.push_back(entity.id);
    }
    if(!Explain("115の上部を構成する8ワイヤーを選ぶ",selection.entityIds.size()==8))return false;
    window.Viewport().SetSelection(selection);QApplication::processEvents();
    auto& tool=window.LoopFacesTool();
    if(!Explain("実データの区間から3区画を下見",tool.Plan()&&tool.Plan()->faces.size()==3))return false;
    const int before=CountOfKind(window,EntityKind::GuideSurface);
    const int wires=CountOfKind(window,EntityKind::Wire);
    const QString output=qEnvironmentVariable("KACHACAD_SURFACE_TEST_OUTPUT");
    if(!output.isEmpty())window.grab().save(output+QStringLiteral("-preview.png"));
    window.HandleToolKey(Qt::Key_Return,nullptr);QApplication::processEvents();
    if(!Explain("実データで面を3枚作り元ワイヤーを保持する",CountOfKind(window,EntityKind::GuideSurface)==before+3
        &&CountOfKind(window,EntityKind::Wire)==wires))return false;
    window.RunCommand("edit.undo");
    if (!Explain("実データの面3枚を一度で戻す",CountOfKind(window,EntityKind::GuideSurface)==before)) return false;
    window.RunCommand("edit.redo");
    if (!Explain("面3枚を再構築できる",CountOfKind(window,EntityKind::GuideSurface)==before+3)) return false;
    window.Viewport().SetSelection({});QApplication::processEvents();
    if(!output.isEmpty()){
        window.grab().save(output+QStringLiteral("-result.png"));
        window.SetPathChooser([output](bool){return output+QStringLiteral("-result.kcd2");});
        window.RunCommand("file.save_as");
    }
    return true;
}

bool CaseLoopFacesOwnerRegion(V2MainWindow& window, const QString& name,
    const std::vector<std::string>& suffixes, int unusedCount, int expected=1)
{
    const QString path=qEnvironmentVariable("KACHACAD_SURFACE_TEST_FILE");
    if(path.isEmpty()||!window.OpenDocumentFile(path))return false;
    window.resize(1600,1000);window.ApplyTheme(UiTheme::Windows95);
    window.Viewport().SetViewDirection(ViewDirection::Isometric);
    window.Viewport().FitToDocument();QApplication::processEvents();
    window.Viewport().SetSelection({});window.RunCommand("surface.from_lines");
    kachakacha::v2::app::SelectionSet selection;
    for(const auto& entity:window.Session().GetDocument().Snapshot().entities)
        for(const auto& suffix:suffixes)
            if(entity.id.ToString().ends_with(suffix))selection.entityIds.push_back(entity.id);
    if(selection.entityIds.size()!=suffixes.size())return false;
    window.Viewport().SetSelection(selection);QApplication::processEvents();
    const auto& plan=window.LoopFacesTool().Plan();
    if(!Explain("実モデルも確定前に面を表示する",window.Viewport().ToolPreviewFaceCount()>0))return false;
    if(!Explain("115の下部・閉曲線の輪を1つ下見",plan&&plan->faces.size()==static_cast<std::size_t>(expected)))return false;
    if(unusedCount>0&&!Explain("余る区間の数を明示する",plan->summaryJa.find(
        "輪に使わない区間 "+std::to_string(unusedCount))!=std::string::npos))return false;
    if(name=="roof-center"||name=="lower-right"){
        auto* dock=window.LoopFacesTool().Dock();
        for(int style:{1,2,0}){
            if(!dock->ChooseStyle(0,style))return false;
            QApplication::processEvents();
            if(!Explain("張り方を切替えても面の下見が出る",window.Viewport().ToolPreviewFaceCount()>0))return false;
            const auto output=qEnvironmentVariable("KACHACAD_SURFACE_TEST_OUTPUT");
            if(!output.isEmpty())window.grab().save(output+"-"+name+"-style-"+QString::number(style)+".png");
        }
    }
    const int before=CountOfKind(window,EntityKind::GuideSurface);
    const auto original=window.Session().GetDocument().Snapshot();
    const QString output=qEnvironmentVariable("KACHACAD_SURFACE_TEST_OUTPUT")+"-"+name;
    if(!qEnvironmentVariable("KACHACAD_SURFACE_TEST_OUTPUT").isEmpty())window.grab().save(output+"-preview.png");
    window.HandleToolKey(Qt::Key_Return,nullptr);QApplication::processEvents();
    if(!Explain("面が1枚でき元ワイヤーを保持する",CountOfKind(window,EntityKind::GuideSurface)==before+expected
        &&window.Session().GetDocument().Snapshot().entities.size()==original.entities.size()+expected))return false;
    window.RunCommand("edit.undo");
    if(CountOfKind(window,EntityKind::GuideSurface)!=before)return false;
    window.RunCommand("edit.redo");
    if(!Explain("面を再構築できる",CountOfKind(window,EntityKind::GuideSurface)==before+expected
        &&window.RebuildProblems().isEmpty()))return false;
    window.Viewport().SetSelection({});QApplication::processEvents();
    if(!qEnvironmentVariable("KACHACAD_SURFACE_TEST_OUTPUT").isEmpty()){
        window.grab().save(output+"-result.png");
        window.SetPathChooser([output](bool){return output+"-result.kcd2";});window.RunCommand("file.save_as");
        if(!window.OpenDocumentFile(output+"-result.kcd2")||!window.RebuildProblems().isEmpty())return false;
        if(CountOfKind(window,EntityKind::GuideSurface)!=before+expected)return false;
    }
    return true;
}

} // namespace

std::vector<SelfTestCase> LoopFacesCases()
{
    std::vector<SelfTestCase> cases{
        {"HP-LF-12 複合ワイヤーの枝と途中の接続から面を作る",CaseLoopFacesCompoundBranch},
        {"HP-LF-11 面にするを先に選び追加クリックで下見と確定",CaseLoopFacesToolFirst},
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
        {"HP-AP-05 atama の面 2 枚を選んで 近似 を押すだけで縦割り 4 部材 × 2(角のレールつき)、横割りも選べる",
            CaseApproxOneButtonOnAtama},
        {"HP-AP-07 近似モデルが 2 つあるとき、3D で押した面・一覧の「部材 n」のモデルが現在になり、曲げはそこへ当たり、帯が色分けされる",
            CaseApproxSelectedModelAndPartAreVisible},
        {"HP-AP-08 面を 2 枚作って片方を近似しても、もう片方の面は 3D に残る(逆向きの辺でも作り直せる)",
            CaseApproxKeepsOtherFaceVisible},
        {"HP-AP-09 役割表の行の色分け(境界辺)は役割表の棚を見ている間だけ 3D に出る",
            CaseGuideRowsOnlyWithShelf},
    };
    if(qEnvironmentVariableIsSet("KACHACAD_SURFACE_TEST_FILE")) {
        cases.push_back({"HP-LF-115-DOME 右だけの滑らかなドームを下見確定し再生成",CaseOwnerDome});
        cases.push_back({"HP-LF-115-R 下部右の丸めと枝を含む輪",[](V2MainWindow& w){
            return CaseLoopFacesOwnerRegion(w,"lower-right",{"070","249","280","327","1e0","1a2","1d6"},4);}});
        cases.push_back({"HP-LF-115-L 下部左の丸めと枝を含む輪",[](V2MainWindow& w){
            return CaseLoopFacesOwnerRegion(w,"lower-left",{"06a","250","280","319","201","1ac","1e2"},4);}});
        cases.push_back({"HP-LF-115-C 外周と中央断面のみの2曲面",[](V2MainWindow& w){
            return CaseLoopFacesOwnerRegion(w,"roof-center",{"02e","038","03b","302","340","175","304"},0,2);}});
        cases.push_back({"HP-LF-115-D 逆順区間を含む丸窓",[](V2MainWindow& w){
            return CaseLoopFacesOwnerRegion(w,"window",{"0db"},0);}});
        cases.push_back({"HP-LF-115 添付モデルの上部ワイヤーから面を作る",CaseLoopFacesOwner115});
    }
    return cases;
}

} // namespace kachakacha::v2::selftest
