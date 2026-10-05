#include <QApplication>
#include <QMouseEvent>
#include <QDir>
#include <QPixmap>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QEvent>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QWidget>
//! 足す・引くの人の道(HP-BO)。引継ぎ 2026-09-17 の 4。
//!
//! 足す/引くを押す → 土台待ち → 3D で部品を押すと土台に入り、自動で相手待ちへ →
//! 相手を押す → 実際に足し引きした稜線が下見に出る → Enter で確定、1回の取り消しで消える。
//! 土台・相手は 3D の札と棚の欄に別々に出て、「ここへ選ぶ」で選び直せる。
//!
//! 選ぶのは実際に拾う道(`SelectAt`)だけ。ID の注入も、見えない widget を叩くこともしない。

#include "V2SelfTest.h"

#include "V2BooleanDock.h"
#include "V2ExtrudeDock.h"
#include "kachakacha/kernel/OcctContact.h"
#include "kachakacha/kernel/OcctExtrude.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"

#include "kachakacha/app/BooleanInputState.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/modeling/ToolController.h"
#include "kachakacha/domain/Feature.h"

#include <QPointF>
#include <QString>

#include <cstdint>
#include <cmath>
#include <string>
#include <variant>
#include <vector>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::BooleanSlot;
using kachakacha::v2::app::Shelf;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;

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

//! 重なる箱を2つ作り、選択を空にして「足す」を構えるところまで。
[[nodiscard]] bool ArmUnionOnTwoBoxes(V2MainWindow& window, EntityId* first, EntityId* second)
{
    window.RunCommand("file.new");
    *first = MakeBoxByHand(window, 0.25, 0.35, 0.45, 0.65);
    *second = MakeBoxByHand(window, 0.40, 0.35, 0.60, 0.65);   // 少し重なる
    if (first->IsNil() || second->IsNil()) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);   // 空所を押して選択を外す
    window.RunCommand("part.boolean_add");
    return Explain("足すを押すと棚が構える",
               window.BooleanShelfShown() && window.ShelfShown(Shelf::Boolean))
        && Explain("構えただけでは部品は増えない", CountOfKind(window, EntityKind::Part) == 2)
        && Explain("土台待ちが見えている",
            window.BooleanDock().ActiveSlotShown() == BooleanSlot::Target
                && window.ToolFooterTextJa().contains(QStringLiteral("NEXT=TARGET")));
}

//! HP-BO-01。土台 → 相手へ自動で移り、札と欄に別々に出て、押し直すと外れる。
[[nodiscard]] bool CaseHumanPathBooleanSlotsFollowClicks(V2MainWindow& window)
{
    EntityId first;
    EntityId second;
    if (!ArmUnionOnTwoBoxes(window, &first, &second)
        || !Explain("土台を画面で押せる", PressPart(window, first))) {
        return false;
    }
    auto& dock = window.BooleanDock();
    if (!Explain("押したものが土台の欄に入る", window.BooleanInput().target == first)
        || !Explain("自動で相手待ちへ移る", dock.ActiveSlotShown() == BooleanSlot::Tool)
        || !Explain("一番下の一行が NEXT=TOOL",
            window.ToolFooterTextJa().contains(QStringLiteral("NEXT=TOOL")))) {
        return false;
    }
    bool sawTarget = false;
    for (const auto& label : window.Viewport().ToolRoleLabels()) {
        sawTarget = sawTarget || label.text == QStringLiteral("TARGET");
    }
    if (!Explain("3D に TARGET の札が出る", sawTarget)
        || !Explain("相手を画面で押せる", PressPart(window, second))
        || !Explain("相手の欄に入る", window.BooleanInput().tools.size() == 1
                && window.BooleanInput().tools.front() == second)) {
        return false;
    }
    bool sawTool = false;
    for (const auto& label : window.Viewport().ToolRoleLabels()) {
        sawTool = sawTool || label.text == QStringLiteral("TOOL");
    }
    const std::string footer = window.ToolFooterTextJa().toStdString();
    if (!Explain("3D に TOOL の札も出る", sawTool)
        || !Explain((std::string("一番下の一行に両方が出る(") + footer + ")").c_str(),
            footer.find("TARGET=") != std::string::npos
                && footer.find("TOOL=") != std::string::npos)
        || !Explain("欄には名前が別々に出る",
            dock.TargetTextJa() != QStringLiteral("(選んでいません)")
                && dock.ToolTextJa() != QStringLiteral("(選んでいません)")
                && dock.TargetTextJa() != dock.ToolTextJa())) {
        return false;
    }
    // 土台をもう一度押すと外れ、土台の欄が次のクリックを待つ。
    if (!Explain("土台をもう一度押せる", PressPart(window, first))
        || !Explain("押し直すと土台だけ外れる",
            window.BooleanInput().target.IsNil() && window.BooleanInput().tools.size() == 1
                && window.BooleanInput().tools.front() == second)
        || !Explain("土台待ちへ戻る", dock.ActiveSlotShown() == BooleanSlot::Target)) {
        return false;
    }
    // 「ここへ選ぶ」で相手を選び直す。見えているボタンを押す。
    if (!Explain("相手の「ここへ選ぶ」を押せる", dock.ClickActivate(BooleanSlot::Tool))
        || !Explain("次のクリックは相手へ", dock.ActiveSlotShown() == BooleanSlot::Tool)
        || !Explain("別の部品を押せる", PressPart(window, first))
        || !Explain("相手に足される(相手は何個でも)", window.BooleanInput().tools.size() == 2
                && window.BooleanInput().tools.back() == first)) {
        return false;
    }
    if (!Explain("Escでやめられる", window.HandleToolKey(Qt::Key_Escape, nullptr))) {
        return false;
    }
    return Explain("取消後も道具を維持する", window.BooleanShelfShown())
        && Explain("部品は2つのまま", CountOfKind(window, EntityKind::Part) == 2)
        && Explain("札も一行も消える",
            window.Viewport().ToolRoleLabels().empty() && window.ToolFooterTextJa().isEmpty());
}

