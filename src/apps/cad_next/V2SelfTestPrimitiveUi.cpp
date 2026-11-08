#include "V2SelfTestPrimitiveUi.h"
#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include <QPointF>

namespace kachakacha::v2::selftest {
using base::EntityId;
using domain::EntityKind;
//! 画面の割合で指した矩形を引き、その線を拾って押し出し、Enter で部品にする。
[[nodiscard]] EntityId MakeBoxByHand(V2MainWindow& window, double x0, double y0, double x1,
    double y1)
{
    const int before = CountOfKind(window, EntityKind::Part);
    const EntityId wire = DrawRectangleAtByHand(window, x0, y0, x1, y1);
    if (wire.IsNil() || !Explain("線を画面から拾える", ClickOnCurveOf(window, wire))) {
        return EntityId{};
    }
    window.RunCommand("part.extrude");
    if (!Explain("押し出しの下見が出る", window.Viewport().ExtrudeHandleShown())
        || !Explain("Enterで部品にできる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("部品が1つ増える", CountOfKind(window, EntityKind::Part) == before + 1)) {
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

//! その部品の塗りの真ん中を、上から素のクリックで押す。
[[nodiscard]] bool PressPart(V2MainWindow& window, const EntityId& id)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.FitToDocument();
    for (const auto& shape : viewport.ShapeViews()) {
        if (shape.surface || shape.mesh.Empty() || !(shape.entityId == id)) {
            continue;
        }
        const auto center = kachakacha::v2::geometry::Vector3{
            (shape.mesh.minimum.x + shape.mesh.maximum.x) * 0.5,
            (shape.mesh.minimum.y + shape.mesh.maximum.y) * 0.5, shape.mesh.maximum.z};
        const auto screen = viewport.Mapping().Project(center);
        if (!screen.has_value()
            || !Explain("部品の塗りに当たり判定がある",
                viewport.PickShapeAt(QPointF(screen->x, screen->y)).has_value())) {
            return false;
        }
        viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
        return true;
    }
    return Explain("その部品が画面に出ている", false);
}

}
