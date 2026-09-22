//! シェル・分割の人の道(HP-SH、matrix P-13)。
//!
//! 帯の シェル / 分割 を押す → 棚が出る → 3D で部品(シェルなら開けたい面)を押す
//! (同じ面をもう一度押すと外れる)→ 実際に核で作った稜線が下見に出て、体積が状態に出る
//! → Enter / 確定 で部品になり(元は隠れる)、1 回の取り消しで戻り、開き直しても同じ入力で
//! 作り直す。分割は平面が部品を通らなければ断り、通れば両側を 2 つの部品にする。
//! 選ぶのは実際に拾う道(`SelectAt`)だけ。
#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2ShellSplitDock.h"
#include "V2ShellSplitTool.h"
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

//! 上から見て、上の面の点を素のクリックで押す。
[[nodiscard]] bool PressAt(V2MainWindow& window, const Vector3& point)
{
    auto& viewport = window.Viewport();
    const auto screen = viewport.Mapping().Project(point);
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

[[nodiscard]] std::string Number(double value)
{
    return std::to_string(value);
}

//! 開き直して、見えている部品がどれも形を持ち、作り直せなかったものが無い。
[[nodiscard]] bool ReopenKeepsShapes(V2MainWindow& window, const QString& file, std::size_t visible)
{
    if (!Explain("保存して開き直せる", window.SaveAndReopen(file))) {
        return false;
    }
    const auto reopened = VisibleParts(window);
    bool shaped = reopened.size() == visible;
    for (const EntityId& id : reopened) {
        shaped = shaped && PartBounds(window, id).has_value();
    }
    return Explain((std::string("開き直しても部品の形がある(作り直せなかったもの「")
                       + window.RebuildProblems().toStdString() + "」)").c_str(),
        shaped && window.RebuildProblems().isEmpty());
}

//! HP-SH-01。シェル: 箱の上の面を押して抜く面に入れ(押し直すと外れ、また押すと入る)、
//! 肉厚 1 で内側へ残すと体積が式どおりになり、Enter で部品になって元は隠れ、1 回で戻り、
//! 開き直してもシェルのまま。
[[nodiscard]] bool CaseShellTopFace(V2MainWindow& window)
{
    Box3 box{};
    const EntityId source = MakeBox(window, box);
    const double sx = box.maximum.x - box.minimum.x;
    const double sy = box.maximum.y - box.minimum.y;
    const double sz = box.maximum.z - box.minimum.z;
    if (!Explain("箱を手で作れる", !source.IsNil())
        || !Explain("肉厚 1 が入る大きさ", sx > 4.0 && sy > 4.0 && sz > 2.0)) {
        return false;
    }
    window.RunCommand("part.shell");
    auto& tool = window.ShellSplitTool();
    auto& dock = *tool.Dock();
    if (!Explain("シェルを押すと棚が構える", tool.Active() && window.ShelfShown(Shelf::ShellSplit))
        || !Explain("シェルでは面と肉厚の欄が見え、平面の欄は隠れる",
            dock.FacesRowShown() && !dock.PlaneRowShown() && dock.MethodShown() == 0)
        || !Explain("最初は部品待ち", window.ToolFooterTextJa().contains(QStringLiteral("NEXT=PART")))
        || !Explain("肉厚を 1 にできる", dock.TypeThickness(1.0))) {
        return false;
    }
    const Vector3 topCenter{(box.minimum.x + box.maximum.x) * 0.5,
        (box.minimum.y + box.maximum.y) * 0.5, box.maximum.z};
    if (!Explain("上の面を押せる", PressAt(window, topCenter))
        || !Explain("部品と面が 1 枚入る", tool.Input().part == source && tool.Input().faces.size() == 1)
        || !Explain("入った面は上の面", std::abs(tool.Input().faces.front().z - box.maximum.z) < 1.0e-3)
        || !Explain("同じ面をもう一度押すと外れる",
            PressAt(window, topCenter) && tool.Input().faces.empty())
        || !Explain("また押すと入る", PressAt(window, topCenter) && tool.Input().faces.size() == 1)) {
        return false;
    }
    const double expected = sx * sy * sz - (sx - 2.0) * (sy - 2.0) * (sz - 1.0);
    const std::string status = dock.StatusTextJa().toStdString();
    if (!Explain((std::string("実際にシェルにした結果が状態に出る(") + status + ")").c_str(),
            tool.Outcome().available && status.find("mm3") != std::string::npos)
        || !Explain(("残る体積は 外 − (X−2t)(Y−2t)(Z−t)(" + Number(tool.Outcome().volumeMm3) + " / "
                        + Number(expected) + ")").c_str(),
            Near(tool.Outcome().volumeMm3, expected, 1.0e-3))
        || !Explain("下見が 3D に出ている", !window.Viewport().ToolPreview().empty())
        || !Explain("Enter で確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))) {
        return false;
    }
    const auto visible = VisibleParts(window);
    if (!Explain("シェルの部品だけが見える(元は隠れる)", visible.size() == 1 && !(visible.front() == source)
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
    return ReopenKeepsShapes(window, QStringLiteral("kacha_selftest_shell.kcd2"), 1);
}

//! HP-SH-02。分割: 作業平面(上面、z = 0)は箱の底と重なるので断る。「ずらす」で高さの半分へ
//! 動かすと両側が式どおりの体積で下見に出て、確定で 2 つの部品になり(元は隠れる)、1 回で戻り、
//! 開き直しても 2 つのまま。
[[nodiscard]] bool CaseSplitByWorkPlane(V2MainWindow& window)
{
    Box3 box{};
    const EntityId source = MakeBox(window, box);
    if (!Explain("箱を手で作れる", !source.IsNil())) {
        return false;
    }
    window.RunCommand("part.split");
    auto& tool = window.ShellSplitTool();
    auto& dock = *tool.Dock();
    const auto& plane = window.Viewport().WorkPlane();
    if (!Explain("分割を押すと棚が構える", tool.Active() && window.ShelfShown(Shelf::ShellSplit))
        || !Explain("分割では平面の欄が見え、面の欄は隠れる",
            dock.PlaneRowShown() && !dock.FacesRowShown() && dock.MethodShown() == 1)
        || !Explain("作業平面は上向き", plane.normal.z > 0.99)
        || !Explain("部品を押せる", PressAt(window, Vector3{(box.minimum.x + box.maximum.x) * 0.5,
                                                    (box.minimum.y + box.maximum.y) * 0.5, box.maximum.z}))
        || !Explain("部品が入る", tool.Input().part == source)) {
        return false;
    }
    const bool touchesBottom = std::abs(plane.origin.z - box.minimum.z) < 1.0e-6;
    if (touchesBottom
        && (!Explain(("底と重なる平面では分けない(" + dock.StatusTextJa().toStdString() + ")").c_str(),
                !tool.Outcome().available
                    && dock.StatusTextJa().contains(QStringLiteral("分ける平面が部品を通っていない")))
            || !Explain("作れないときは確定を押せない", !dock.ConfirmEnabled()))) {
        return false;
    }
    const double middle = (box.minimum.z + box.maximum.z) * 0.5;
    if (!Explain("ずらす量を打てる", dock.TypeOffset(middle - plane.origin.z))) {
        return false;
    }
    const double half = (box.maximum.x - box.minimum.x) * (box.maximum.y - box.minimum.y)
        * (box.maximum.z - box.minimum.z) * 0.5;
    if (!Explain(("両側の体積は半分ずつ(" + Number(tool.Outcome().volumeMm3) + " / "
                     + Number(tool.Outcome().otherVolumeMm3) + " / " + Number(half) + ")").c_str(),
            tool.Outcome().available && Near(tool.Outcome().volumeMm3, half, 1.0e-3)
                && Near(tool.Outcome().otherVolumeMm3, half, 1.0e-3))
        || !Explain("平面の説明が棚に出る", dock.PlaneTextJa().contains(QStringLiteral("作業平面")))
        || !Explain("下見が 3D に出ている", !window.Viewport().ToolPreview().empty())
        || !Explain("確定を押せる", dock.ClickConfirm())) {
        return false;
    }
    const auto visible = VisibleParts(window);
    if (!Explain("両側の 2 つが見える(元は隠れる)", visible.size() == 2
                && CountOfKind(window, EntityKind::Part) == 3)) {
        return false;
    }
    window.RunCommand("edit.undo");
    const auto undone = VisibleParts(window);
    if (!Explain("1 回の取り消しで元の箱に戻る", undone.size() == 1 && undone.front() == source)) {
        return false;
    }
    window.RunCommand("edit.redo");
    return ReopenKeepsShapes(window, QStringLiteral("kacha_selftest_split.kcd2"), 2);
}

} // namespace

std::vector<SelfTestCase> ShellSplitCases()
{
    return {
        {"HP-SH-01 シェルは面を押して選び押し直すと外れ肉厚で内側へ残し1回で戻り開き直しても同じ",
            CaseShellTopFace},
        {"HP-SH-02 分割は作業平面が部品を通らなければ断りずらすと両側を2つの部品にする",
            CaseSplitByWorkPlane},
    };
}

} // namespace kachakacha::v2::selftest