//! HP-BO-02。両方入ると実際に足した稜線が下見に出て、Enter で 1回で戻せる部品になる。
[[nodiscard]] bool CaseHumanPathBooleanPreviewThenConfirm(V2MainWindow& window)
{
    EntityId first;
    EntityId second;
    if (!ArmUnionOnTwoBoxes(window, &first, &second)
        || !Explain("土台を画面で押せる", PressPart(window, first))
        || !Explain("下見はまだ出ない", window.Viewport().ToolPreview().empty())
        || !Explain("相手を画面で押せる", PressPart(window, second))) {
        return false;
    }
    auto& dock = window.BooleanDock();
    const std::string status = dock.StatusTextJa().toStdString();
    if (!Explain((std::string("実際に足した結果が状態に出る(") + status + ")").c_str(),
            status.find("生成可能") != std::string::npos
                && status.find("mm3") != std::string::npos)
        || !Explain("下見が 3D に出ている", !window.Viewport().ToolPreview().empty())
        || !Explain("下見の間、部品は増えない", CountOfKind(window, EntityKind::Part) == 2)
        || !Explain("一番下の一行は Preview only",
            window.ToolFooterTextJa().contains(QStringLiteral("Preview only")))) {
        return false;
    }
    const std::uint64_t before = window.Session().GetDocument().Revision();
    if (!Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("足した部品が1つできる(元の2つは隠れる)",
            CountOfKind(window, EntityKind::Part) == 3)
        || !Explain("確定すると構えが解ける", !window.BooleanShelfShown())
        || !Explain("文書が変わっている", window.Session().GetDocument().Revision() != before)) {
        return false;
    }
    int visibleParts = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Part
            && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
            ++visibleParts;
        }
    }
    if (!Explain("見えている部品は足した1つだけ", visibleParts == 1)) {
        return false;
    }
    window.RunCommand("edit.undo");
    visibleParts = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Part
            && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
            ++visibleParts;
        }
    }
    return Explain("1回の取り消しで元の2つに戻る(隠したのも戻る)",
        CountOfKind(window, EntityKind::Part) == 2 && visibleParts == 2);
}

//! 見えている部品の数。
[[nodiscard]] int VisiblePartCount(V2MainWindow& window)
{
    int count = 0;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Part
            && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
            ++count;
        }
    }
    return count;
}

