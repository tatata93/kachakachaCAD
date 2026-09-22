//! 辺の丸め・面取りの人の道(HP-FL、matrix P-12)。
//!
//! 帯の フィレット / 面取り を押す → 棚が出る → 3D で部品の辺の近くを押すと部品と辺が入る
//! (同じ辺をもう一度押すと外れる)→ 実際に核で丸めた稜線が下見に出て、体積の変わり方が
//! 状態に出る → Enter で部品になり(元は隠れる)、1 回の取り消しで戻り、開き直しても同じ辺を
//! 丸め直す。選ぶのは実際に拾う道(`SelectAt`)だけ。
#include "V2SelfTest.h"

#include "V2EdgeFinishDock.h"
#include "V2EdgeFinishTool.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/domain/Entity.h"

#include <QPointF>
#include <QString>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::Shelf;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::Vector3;

struct Box3 {
    Vector3 minimum;
    Vector3 maximum;
};

[[nodiscard]] EntityId NewestPart(V2MainWindow& window)
{
    EntityId newest;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Part) {
            newest = entity.id;
        }
    }
    return newest;
}

[[nodiscard]] std::vector<EntityId> VisibleParts(V2MainWindow& window)
{
    std::vector<EntityId> parts;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Part
            && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
            parts.push_back(entity.id);
        }
    }
    return parts;
}

[[nodiscard]] std::optional<Box3> PartBounds(V2MainWindow& window, const EntityId& id)
{
    for (const auto& shape : window.Viewport().ShapeViews()) {
        if (!shape.surface && shape.entityId == id && !shape.mesh.Empty()) {
            return Box3{shape.mesh.minimum, shape.mesh.maximum};
        }
    }
    return std::nullopt;
}

//! 矩形を引いて押し出し、箱を 1 つ作る。上から見た画面のまま返す。
[[nodiscard]] EntityId MakeBox(V2MainWindow& window, Box3& bounds)
{
    window.RunCommand("file.new");
    const EntityId wire = DrawRectangleAtByHand(window, 0.40, 0.40, 0.60, 0.60);
    if (wire.IsNil() || !ClickOnCurveOf(window, wire)) {
        return EntityId{};
    }
    window.RunCommand("part.extrude");
    if (!window.HandleToolKey(Qt::Key_Return, nullptr)) {
        return EntityId{};
    }
    const EntityId box = NewestPart(window);
    const auto found = PartBounds(window, box);
    if (!found.has_value()) {
        return EntityId{};
    }
    bounds = *found;
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    return box;
}

//! 上から見て、上の面の点を素のクリックで押す(辺から inset mm 内側)。
[[nodiscard]] bool PressTopFace(V2MainWindow& window, double x, double y, double z)
{
    auto& viewport = window.Viewport();
    const auto screen = viewport.Mapping().Project(Vector3{x, y, z});
    if (!screen.has_value()) {
        return false;
    }
    viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
    return true;
}

[[nodiscard]] bool Near(double actual, double expected, double relative)
{
    return std::abs(actual - expected) <= std::abs(expected) * relative;
}

