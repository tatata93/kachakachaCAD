//! 編集の棚(V1 の「選択内容の数値編集」)のケース。
//!
//! 直線の点を打ち直す、長さと平面内角度で終点を置き直す、円の半径を変える、
//! 作業平面の原点を動かす、原点面は断る。欄と定義の往復は core(app/EntityEdit)。
//! ここでは「棚を通って文書が変わり、一度で戻せるか」を見る。

#include "V2SelfTest.h"

#include "V2DrawingDock.h"
#include "V2EditDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2WorkPlaneDock.h"

#include "kachakacha/app/DirectWireEntry.h"
#include "kachakacha/app/EntityEdit.h"
#include "kachakacha/app/Selection.h"

#include <QString>

#include <cmath>
#include <string>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::WireEditShape;
using kachakacha::v2::domain::EntityKind;
using kachakacha::v2::geometry::Vector3;

//! 数値で線を1本作り、それだけを選ぶ。作った線の ID を返す。
[[nodiscard]] bool MakeAndSelectWire(V2MainWindow& window,
    kachakacha::v2::app::DirectWireKind kind, std::vector<Vector3> points, double radiusMm,
    const QString& name)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    kachakacha::v2::app::DirectWireRequest request;
    request.kind = kind;
    request.points = std::move(points);
    request.radiusMm = radiusMm;
    window.DrawingDock().SetDirectWire(request, name);
    const int before = CountOfKind(window, EntityKind::Wire);
    window.DrawingDock().PressCreateWire();
    if (!Explain((std::string("線が作れる(") + window.StatusText().toStdString() + ")").c_str(),
            CountOfKind(window, EntityKind::Wire) == before + 1)) {
        return false;
    }
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(window.Session().Scene().curves.back().entityId);
    viewport.SetSelection(one);
    return true;
}

[[nodiscard]] const kachakacha::v2::geometry::CurveSegment* SelectedCurve(V2MainWindow& window)
{
    const auto& selection = window.Viewport().Selection();
    if (selection.entityIds.size() != 1) {
        return nullptr;
    }
    for (const auto& curve : window.Session().Scene().curves) {
        if (curve.entityId == selection.entityIds.front()) {
            return &curve.segment;
        }
    }
    return nullptr;
}

[[nodiscard]] bool CaseEditDockRewritesLinePoints(V2MainWindow& window)
{
    // 直線を選んで「数値で編集」→ 点2つの欄が出る → 終点を打ち直す → 線が変わる。
    if (!MakeAndSelectWire(window, kachakacha::v2::app::DirectWireKind::PlanarLine,
            {Vector3{0.0, 0.0, 0.0}, Vector3{40.0, 0.0, 0.0}}, 0.0, QStringLiteral("横"))) {
        return false;
    }
    auto& dock = window.EditDock();
    window.RunCommand("edit.numeric");
    if (!Explain((std::string("直線の欄が出る(") + dock.SelectionText().toStdString() + ")").c_str(),
            dock.IsShowingWire() && dock.PointRowCount() == 2
                && dock.SelectionText().contains(QStringLiteral("直線")))) {
        return false;
    }
    if (!Explain((std::string("長さの初期値が 40(") + std::to_string(dock.LengthValue()) + ")").c_str(),
            std::abs(dock.LengthValue() - 40.0) < 1.0e-6)) {
        return false;
    }
    const auto revision = window.Session().GetDocument().Revision();
    dock.SetPoint(1, Vector3{0.0, 30.0, 0.0});
    dock.PressApply();
    const auto* curve = SelectedCurve(window);
    if (!Explain((std::string("終点が (0,30,0) になる(") + window.StatusText().toStdString()
                     + ")").c_str(),
            curve != nullptr && std::abs(curve->EndPoint().y - 30.0) < 1.0e-9
                && std::abs(curve->EndPoint().x) < 1.0e-9)) {
        return false;
    }
    if (!Explain("直したあとも選んだまま", window.Viewport().Selection().entityIds.size() == 1)) {
        return false;
    }
    // 長さと角度で置き直す。始点は動かない。
    dock.SetLength(50.0);
    dock.SetAngle(0.0);
    dock.PressApply();
    curve = SelectedCurve(window);
    if (!Explain((std::string("長さ 50・角度 0° で終点が (50,0,0)(")
                     + window.StatusText().toStdString() + ")").c_str(),
            curve != nullptr && std::abs(curve->EndPoint().x - 50.0) < 1.0e-9
                && std::abs(curve->EndPoint().y) < 1.0e-9
                && std::abs(curve->StartPoint().x) < 1.0e-9)) {
        return false;
    }
    // 長さ 0 は断る。文書は変わらない。
    const auto beforeRefusal = window.Session().GetDocument().Revision();
    dock.SetLength(0.0);
    dock.PressApply();
    if (!Explain((std::string("長さ 0 は断る(") + dock.MessageText().toStdString() + ")").c_str(),
            window.Session().GetDocument().Revision() == beforeRefusal
                && !dock.MessageText().isEmpty())) {
        return false;
    }
    // 補助線にする。印は Entity に付く。
    dock.SetLength(50.0);
    dock.SetConstruction(true);
    dock.PressApply();
    const auto* entity = window.Session().GetDocument().FindEntity(
        window.Viewport().Selection().entityIds.front());
    if (!Explain("補助線の印が付く", entity != nullptr && entity->construction)) {
        return false;
    }
    // 2回戻すと最初の直線に戻る(数値編集は1回1手)。
    window.RunCommand("edit.undo");
    window.RunCommand("edit.undo");
    window.RunCommand("edit.undo");
    (void)revision;
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(window.Session().Scene().curves.back().entityId);
    window.Viewport().SetSelection(one);
    curve = SelectedCurve(window);
    return Explain("3回戻すと終点が (40,0,0) に戻る",
        curve != nullptr && std::abs(curve->EndPoint().x - 40.0) < 1.0e-9
            && std::abs(curve->EndPoint().y) < 1.0e-9);
}