//! HP-BO-03。交差(P-17): 重なる 2 つの箱の共通部分だけを残す部品になり、1 回で戻り、
//! 保存して開き直しても作り直せる(文書には交差として残る)。
[[nodiscard]] bool CaseHumanPathIntersectKeepsOnlyTheOverlap(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId first = MakeBoxByHand(window, 0.25, 0.35, 0.45, 0.65);
    const EntityId second = MakeBoxByHand(window, 0.40, 0.35, 0.60, 0.65);   // 少し重なる
    if (first.IsNil() || second.IsNil()) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);   // 空所を押して選択を外す
    window.RunCommand("part.boolean_intersect");
    auto& dock = window.BooleanDock();
    if (!Explain("交差を押すと棚が構える", window.BooleanShelfShown())
        || !Explain("棚の作り方は交差", dock.KindShown() == kachakacha::v2::app::BooleanKind::Intersect)
        || !Explain("土台を画面で押せる", PressPart(window, first))
        || !Explain("相手を画面で押せる", PressPart(window, second))) {
        return false;
    }
    const std::string status = dock.StatusTextJa().toStdString();
    if (!Explain((std::string("交差した結果が状態に出る(") + status + ")").c_str(),
            status.find("生成可能") != std::string::npos)
        || !Explain("一番下の一行は交差",
            window.ToolFooterTextJa().startsWith(QStringLiteral("交差")))
        || !Explain("Enterで確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("交差の部品が1つできる(元の2つは隠れる)",
            CountOfKind(window, EntityKind::Part) == 3 && VisiblePartCount(window) == 1)) {
        return false;
    }
    bool recorded = false;
    for (const auto& feature : window.Session().GetDocument().Snapshot().features) {
        if (const auto* boolean =
                std::get_if<kachakacha::v2::domain::BooleanDefinition>(&feature.definition)) {
            recorded = recorded || boolean->mode == 2;
        }
    }
    if (!Explain("文書には交差(mode 2)として残る", recorded)) {
        return false;
    }
    window.RunCommand("edit.undo");
    if (!Explain("1回の取り消しで元の2つに戻る",
            CountOfKind(window, EntityKind::Part) == 2 && VisiblePartCount(window) == 2)) {
        return false;
    }
    window.RunCommand("edit.redo");
    if (!Explain("やり直すと交差の部品に戻る", VisiblePartCount(window) == 1)
        || !Explain("保存して開き直せる",
            window.SaveAndReopen(QStringLiteral("kacha_selftest_intersect.kcd2")))) {
        return false;
    }
    // 開き直したあと、見えている部品(交差の結果)に形がある = 交差として作り直せた。
    kachakacha::v2::app::SelectionSet visible;
    for (const auto& entity : window.Session().GetDocument().Snapshot().entities) {
        if (entity.kind == EntityKind::Part
            && entity.visibility == kachakacha::v2::domain::Visibility::Visible) {
            visible.entityIds.push_back(entity.id);
        }
    }
    window.Viewport().SetSelection(visible);
    return Explain("開き直しても交差の部品は形があり、書き出せる",
        visible.entityIds.size() == 1 && window.CanExportSelectedParts());
}

[[nodiscard]] EntityId PolygonPart(V2MainWindow& window,
    const std::vector<kachakacha::v2::geometry::Vector3>& points, const char* name)
{
    using namespace kachakacha::v2;
    std::vector<geometry::CurveSegment> curves;
    for(std::size_t i=0;i<points.size();++i) curves.push_back(geometry::CurveSegment::MakeLine(points[i],points[(i+1)%points.size()]).Value());
    const auto wire=window.Session().AddWire(curves,false,name);
    if(!wire.committed || !window.SaveAndReopen(QStringLiteral("contact-fixture.kcd2")))return {};
    app::SelectionSet selection;selection.entityIds=wire.createdEntityIds;window.Viewport().SetSelection(selection);
    window.SetMode(app::UiMode::Part);window.RunCommand("part.extrude");
    auto& dock=window.ExtrudeDock();(void)dock.PickDirection(modeling::ExtrudeDirectionMode::WorldZ);
    dock.TypeDistanceMm(4);dock.PressConfirm();
    EntityId result;
    for(const auto& e:window.Session().GetDocument().Snapshot().entities)if(e.kind==EntityKind::Part)result=e.id;
    return result;
}

