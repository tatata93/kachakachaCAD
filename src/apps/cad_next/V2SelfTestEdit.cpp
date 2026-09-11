//! 編集の棚(V1 の「選択内容の数値編集」)のケース。
//!
//! 直線の点を打ち直す、長さと平面内角度で終点を置き直す、円の半径を変える、
//! 作業平面の原点を動かす、原点面は断る。欄と定義の往復は core(app/EntityEdit)。
//! ここでは「棚を通って文書が変わり、一度で戻せるか」を見る。

#include "V2SelfTest.h"

#include "V2CornerDock.h"
#include "V2DrawingDock.h"
#include "V2EditDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2ParameterDock.h"
#include "V2WorkPlaneDock.h"

#include "kachakacha/app/CommandParameters.h"
#include "kachakacha/app/DirectWireEntry.h"
#include "kachakacha/app/EntityEdit.h"
#include "kachakacha/app/Selection.h"

#include <QPointF>
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

[[nodiscard]] bool CaseUndoRedrawsScene(V2MainWindow& window)
{
    // 元に戻すと、画面の線(場面)も消える。文書だけ戻って線が残っていた。
    window.RunCommand("file.new");
    if (!MakeAndSelectWire(window, kachakacha::v2::app::DirectWireKind::PlanarLine,
            {Vector3{0.0, 0.0, 0.0}, Vector3{40.0, 0.0, 0.0}}, 0.0, QStringLiteral("横"))) {
        return false;
    }
    const std::size_t before = window.Session().Scene().curves.size();
    window.RunCommand("edit.undo");
    if (!Explain((std::string("戻すと画面の線が減る(") + std::to_string(before) + " → "
                     + std::to_string(window.Session().Scene().curves.size()) + ")").c_str(),
            window.Session().Scene().curves.size() + 1 == before
                && CountOfKind(window, EntityKind::Wire) == 0)) {
        return false;
    }
    window.RunCommand("edit.redo");
    return Explain("やり直すと画面の線が戻る",
        window.Session().Scene().curves.size() == before
            && CountOfKind(window, EntityKind::Wire) == 1);
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

[[nodiscard]] bool CaseCornerDockSingleVertex(V2MainWindow& window);

[[nodiscard]] bool CaseCornerDockAsymmetricChamferAndKeepSides(V2MainWindow& window)
{
    // 面取りの棚: A の切戻し 4・B の切戻し 8、A は始点側・B は終点側を残す(V1 の欄と同じ)。
    window.RunCommand("file.new");
    if (!MakeAndSelectWire(window, kachakacha::v2::app::DirectWireKind::PlanarLine,
            {Vector3{-30.0, 0.0, 0.0}, Vector3{30.0, 0.0, 0.0}}, 0.0, QStringLiteral("横"))) {
        return false;
    }
    const auto firstId = window.Session().Scene().curves.back().entityId;
    if (!MakeAndSelectWire(window, kachakacha::v2::app::DirectWireKind::PlanarLine,
            {Vector3{0.0, -30.0, 0.0}, Vector3{0.0, 30.0, 0.0}}, 0.0, QStringLiteral("縦"))) {
        return false;
    }
    const auto secondId = window.Session().Scene().curves.back().entityId;
    kachakacha::v2::app::SelectionSet pair;
    pair.entityIds = {firstId, secondId};
    window.Viewport().SetSelection(pair);
    auto& dock = window.CornerDock();
    if (!Explain((std::string("直線 A/B に選んだ順の名前が出る(") + dock.FirstText().toStdString()
                     + ", " + dock.SecondText().toStdString() + ")").c_str(),
            dock.FirstText() == QStringLiteral("横") && dock.SecondText() == QStringLiteral("縦"))) {
        return false;
    }
    // 量は数の棚と同じ値。数の棚で打つと棚に映る。
    if (!Explain("面取り量を入れられる",
            window.ParameterDock().Apply(kachakacha::v2::app::ParameterId::CornerSize,
                QStringLiteral("4")))) {
        return false;
    }
    if (!Explain((std::string("棚の量が 4 になる(") + std::to_string(dock.Choice().sizeMm) + ")").c_str(),
            std::abs(dock.Choice().sizeMm - 4.0) < 1.0e-9)) {
        return false;
    }
    dock.SetFillet(false);
    dock.SetSecondSetback(8.0);
    dock.SetKeepSides(1, 2);
    dock.PressCreate();
    const auto& curves = window.Session().Scene().curves;
    if (!Explain((std::string("2本が 3本(A'・面取り・B')になる(") + window.StatusText().toStdString()
                     + ", 見える線 " + std::to_string(curves.size()) + ")").c_str(),
            curves.size() == 3)) {
        return false;
    }
    // A' は始点側 (-30,0)〜(-4,0)、B' は (0,8)〜終点側 (0,30)、面取りは sqrt(4^2+8^2)。
    bool aOk = false;
    bool bOk = false;
    bool cOk = false;
    for (const auto& curve : curves) {
        const auto s = curve.segment.StartPoint();
        const auto e = curve.segment.EndPoint();
        if (std::abs(s.x + 30.0) < 1e-6 && std::abs(e.x + 4.0) < 1e-6 && std::abs(e.y) < 1e-6) {
            aOk = true;
        }
        if (std::abs(s.y - 8.0) < 1e-6 && std::abs(s.x) < 1e-6 && std::abs(e.y - 30.0) < 1e-6) {
            bOk = true;
        }
        if (std::abs(curve.segment.TotalLength(1e-9) - std::sqrt(80.0)) < 1e-6) {
            cOk = true;
        }
    }
    if (!Explain("A は始点側が残り B は終点側が残り面取りは非対称", aOk && bOk && cOk)) {
        return false;
    }
    return CaseCornerDockSingleVertex(window);
}

[[nodiscard]] bool DrawUShapeAndSelect(V2MainWindow& window)
{
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetVisibleWidthMm(200.0);
    const double pxPerMm = viewport.width() / 200.0;
    const QPointF center(viewport.width() * 0.5, viewport.height() * 0.5);
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Polyline);
    viewport.ClickAt(QPointF(center.x() - 30.0 * pxPerMm, center.y()));
    viewport.ClickAt(QPointF(center.x(), center.y()));
    viewport.ClickAt(QPointF(center.x(), center.y() - 30.0 * pxPerMm));
    viewport.ClickAt(QPointF(center.x() + 30.0 * pxPerMm, center.y() - 30.0 * pxPerMm));
    viewport.FinishTool();
    window.SelectTool(kachakacha::v2::modeling::DrawingTool::Select);
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::Wire));
    return window.Session().Scene().curves.size() == 3;
}