[[nodiscard]] bool CaseEditDockChangesCircleRadius(V2MainWindow& window)
{
    // 円を選ぶと中心・軸・半径の欄。半径を 20 にすると円のまま半径が変わる。
    if (!MakeAndSelectWire(window, kachakacha::v2::app::DirectWireKind::PlanarCircle,
            {Vector3{10.0, 10.0, 0.0}}, 8.0, QStringLiteral("穴"))) {
        return false;
    }
    auto& dock = window.EditDock();
    window.RunCommand("edit.numeric");
    if (!Explain((std::string("円の欄が出る(") + dock.SelectionText().toStdString() + ")").c_str(),
            dock.IsShowingWire() && dock.SelectionText().contains(QStringLiteral("円")))) {
        return false;
    }
    dock.SetRadius(20.0);
    dock.PressApply();
    const auto* curve = SelectedCurve(window);
    if (!Explain((std::string("半径 20 の円になる(") + window.StatusText().toStdString() + ")").c_str(),
            curve != nullptr && curve->Kind() == kachakacha::v2::geometry::CurveKind::Circle
                && std::abs(curve->Radius() - 20.0) < 1.0e-9
                && std::abs(curve->Center().x - 10.0) < 1.0e-9)) {
        return false;
    }
    const auto before = window.Session().GetDocument().Revision();
    dock.SetRadius(0.0);
    dock.PressApply();
    return Explain((std::string("半径 0 は断る(") + dock.MessageText().toStdString() + ")").c_str(),
        window.Session().GetDocument().Revision() == before && !dock.MessageText().isEmpty());
}

[[nodiscard]] bool CaseEditDockMovesWorkPlaneAndRefusesOrigin(V2MainWindow& window)
{
    // 作業平面を作って選ぶと原点・法線・平面内Xの欄。原点を z=25 にすると平面が動く。
    window.SetWorkPlaneChooser({});
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    window.RunCommand("workplane.create");
    V2WorkPlaneDock* planeDock = window.WorkPlaneDock();
    if (!Explain("作業平面の棚がある", planeDock != nullptr)) {
        return false;
    }
    WorkPlaneChoice choice;
    choice.method = kachakacha::v2::modeling::WorkPlaneMethod::PointNormal;
    choice.name = "台";
    choice.origin = Vector3{0.0, 0.0, 10.0};
    choice.normal = Vector3{0.0, 0.0, 1.0};
    choice.uAxis = Vector3{1.0, 0.0, 0.0};
    planeDock->SetChoice(choice);
    const int before = CountOfKind(window, EntityKind::WorkPlane);
    planeDock->PressCreate();
    if (!Explain("平面が増える", CountOfKind(window, EntityKind::WorkPlane) == before + 1)) {
        return false;
    }
    kachakacha::v2::base::EntityId planeId;
    kachakacha::v2::base::EntityId originId;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::WorkPlane && entity.displayName == "台") {
            planeId = entity.id;
        }
        if (entity.kind == EntityKind::WorkPlane && entity.displayName == "top_XY") {
            originId = entity.id;
        }
    }
    kachakacha::v2::app::SelectionSet one;
    one.entityIds.push_back(planeId);
    window.Viewport().SetSelection(one);
    auto& dock = window.EditDock();
    window.RunCommand("edit.numeric");
    if (!Explain((std::string("平面の欄が出る(") + dock.SelectionText().toStdString() + ")").c_str(),
            dock.IsShowingPlane() && dock.SelectionText().contains(QStringLiteral("台")))) {
        return false;
    }
    dock.SetPlaneOrigin(Vector3{0.0, 0.0, 25.0});
    dock.PressApply();
    // 作った平面は作業中になっているので、画面の作図面も付いてくる。
    const auto& plane = window.Viewport().WorkPlane();
    if (!Explain((std::string("平面が z=25 へ動く(") + std::to_string(plane.origin.z) + ", "
                     + window.StatusText().toStdString() + ")").c_str(),
            std::abs(plane.origin.z - 25.0) < 1.0e-9)) {
        return false;
    }
    // 原点面は断る(UI-E002)。
    kachakacha::v2::app::SelectionSet origin;
    origin.entityIds.push_back(originId);
    window.Viewport().SetSelection(origin);
    if (!Explain("原点面の欄に断りが出る", dock.MessageText().contains(QStringLiteral("UI-E002")))) {
        return false;
    }
    const auto revision = window.Session().GetDocument().Revision();
    dock.SetPlaneOrigin(Vector3{0.0, 0.0, 5.0});
    dock.PressApply();
    if (!Explain((std::string("原点面は変わらない(") + window.StatusText().toStdString() + ")").c_str(),
            window.Session().GetDocument().Revision() == revision
                && window.StatusText().contains(QStringLiteral("UI-E002")))) {
        return false;
    }
    // 何も選んでいなければ、何を選ぶかを言う。
    window.Viewport().SetSelection(kachakacha::v2::app::SelectionSet{});
    return Explain("選んでいないと案内が出る", !dock.IsShowingPlane() && !dock.IsShowingWire());
}

} // namespace

std::vector<SelfTestCase> EditCases()
{
    return {
        {"編集の棚で直線の点と長さ・角度を直せる", &CaseEditDockRewritesLinePoints},
        {"編集の棚で円の半径を変えられる", &CaseEditDockChangesCircleRadius},
        {"編集の棚で作業平面を動かし原点面は断る", &CaseEditDockMovesWorkPlaneAndRefusesOrigin},
    };
}

} // namespace kachakacha::v2::selftest