//! HP-FL-01。フィレット: 箱の上の手前の辺の近くを押し、半径 2 で丸めると体積が式どおり減り、
//! Enter で部品になって元は隠れ、1 回で戻り、開き直しても丸めたまま。
[[nodiscard]] bool CaseFilletOneEdge(V2MainWindow& window)
{
    Box3 box{};
    const EntityId source = MakeBox(window, box);
    if (!Explain("箱を手で作れる", !source.IsNil())) {
        return false;
    }
    window.RunCommand("part.fillet");
    auto& tool = window.EdgeFinishTool();
    auto& dock = *tool.Dock();
    if (!Explain("フィレットを押すと棚が構える", tool.Active() && window.ShelfShown(Shelf::EdgeFinish))
        || !Explain("最初は部品待ち", window.ToolFooterTextJa().contains(QStringLiteral("NEXT=PART")))
        || !Explain("半径を 2 にできる", dock.TypeSize(2.0))) {
        return false;
    }
    const double centerX = (box.minimum.x + box.maximum.x) * 0.5;
    // 手前の辺(y = 最小、z = 上)から 3 mm 内側を押す。
    if (!Explain("上の面を押せる", PressTopFace(window, centerX, box.minimum.y + 3.0, box.maximum.z))
        || !Explain("部品と辺が 1 本入る", tool.Input().part == source && tool.Input().edges.size() == 1)
        || !Explain("入った辺は上の手前の辺",
            std::abs(tool.Input().edges.front().y - box.minimum.y) < 1.0e-3
                && std::abs(tool.Input().edges.front().z - box.maximum.z) < 1.0e-3)) {
        return false;
    }
    const std::string status = dock.StatusTextJa().toStdString();
    const double length = box.maximum.x - box.minimum.x;
    const double removed = (1.0 - 3.14159265358979323846 / 4.0) * 4.0 * length;
    if (!Explain((std::string("実際に丸めた結果が状態に出る(") + status + ")").c_str(),
            tool.Outcome().available && status.find("mm3") != std::string::npos)
        || !Explain((std::string("減った体積は (1 - π/4) r² L(") + std::to_string(
                         tool.Outcome().previousVolumeMm3 - tool.Outcome().volumeMm3) + " / "
                        + std::to_string(removed) + ")").c_str(),
            Near(tool.Outcome().previousVolumeMm3 - tool.Outcome().volumeMm3, removed, 0.01))
        || !Explain("下見が 3D に出ている", !window.Viewport().ToolPreview().empty())
        || !Explain("Enter で確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))) {
        return false;
    }
    const auto visible = VisibleParts(window);
    if (!Explain("丸めた部品だけが見える(元は隠れる)", visible.size() == 1 && !(visible.front() == source)
                && CountOfKind(window, EntityKind::Part) == 2)
        || !Explain("確定すると構えが解ける", !tool.Active())) {
        return false;
    }
    window.RunCommand("edit.undo");
    const auto undone = VisibleParts(window);
    if (!Explain("1 回の取り消しで元の箱に戻る", undone.size() == 1 && undone.front() == source)) {
        return false;
    }
    window.RunCommand("edit.redo");
    if (!Explain("保存して開き直せる",
            window.SaveAndReopen(QStringLiteral("kacha_selftest_fillet.kcd2")))) {
        return false;
    }
    const auto reopened = VisibleParts(window);
    return Explain((std::string("開き直しても丸めた部品の形がある(作り直せなかったもの「")
                       + window.RebuildProblems().toStdString() + "」)").c_str(),
        reopened.size() == 1 && PartBounds(window, reopened.front()).has_value()
            && window.RebuildProblems().isEmpty());
}

//! HP-FL-02。面取り: 上の手前と奥の辺を押して 2 本、手前をもう一度押すと外れ、また押すと入る。
//! 距離 1.5 で 2 本落とすと 2 × d²/2 × L だけ減る。
[[nodiscard]] bool CaseChamferTwoEdgesWithToggle(V2MainWindow& window)
{
    Box3 box{};
    const EntityId source = MakeBox(window, box);
    if (!Explain("箱を手で作れる", !source.IsNil())) {
        return false;
    }
    window.RunCommand("part.chamfer");
    auto& tool = window.EdgeFinishTool();
    auto& dock = *tool.Dock();
    const double centerX = (box.minimum.x + box.maximum.x) * 0.5;
    const double top = box.maximum.z;
    if (!Explain("面取りの棚が構える", tool.Active() && dock.KindShown() == 1
                && dock.SizeLabelJa() == QStringLiteral("距離"))
        || !Explain("距離を 1.5 にできる", dock.TypeSize(1.5))
        || !Explain("手前と奥の辺の近くを押せる",
            PressTopFace(window, centerX, box.minimum.y + 3.0, top)
                && PressTopFace(window, centerX, box.maximum.y - 3.0, top))
        || !Explain("辺が 2 本入る", tool.Input().edges.size() == 2)
        || !Explain("手前をもう一度押せる", PressTopFace(window, centerX, box.minimum.y + 3.0, top))
        || !Explain("押し直した辺は外れる", tool.Input().edges.size() == 1
                && std::abs(tool.Input().edges.front().y - box.maximum.y) < 1.0e-3)
        || !Explain("また押すと入る", PressTopFace(window, centerX, box.minimum.y + 3.0, top)
                && tool.Input().edges.size() == 2)) {
        return false;
    }
    const double length = box.maximum.x - box.minimum.x;
    const double removed = 2.0 * (1.5 * 1.5 / 2.0) * length;
    if (!Explain((std::string("減った体積は 2 × d²/2 × L(") + std::to_string(
                     tool.Outcome().previousVolumeMm3 - tool.Outcome().volumeMm3) + ")").c_str(),
            tool.Outcome().available
                && Near(tool.Outcome().previousVolumeMm3 - tool.Outcome().volumeMm3, removed, 1.0e-3))
        || !Explain("確定を押せる", dock.ClickConfirm())) {
        return false;
    }
    return Explain("面取りした部品ができる(元は隠れる)", VisibleParts(window).size() == 1
            && CountOfKind(window, EntityKind::Part) == 2);
}

} // namespace

std::vector<SelfTestCase> EdgeFinishCases()
{
    return {
        {"HP-FL-01 フィレットは辺の近くを押して半径で丸め体積が式どおり減り1回で戻り開き直しても同じ",
            CaseFilletOneEdge},
        {"HP-FL-02 面取りは辺を何本でも押して選び押し直すと外れ距離で落とす", CaseChamferTwoEdgesWithToggle},
    };
}

} // namespace kachakacha::v2::selftest