[[nodiscard]] bool CaseCornerDockSingleVertex(V2MainWindow& window)
{
    // ポリラインの角: 頂点 1 だけ。コの字を引いて、角は 2 つあるが 1 つだけ落ちる。
    window.RunCommand("file.new");
    auto& viewport = window.Viewport();
    auto& dock = window.CornerDock();
    if (!Explain("コの字が 1 本", DrawUShapeAndSelect(window))) {
        return false;
    }
    dock.SetOnlyVertex(true, 1);
    dock.PressCorner();
    if (!Explain((std::string("頂点 1 だけ落ちて 4 本になる(") + window.StatusText().toStdString()
                     + ", 見える線 " + std::to_string(window.Session().Scene().curves.size())
                     + ")").c_str(),
            window.Session().Scene().curves.size() == 4)) {
        return false;
    }
    // 頂点 0 は開いた並びの端なので断る(GEO-E021)。文書は変わらない。
    viewport.SetSelection(kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::Wire));
    const auto revision = window.Session().GetDocument().Revision();
    dock.SetOnlyVertex(true, 0);
    dock.PressCorner();
    return Explain((std::string("頂点 0 は断る(") + window.StatusText().toStdString() + ")").c_str(),
        window.Session().GetDocument().Revision() == revision
            && window.StatusText().contains(QStringLiteral("GEO-E021")));
}

} // namespace

std::vector<SelfTestCase> EditCases()
{
    return {
        {"面取りの棚で非対称の切戻しと残す側と頂点番号が効く", &CaseCornerDockAsymmetricChamferAndKeepSides},
        {"編集の棚で直線の点と長さ・角度を直せる", &CaseEditDockRewritesLinePoints},
        {"戻すと画面の線も消えやり直すと戻る", &CaseUndoRedrawsScene},
        {"編集の棚で円の半径を変えられる", &CaseEditDockChangesCircleRadius},
        {"編集の棚で作業平面を動かし原点面は断る", &CaseEditDockMovesWorkPlaneAndRefusesOrigin},
    };
}

} // namespace kachakacha::v2::selftest