[[nodiscard]] bool ContactFixture(V2MainWindow& window,EntityId& a,EntityId& b)
{
    window.RunCommand("file.new");
    a=PolygonPart(window,{{0,0,0},{10,0,0},{10,10,0},{7,10,0},{7,3,0},{3,3,0},{3,10,0},{0,10,0}},"U字");
    b=PolygonPart(window,{{-1,6,0},{11,6,0},{11,8,0},{-1,8,0}},"横棒");
    return !a.IsNil()&&!b.IsNil()&&VisiblePartCount(window)==2;
}

[[nodiscard]] bool CaseContactWireOnly(V2MainWindow& window)
{
    EntityId a,b;if(!ContactFixture(window,a,b))return false;
    const int wires=CountOfKind(window,EntityKind::Wire);
    window.RunCommand("part.contact_wire");
    kachakacha::v2::app::SelectionSet selected;selected.entityIds={a,b};window.Viewport().SetSelection(selected);
    if(!Explain("接触境界を下見できる",!window.Viewport().ToolPreview().empty()))return false;
    if(!Explain("ワイヤーだけ確定できる",window.BooleanDock().ClickConfirm()))return false;
    if(!Explain("元の2部品を残しワイヤーだけ増える",VisiblePartCount(window)==2 && CountOfKind(window,EntityKind::Wire)>wires))return false;
    if(!window.SaveAndReopen(QStringLiteral("contact-wire.kcd2")))return false;
    for(const auto& curve:window.Session().Scene().curves) {
        if(curve.segment.Kind()!=kachakacha::v2::geometry::CurveKind::Line)return Explain("箱の境界は直線を保つ",false);
    }
    return true;
}

[[nodiscard]] bool CaseContactOverviewAndLocalTrim(V2MainWindow& window)
{
    using namespace kachakacha::v2;
    EntityId a,b;if(!ContactFixture(window,a,b))return false;
    window.Viewport().SetSelection({});window.RunCommand("part.overlap_inspect");
    auto* browser=window.findChild<QWidget*>(QStringLiteral("overlapBrowser"));
    if(browser==nullptr || browser->isWindow())return Explain("右ペインで処理し専用ウインドウを開かない",false);
    auto* list=browser->findChild<QListWidget*>(QStringLiteral("overlapList"));
    auto* wire=browser->findChild<QPushButton*>(QStringLiteral("overlapWire"));
    auto* first=browser->findChild<QPushButton*>(QStringLiteral("overlapSideA"));
    auto* second=browser->findChild<QPushButton*>(QStringLiteral("overlapSideB"));
    auto* confirm=browser->findChild<QPushButton*>(QStringLiteral("overlapConfirm"));
    if(!list || !wire || !first || !second || !confirm)return false;
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<30000) {
        QApplication::processEvents(QEventLoop::AllEvents,20);
        if(list->count()>0)list->setCurrentRow(0);
        if(wire->isEnabled())break;
    }
    if(!Explain("通常3Dビューで全領域を表示",list->count()==2 && wire->isEnabled()
        && window.Viewport().findChild<QWidget*>(QStringLiteral("overlapOverlay"))))return false;
    if(!Explain("側を選ぶ前には確定できない",!confirm->isEnabled()))return false;
    window.Viewport().SetViewDirection(ViewDirection::Top);window.Viewport().FitToDocument();
    const auto click=[&](geometry::Vector3 point) {
        const auto s=window.Viewport().Mapping().Project(point);if(!s)return false;
        const QPointF local(s->x,s->y);
        QMouseEvent event(QEvent::MouseButtonPress,local,local,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&window.Viewport(),&event);return true;
    };
    list->setCurrentRow(-1);
    if(!click({1.5,7,4}) || list->currentRow()!=0)return Explain("3Dでめり込み部分を選択",false);
    auto* mode=browser->findChild<QComboBox*>(QStringLiteral("overlapDisposition"));
    if(!mode)return false;mode->setCurrentIndex(1);
    if(!click({-0.5,7,4}) || !confirm->isEnabled())return Explain("3Dで横棒を残す側に選択",false);
    list->setCurrentRow(1);mode->setCurrentIndex(0);second->click();
    QApplication::processEvents();
    window.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-overlap-main-view.png"));
    if(!confirm->isEnabled())return false;
    window.SendKeyToViewport(Qt::Key_Return);QApplication::processEvents();
    bool sideA=false,sideB=false;
    for(const auto& f:window.Session().GetDocument().Snapshot().features) {
        const auto* def=std::get_if<domain::BooleanDefinition>(&f.definition);
        if(def!=nullptr&&def->mode==3) {
            if(def->contactRemovals!=std::vector<int>{1,2})return false;
            sideA=sideA||def->contactSide==0;sideB=sideB||def->contactSide==1;
        }
    }
    if(!Explain("双方の領域指定が保存される",sideA&&sideB))return false;
    const int visible=VisiblePartCount(window);
    window.RunCommand("edit.undo");
    if(!Explain("1回のUndoで両部品とも戻る",VisiblePartCount(window)==2&&CountOfKind(window,EntityKind::Part)==2))return false;
    window.RunCommand("edit.redo");
    if(!window.SaveAndReopen(QStringLiteral("contact-local-trim.kcd2")))return false;
    if(!Explain("領域指定から再生成できる",VisiblePartCount(window)==visible))return false;
    int solids=0;
    for(const auto& view:window.Viewport().ShapeViews())if(!view.surface){if(!view.mesh.closed)return false;++solids;}
    return Explain("再生成後も各部品が閉じた立体",solids==visible);
}

