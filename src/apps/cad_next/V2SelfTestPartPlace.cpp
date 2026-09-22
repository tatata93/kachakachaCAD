//! 部品の配置の人の道(HP-PL、P-18)。
//!
//! 線と同じ道具・同じ点の置き方で部品を動かす: 部品を 3D で押して選ぶ → 帯の「配置」の道具 →
//! 3D で点を置く → 部品が変換どおりに動く(元の形に同じ変換を掛けた新しい部品)。
//! 動かす・回すは元を隠し、写す・鏡・並べるは元を残す。1 回の取り消しで戻り、
//! 保存して開き直しても同じ変換で作り直せる。

#include "V2SelfTest.h"

#include "V2ArrayDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/domain/Entity.h"

#include <QPointF>
#include <QString>

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::Vector3;

struct Box3 {
    Vector3 minimum;
    Vector3 maximum;
};

//! 画面の割合で指した矩形を引き、その線を拾って押し出し、Enter で部品にする。
[[nodiscard]] EntityId MakeBox(V2MainWindow& window, double x0, double y0, double x1, double y1)
{
    const EntityId wire = DrawRectangleAtByHand(window, x0, y0, x1, y1);
    if (wire.IsNil() || !Explain("線を画面から拾える", ClickOnCurveOf(window, wire))) {
        return EntityId{};
    }
    window.RunCommand("part.extrude");
    if (!Explain("Enterで部品にできる", window.HandleToolKey(Qt::Key_Return, nullptr))) {
        return EntityId{};
    }
    EntityId newest;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Part) {
            newest = entity.id;
        }
    }
    return newest;
}

//! 見えている部品(一覧の順)。
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

//! 画面に出ている、その部品の外接箱。
[[nodiscard]] std::optional<Box3> BoundsOf(V2MainWindow& window, const EntityId& id)
{
    for (const auto& shape : window.Viewport().ShapeViews()) {
        if (!shape.surface && shape.entityId == id && !shape.mesh.Empty()) {
            return Box3{shape.mesh.minimum, shape.mesh.maximum};
        }
    }
    return std::nullopt;
}

//! 上から見て、その部品の塗りの真ん中を素のクリックで押す(選択)。
[[nodiscard]] bool PressPart(V2MainWindow& window, const EntityId& id)
{
    auto& viewport = window.Viewport();
    const auto box = BoundsOf(window, id);
    if (!box.has_value()) {
        return Explain("その部品が画面に出ている", false);
    }
    const Vector3 center{(box->minimum.x + box->maximum.x) * 0.5,
        (box->minimum.y + box->maximum.y) * 0.5, box->maximum.z};
    const auto screen = viewport.Mapping().Project(center);
    if (!screen.has_value()) {
        return false;
    }
    viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
    const auto& selected = viewport.Selection().entityIds;
    return Explain("押した部品が選ばれる", selected.size() == 1 && selected.front() == id);
}

[[nodiscard]] bool Near(double a, double b)
{
    return std::abs(a - b) < 1.0e-3;
}

//! 上から見た画面に、箱を 1 つ作って選ぶところまで。
[[nodiscard]] EntityId ArmBox(V2MainWindow& window, Box3& before)
{
    window.RunCommand("file.new");
    const EntityId box = MakeBox(window, 0.30, 0.40, 0.45, 0.60);
    if (box.IsNil()) {
        return EntityId{};
    }
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.FitToDocument();
    viewport.SetVisibleWidthMm(viewport.VisibleWidthMm() * 2.5);   // 動かす先も画面に入れる
    const auto bounds = BoundsOf(window, box);
    if (!bounds.has_value() || !PressPart(window, box)) {
        return EntityId{};
    }
    before = *bounds;
    return box;
}

