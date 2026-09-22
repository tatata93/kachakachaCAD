//! 「線から面」の人の道(HP-LF、オーナー要望 2026-09-22)。
//!
//! 線を選んで「線から面」を押すだけで、端点のつながりから閉じた輪を全部見つけて
//! 面にする(V2LoopFacesTool)。端が離れていれば黙って寄せず、どこが何 mm 離れて
//! いるかを言い、Enter で寄せてから作る・Esc でやめるを人が選ぶ。元の線は残す。
//! 作る・寄せるはまとめて 1 回の取り消しで戻る。
//!
//! 選ぶのは実際に画面へ引く道(ClickAt / HoverAt)だけ。

#include "V2SelfTest.h"

#include "V2LoopFacesTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/LoopFaces.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/domain/Entity.h"
#include "kachakacha/geometry/CurveSegment.h"
#include "kachakacha/geometry/Vector3.h"
#include "kachakacha/modeling/ToolController.h"

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
    if (!Explain("線から面の道具が構える", tool.Active())
        || !Explain("計画が出る", tool.Plan().has_value())) {
        return false;
    }
    const auto& plan = *tool.Plan();
    if (!Explain("輪が1つ、平面、3本の線から", plan.faces.size() == 1
            && plan.faces.front().method == LoopFaceMethod::Planar
            && plan.faces.front().selections.size() == 3)
        || !Explain("ずれは無い", plan.gaps.empty())
        || !Explain((std::string("一番下の一行に「線から面」が出る(")
                        + window.ToolFooterTextJa().toStdString() + ")").c_str(),
            window.ToolFooterTextJa().contains(QStringLiteral("線から面")))) {
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
    if (!Explain("線から面の道具が構える", tool.Active())
        || !Explain("計画が出る", tool.Plan().has_value())) {
        return false;
    }
    const auto& plan = *tool.Plan();
    if (!Explain("ずれが1つだけ見つかる", plan.gaps.size() == 1)
        || !Explain("直線どうしなので寄せられる", plan.gaps.front().movable)
        || !Explain("輪はまだ無い(閉じていない)", plan.faces.empty())
        || !Explain((std::string("状態行に「離れています」が出る(")
                        + window.StatusText().toStdString() + ")").c_str(),
            window.StatusText().contains(QStringLiteral("離れています")))) {
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
    if (!Explain("線から面の道具が構える", window.LoopFacesTool().Active())) {
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

} // namespace

std::vector<SelfTestCase> LoopFacesCases()
{
    return {
        {"HP-LF-01 線から面は閉じた輪を全部見つけて面にし、元の線を残す",
            CaseLoopFacesMakesFaceKeepsLines},
        {"HP-LF-02 線から面はずれを言い、Enterで直線の端を寄せてから作る",
            CaseLoopFacesReportsGapAndCloses},
        {"HP-LF-03 線から面はEscで何も変えない",
            CaseLoopFacesEscapeChangesNothing},
    };
}

} // namespace kachakacha::v2::selftest