[[nodiscard]] bool CaseContactViewportCancelAndWire(V2MainWindow& window)
{
    EntityId a,b;if(!ContactFixture(window,a,b))return false;
    const int wires=CountOfKind(window,EntityKind::Wire);
    window.Viewport().SetSelection({});window.RunCommand("part.overlap_inspect");
    window.SendKeyToViewport(Qt::Key_Escape);QApplication::processEvents();
    if(!Explain("取消で文書と部品を維持",VisiblePartCount(window)==2
        && CountOfKind(window,EntityKind::Wire)==wires
        && window.Viewport().findChild<QWidget*>(QStringLiteral("overlapOverlay"))))return false;
    auto* browser=window.findChild<QWidget*>(QStringLiteral("overlapBrowser"));
    if(!browser)return false;
    auto* list=browser->findChild<QListWidget*>(QStringLiteral("overlapList"));
    auto* wire=browser->findChild<QPushButton*>(QStringLiteral("overlapWire"));
    if(!list || !wire)return false;
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<30000) {
        QApplication::processEvents(QEventLoop::AllEvents,20);
        if(list->count()>0)list->setCurrentRow(0);
        if(wire->isEnabled())break;
    }
    if(!Explain("取消後も一覧からワイヤー生成へ進める",wire->isEnabled()))return false;
    wire->click();QApplication::processEvents();
    if(!Explain("主画面の操作で交差ワイヤーのみ生成",CountOfKind(window,EntityKind::Wire)>wires
        && VisiblePartCount(window)==2))return false;
    window.RunCommand("edit.undo");
    return Explain("ワイヤー生成も1回で戻る",CountOfKind(window,EntityKind::Wire)==wires);
}