//! HP-PL-01。部品の移動: 2 点で示した分だけ動き、元は隠れ、1 回で戻り、開き直しても同じ場所。
[[nodiscard]] bool CaseMovePartByTwoPoints(V2MainWindow& window)
{
    Box3 before;
    const EntityId box = ArmBox(window, before);
    if (box.IsNil()) {
        return false;
    }
    auto& viewport = window.Viewport();
    window.RunCommand("part.move");
    viewport.SetSnapSuppressed(true);   // 部品の角へ吸い付かせない(動かす量をきっちり見る)
    viewport.ClickAt(QPointF(viewport.width() * 0.50, viewport.height() * 0.50));
    viewport.ClickAt(QPointF(viewport.width() * 0.65, viewport.height() * 0.50));
    viewport.SetSnapSuppressed(false);
    const auto visible = VisibleParts(window);
    if (!Explain((std::string("動かすと見える部品は 1 つ(") + window.StatusText().toStdString() + ")")
                     .c_str(),
            visible.size() == 1 && !(visible.front() == box))) {
        return false;
    }
    const auto after = BoundsOf(window, visible.front());
    if (!Explain("動かした部品が画面に出ている", after.has_value())) {
        return false;
    }
    const double dx = after->minimum.x - before.minimum.x;
    if (!Explain((std::string("右へ動く(x が ") + std::to_string(dx) + "mm)、y と z と大きさは同じ").c_str(),
            dx > 1.0 && Near(after->maximum.x - after->minimum.x, before.maximum.x - before.minimum.x)
                && Near(after->minimum.y, before.minimum.y) && Near(after->maximum.z, before.maximum.z))) {
        return false;
    }
    window.RunCommand("edit.undo");
    const auto undone = VisibleParts(window);
    if (!Explain("1回の取り消しで元の部品だけが見える", undone.size() == 1 && undone.front() == box)) {
        return false;
    }
    window.RunCommand("edit.redo");
    if (!Explain("保存して開き直せる",
            window.SaveAndReopen(QStringLiteral("kacha_selftest_part_move.kcd2")))) {
        return false;
    }
    const auto reopened = VisibleParts(window);
    const auto again = reopened.size() == 1 ? BoundsOf(window, reopened.front()) : std::nullopt;
    return Explain("開き直しても動かした場所に形がある",
        again.has_value() && Near(again->minimum.x, after->minimum.x)
            && Near(again->maximum.x, after->maximum.x));
}

//! HP-PL-02。部品のミラー: 2 点の線を鏡にして写す。元は残り、写しは線の向こう側に同じ大きさで出る。
[[nodiscard]] bool CaseMirrorPartAcrossLine(V2MainWindow& window)
{
    Box3 before;
    const EntityId box = ArmBox(window, before);
    if (box.IsNil()) {
        return false;
    }
    auto& viewport = window.Viewport();
    // 鏡の線は、箱の右の縁より少し右に縦に引く(画面での箱の位置から決める)。
    const auto edge = viewport.Mapping().Project(Vector3{before.maximum.x,
        (before.minimum.y + before.maximum.y) * 0.5, before.maximum.z});
    if (!Explain("箱の右の縁が画面に写る", edge.has_value())) {
        return false;
    }
    const double lineX = edge->x + viewport.width() * 0.05;
    window.RunCommand("part.mirror");
    viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(lineX, viewport.height() * 0.30));
    viewport.ClickAt(QPointF(lineX, viewport.height() * 0.70));
    viewport.SetSnapSuppressed(false);
    const auto visible = VisibleParts(window);
    if (!Explain((std::string("鏡に写すと見える部品は 2 つ(") + window.StatusText().toStdString() + ")")
                     .c_str(),
            visible.size() == 2)) {
        return false;
    }
    const EntityId copy = visible.front() == box ? visible.back() : visible.front();
    const auto mirrored = BoundsOf(window, copy);
    return Explain("写しは鏡の線の向こう側(右)に同じ大きさで出る",
        mirrored.has_value() && mirrored->minimum.x > before.maximum.x
            && Near(mirrored->maximum.x - mirrored->minimum.x, before.maximum.x - before.minimum.x)
            && Near(mirrored->minimum.y, before.minimum.y));
}

//! HP-PL-03。部品のパターン: 配列の棚で個数 3 を確定すると、見える部品が 3 つになり 1 回で戻る。
[[nodiscard]] bool CasePatternPartsOnShelf(V2MainWindow& window)
{
    Box3 before;
    const EntityId box = ArmBox(window, before);
    if (box.IsNil()) {
        return false;
    }
    window.SetArrayChooser(nullptr);   // 窓の差し替えが残っていれば棚の道を通らない
    window.RunCommand("part.array_linear");
    window.ArrayDock().SetCountTyped(3);
    if (!Explain("配列の棚の確定を押せる", window.ArrayDock().ClickConfirm())) {
        return false;
    }
    if (!Explain((std::string("見える部品が 3 つ(") + window.StatusText().toStdString() + ")").c_str(),
            VisibleParts(window).size() == 3)) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1回の取り消しで元の 1 つに戻る", VisibleParts(window).size() == 1);
}

} // namespace

std::vector<SelfTestCase> PartPlaceCases()
{
    return {
        {"HP-PL-01 部品の移動は2点の分だけ動き元は隠れ1回で戻り開き直しても同じ場所",
            CaseMovePartByTwoPoints},
        {"HP-PL-02 部品のミラーは線の向こうに同じ大きさで写し元は残す", CaseMirrorPartAcrossLine},
        {"HP-PL-03 部品のパターンは配列の棚で個数ぶん並び1回で戻る", CasePatternPartsOnShelf},
    };
}

} // namespace kachakacha::v2::selftest