[[nodiscard]] kachakacha::v2::modeling::KernelShapeHandle ContactPrism(
    const std::vector<kachakacha::v2::geometry::CurveSegment>& curves,double distance)
{
    using namespace kachakacha::v2;
    modeling::ExtrudeRequest request;request.profiles.push_back({curves,true,{},{}});
    request.directionMode=modeling::ExtrudeDirectionMode::WorldZ;request.distanceMm=distance;request.outputs.part=true;
    geometry::GeometryTolerance tolerance;
    const auto analysis=modeling::AnalyzeExtrudeRequest(request,tolerance);if(!analysis.HasValue())return {};
    const auto built=kernel::BuildExtrude(request,analysis.Value(),tolerance);
    return built.HasValue() ? built.Value().parts.front().handle : modeling::KernelShapeHandle{};
}
[[nodiscard]] std::vector<kachakacha::v2::geometry::CurveSegment> ContactRectangle(double x0,double y0,double x1,double y1,double z)
{
    using namespace kachakacha::v2::geometry;
    const std::vector<Vector3> points{{x0,y0,z},{x1,y0,z},{x1,y1,z},{x0,y1,z}};
    std::vector<CurveSegment> result;
    for(int i=0;i<4;++i)result.push_back(CurveSegment::MakeLine(points[i],points[(i+1)%4]).Value());
    return result;
}
[[nodiscard]] bool CaseContactTouchAndCircle(V2MainWindow&)
{
    using namespace kachakacha::v2;
    const auto a=ContactPrism(ContactRectangle(0,0,10,10,0),4);
    const auto b=ContactPrism(ContactRectangle(10,2,15,8,0),4);
    const auto touch=kernel::BuildContact(a,b,true,false,false,1e-6);
    if(!Explain("面接触は境界ワイヤーを作れる",touch.HasValue()&&!touch.Value().wires.empty()))return false;
    if(!Explain("体積ゼロの面接触は削除と区別",!kernel::BuildContact(a,b,false,false,true,1e-6).HasValue()))return false;
    const auto circle=geometry::CurveSegment::MakeCircle({0,0,0},{0,0,1},{1,0,0},5);
    const auto cylinder=ContactPrism({circle.Value()},4);
    const auto cap=ContactPrism(ContactRectangle(-8,-8,8,8,2),4);
    const auto boundary=kernel::BuildContact(cylinder,cap,true,false,false,1e-6);
    if(!Explain("曲面の交線を生成できる",boundary.HasValue()))return false;
    bool curved=false;
    for(const auto& wire:boundary.Value().wires)for(const auto& curve:wire)
        curved=curved||curve.Kind()==geometry::CurveKind::Circle||curve.Kind()==geometry::CurveKind::CircularArc;
    if(!Explain("円の交線を短い直線群にせず保持",curved))return false;
    const std::vector<geometry::Vector3> uPoints{{0,0,0},{10,0,0},{10,10,0},{7,10,0},{7,3,0},{3,3,0},{3,10,0},{0,10,0}};
    std::vector<geometry::CurveSegment> uCurves;
    for(std::size_t i=0;i<uPoints.size();++i)uCurves.push_back(geometry::CurveSegment::MakeLine(uPoints[i],uPoints[(i+1)%uPoints.size()]).Value());
    const auto u=ContactPrism(uCurves,4),bar=ContactPrism(ContactRectangle(-1,6,11,8,0),4);
    const auto local=kernel::BuildLocalTrim(u,bar,{1,2},1e-6);
    if(!Explain("異なる領域を双方から別々に削れる",local.HasValue()))return false;
    double aVolume=0,bVolume=0;
    for(const auto& piece:local.Value().pieces){if(piece.sourceSide==0)aVolume+=piece.volumeMm3;else bVolume+=piece.volumeMm3;}
    return Explain("AとBそれぞれから指定した24mm3だけ除去",std::abs(aVolume-264)<1e-5&&std::abs(bVolume-72)<1e-5);
}
[[nodiscard]] bool CaseContactSplitSelection(V2MainWindow& window)
{
    using namespace kachakacha::v2;
    EntityId a,b;if(!ContactFixture(window,a,b))return false;
    app::SelectionSet selected;selected.entityIds={a,b};window.Viewport().SetSelection(selected);
    window.RunCommand("part.split_overlap");
    auto* regions=window.findChild<QListWidget*>(QStringLiteral("contactRegions"));
    if(!Explain("分割後の各連結領域を選べる",regions!=nullptr&&regions->count()>=4))return false;
    for(int i=1;i<regions->count();++i)regions->item(i)->setCheckState(Qt::Unchecked);
    if(!window.BooleanDock().ClickConfirm())return false;
    if(!Explain("選んだ領域だけ生成し相手を残す",VisiblePartCount(window)==2&&CountOfKind(window,EntityKind::Part)==3))return false;
    return window.SaveAndReopen(QStringLiteral("contact-split.kcd2"))&&VisiblePartCount(window)==2;
}

//! 実際のマウスイベントでツールからフェイスを拾い、平面と近似入力へ進める。
[[nodiscard]] bool CaseFaceToolFirst(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const auto part = MakeBoxByHand(window, 0.25, 0.35, 0.55, 0.65);
    if (part.IsNil()) return false;
    auto& viewport = window.Viewport();
    viewport.SetViewDirection(ViewDirection::Top); viewport.FitToDocument();
    geometry::Vector3 center;
    for (const auto& shape : viewport.ShapeViews()) if (shape.entityId == part)
        center = (shape.mesh.minimum + shape.mesh.maximum) * 0.5;
    const auto screen = viewport.Mapping().Project(center);
    if (!screen) return false;
    const QPointF point(screen->x,screen->y);
    const auto click = [&] {
        QMouseEvent press(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&viewport,&press);
        QMouseEvent release(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&viewport,&release); QApplication::processEvents();
    };
    viewport.SetSelection({}); window.RunCommand("workplane.from_face"); click();
    const auto* plane = window.Session().GetDocument().FindEntity(window.ActiveWorkPlaneId());
    if (!Explain("HP-FA フェイスから作業面1を作る", plane && plane->displayName == "作業面1"
            && CountOfKind(window,EntityKind::WorkPlane) == 4)) return false;
    window.SelectTool(modeling::DrawingTool::Select);
    for (const auto command : {"fabrication.from_face", "fabrication.gpt_from_face"}) {
        const int before = CountOfKind(window,EntityKind::GuideSurface);
        viewport.SetViewDirection(ViewDirection::Top);viewport.FitToDocument();
        viewport.SetSelection({});window.RunCommand(command);
        // 作業面生成後はカメラが変わるので投影位置を取り直す。
        const auto at = viewport.Mapping().Project(center);if(!at)return false;
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(at->x,at->y),QPointF(at->x,at->y),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&viewport,&press);QApplication::processEvents();
        if (!Explain("HP-FA 元ソリッドを残して独立面が増える",CountOfKind(window,EntityKind::Part)==1
                && CountOfKind(window,EntityKind::GuideSurface)==before+1))return false;
        if (std::string(command)=="fabrication.from_face") {
            if (!Explain("HP-FA 従来近似の入力を受け付ける",window.ApproxShelfShown()
                    && !window.ApproxInput().sources.empty() && !viewport.ToolPreview().empty())) return false;
        } else {
            auto* preview=window.findChild<QPushButton*>(QStringLiteral("gptFabricationPreview"));
            if (!preview || !preview->isEnabled())return false;
            preview->click();
            if (!Explain("HP-FA GPT近似の面プレビュー",viewport.ToolPreviewFaceCount()>0))return false;
        }
        window.HandleToolKey(Qt::Key_Escape,nullptr);
    }
    for(const auto command:{"fabrication.create","fabrication.gpt_create"}){
        const int before=CountOfKind(window,EntityKind::GuideSurface);
        app::SelectionRef face;face.entityId=part;face.kind=app::SelectionElementKind::Face;face.pickedFaceIndex=0;
        app::SelectionSet source;source.entityIds={part};source.ordered={face};viewport.SetSelection(source);window.RunCommand(command);
        if(!Explain("HP-FA usual approximation honors selected solid face",CountOfKind(window,EntityKind::GuideSurface)==before+1))return false;
        window.HandleToolKey(Qt::Key_Escape,nullptr);
    }
    if(!window.SaveAndReopen(QStringLiteral("face-actions.kcd2")))return false;
    app::SelectionSet selected;
    for(const auto& entity:window.Session().GetDocument().Snapshot().entities)
        if(entity.kind==EntityKind::GuideSurface){selected.entityIds={entity.id};break;}
    viewport.SetSelection(selected);window.RunCommand("fabrication.create");
    return Explain("HP-FA 保存後の独立フェイスも近似できる",!viewport.ToolPreview().empty());
}

} // namespace

std::vector<SelfTestCase> HumanPathBooleanCases()
{
    return {
        {"HP-FA-01 フェイスから作図と従来GPT近似", CaseFaceToolFirst},
        {"HP-CT-05 主画面で取消して再開し交差ワイヤーのみ生成", CaseContactViewportCancelAndWire},
        {"HP-CT-03 面接触と体積重なりを区別し曲面の円交線を保持", CaseContactTouchAndCircle},
        {"HP-CT-04 分割領域を選択して生成し保存再生成", CaseContactSplitSelection},
        {"HP-CT-01 接触境界ワイヤーのみ生成し元の両部品を保持", CaseContactWireOnly},
        {"HP-CT-02 KCDの3Dめり込み一覧から領域別に双方を削り保存再生成", CaseContactOverviewAndLocalTrim},
        {"HP-BO-01 足す引くは土台→相手へ自動で移り、札と欄に別々に出て押し直すと外れる",
            CaseHumanPathBooleanSlotsFollowClicks},
        {"HP-BO-02 両方入ると実際の結果が下見に出て、Enter で 1回で戻せる部品になる",
            CaseHumanPathBooleanPreviewThenConfirm},
        {"HP-BO-03 交差は重なりだけを残す部品になり1回で戻り開き直しても作り直せる",
            CaseHumanPathIntersectKeepsOnlyTheOverlap},
    };
}

} // namespace kachakacha::v2::selftest
