//! 「面の編集」の人の道(HP-SE、プロンプト additional_surface_tools)。
//!
//! 道具を押す → 3D で面(縁は縁の近くを押す)を押す → 下見 → Enter で確定、
//! 1 回の取り消しで全部消える。Esc は何も作らない。作れないもの(平面を整える)は
//! 理由を言って確定させない。選ぶのは実際に拾う道(`SelectAt`)だけ。

#include "V2SelfTest.h"

#include "V2MainWindow.h"
#include "V2SurfaceEditDock.h"
#include "V2SurfaceEditTool.h"
#include "V2Viewport.h"
#include "V2EntityTree.h"
#include "kachakacha/document/Commands.h"

#include "kachakacha/app/Selection.h"
#include "kachakacha/app/ShelfLayout.h"
#include "kachakacha/app/SurfaceEditInputState.h"
#include "kachakacha/domain/Feature.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctOutput.h"

#include <QPointF>
#include <QString>
#include <QPushButton>
#include <QTreeWidgetItem>
#include <QApplication>
#include <QMouseEvent>
#include <QEvent>
#include <QVariant>

#include <string>
#include <algorithm>

namespace kachakacha::v2::selftest {
namespace {

using kachakacha::v2::app::MirrorPlaneChoice;
using kachakacha::v2::app::Shelf;
using kachakacha::v2::app::SurfaceEditOperation;
using kachakacha::v2::base::EntityId;
using kachakacha::v2::domain::EntityKind;

//! その線から平面の面を作る(線を拾う → 面を作る → Enter)。できた面の番号。
[[nodiscard]] EntityId SurfaceFromRectangle(V2MainWindow& window, const EntityId& rectangle)
{
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    if (!ClickOnCurveOf(window, rectangle)) {
        return EntityId{};
    }
    window.RunCommand("surface.create");
    if (!window.SurfacePreviewShown() || !window.HandleToolKey(Qt::Key_Return, nullptr)) {
        return EntityId{};
    }
    const auto surfaces = kachakacha::v2::app::SelectAllOfKind(
        window.Session().GetDocument().Snapshot(), EntityKind::GuideSurface);
    return surfaces.entityIds.empty() ? EntityId{} : surfaces.entityIds.back();
}

//! その面の塗りを、外接箱の中の割合(fx, fy)の場所で実際に押す。
[[nodiscard]] bool ClickSurfaceAt(V2MainWindow& window, const EntityId& id, double fx, double fy)
{
    auto& viewport = window.Viewport();
    for (const auto& shape : viewport.ShapeViews()) {
        if (shape.entityId != id || shape.mesh.Empty()) {
            continue;
        }
        const kachakacha::v2::geometry::Vector3 point{
            shape.mesh.minimum.x + (shape.mesh.maximum.x - shape.mesh.minimum.x) * fx,
            shape.mesh.minimum.y + (shape.mesh.maximum.y - shape.mesh.minimum.y) * fy,
            (shape.mesh.minimum.z + shape.mesh.maximum.z) * 0.5};
        const auto screen = viewport.Mapping().Project(point);
        if (!screen.has_value()) {
            return false;
        }
        viewport.SelectAt(QPointF(screen->x, screen->y), Qt::NoModifier);
        return true;
    }
    return false;
}

//! HP-SE-01。2 枚の面の向かい合う縁を押して「面をつなぐ」(G1)を下見し、Enter で確定、
//! 1 回の取り消しで消える。
[[nodiscard]] bool CaseSurfaceEditBridge(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId leftWire = DrawRectangleAtByHand(window, 0.18, 0.35, 0.40, 0.65);
    const EntityId rightWire = DrawRectangleAtByHand(window, 0.60, 0.35, 0.82, 0.65);
    if (!Explain("矩形を 2 つ引ける", !leftWire.IsNil() && !rightWire.IsNil())) {
        return false;
    }
    const EntityId left = SurfaceFromRectangle(window, leftWire);
    const EntityId right = SurfaceFromRectangle(window, rightWire);
    if (!Explain("面が 2 枚できる", !left.IsNil() && !right.IsNil()
                && CountOfKind(window, EntityKind::GuideSurface) == 2)) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    window.RunCommand("surface.bridge");
    if (!Explain("面をつなぐを押すと棚が構える", window.ShelfShown(Shelf::SurfaceEdit)
                && window.SurfaceEdit().Input().operation == SurfaceEditOperation::Bridge)) {
        return false;
    }
    if (!Explain("左の面を右の縁の近くで押せる", ClickSurfaceAt(window, left, 0.93, 0.5))
        || !Explain("右の面を左の縁の近くで押せる", ClickSurfaceAt(window, right, 0.07, 0.5))) {
        return false;
    }
    const auto& input = window.SurfaceEdit().Input();
    if (!Explain("縁が 2 本決まる", input.edges.size() == 2 && input.edges[0].edgeIndex >= 0
                && input.edges[1].edgeIndex >= 0)
        || !Explain("両端とも G1 が出ている", window.SurfaceEdit().Dock()->ContinuityRowShown(false)
                && window.SurfaceEdit().Dock()->ContinuityRowShown(true))) {
        return false;
    }
    const std::string refusal = window.SurfaceEdit().Outcome().refusalJa;
    if (!Explain(("つなぐ面の下見が出る(" + refusal + ")").c_str(),
            window.SurfaceEdit().Outcome().available && !window.Viewport().ToolPreview().empty())) {
        return false;
    }
    if (!Explain("Enter で確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("面が 1 枚増える", CountOfKind(window, EntityKind::GuideSurface) == 3)
        || !Explain("棚が引っ込む", !window.ShelfShown(Shelf::SurfaceEdit))) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1 回の取り消しで消える", CountOfKind(window, EntityKind::GuideSurface) == 2);
}

//! HP-SE-02。U/V 線は線として取り出せ、平面を整えるのは理由を言って断り、
//! 対称に写すと面が増え、Esc は何も作らない。
[[nodiscard]] bool CaseSurfaceEditIsoRefitMirror(V2MainWindow& window)
{
    window.RunCommand("file.new");
    const EntityId wire = DrawRectangleAtByHand(window, 0.30, 0.30, 0.70, 0.70);
    const EntityId surface = SurfaceFromRectangle(window, wire);
    if (!Explain("面が 1 枚できる", !surface.IsNil())) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    const int wiresBefore = CountOfKind(window, EntityKind::Wire);
    window.RunCommand("surface.iso_curves");
    if (!Explain("U/V 線の棚が構える", window.ShelfShown(Shelf::SurfaceEdit))
        || !Explain("面を押せる", ClickSurfaceAt(window, surface, 0.5, 0.5))
        || !Explain("両方 3 本ずつを選べる", window.SurfaceEdit().Dock()->ChooseIso(2, 3))) {
        return false;
    }
    if (!Explain(("線 6 本の下見(" + window.SurfaceEdit().Outcome().refusalJa + ")").c_str(),
            window.SurfaceEdit().Outcome().available && window.SurfaceEdit().Outcome().outputs == 6)
        || !Explain("Enter で確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("線が 6 本増える", CountOfKind(window, EntityKind::Wire) == wiresBefore + 6)) {
        return false;
    }
    window.RunCommand("edit.undo");
    if (!Explain("1 回の取り消しで 6 本とも消える", CountOfKind(window, EntityKind::Wire) == wiresBefore)) {
        return false;
    }
    window.RunCommand("surface.refit");
    if (!Explain("整える棚が構える", window.ShelfShown(Shelf::SurfaceEdit))
        || !Explain("面を押せる", ClickSurfaceAt(window, surface, 0.5, 0.5))
        || !Explain("平面は作らない", !window.SurfaceEdit().Outcome().available)
        || !Explain(("理由を言う(" + window.SurfaceEdit().Outcome().refusalJa + ")").c_str(),
            window.SurfaceEdit().Outcome().refusalJa.find("いちばん簡単") != std::string::npos)
        || !Explain("確定のボタンは押せない", !window.SurfaceEdit().Dock()->ConfirmEnabled())
        || !Explain("Esc でやめられる", window.HandleToolKey(Qt::Key_Escape, nullptr))
        || !Explain("何も作っていない", CountOfKind(window, EntityKind::GuideSurface) == 1)) {
        return false;
    }
    window.RunCommand("surface.mirror");
    if (!Explain("面を押せる", ClickSurfaceAt(window, surface, 0.5, 0.5))
        || !Explain("対称面を X = 0 にできる",
            window.SurfaceEdit().Dock()->ChooseMirrorPlane(MirrorPlaneChoice::CenterYZ))
        || !Explain(("写した面の下見(" + window.SurfaceEdit().Outcome().refusalJa + ")").c_str(),
            window.SurfaceEdit().Outcome().available)
        || !Explain("Enter で確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))) {
        return false;
    }
    return Explain("面が 1 枚増え、元の面も残る", CountOfKind(window, EntityKind::GuideSurface) == 2);
}

//! HP-SE-03。隣り合う 2 枚の面で、直す面の縁 → 合わせ先の縁を押して「面を合わせる」(G1)を
//! 下見し、Enter で確定(新しい面が増え、元の面は残る)、1 回の取り消しで消える。
[[nodiscard]] bool CaseSurfaceEditMatch(V2MainWindow& window)
{
    window.RunCommand("file.new");
    // 右の矩形は左の矩形の右の辺から始める(角に吸着して、縁の端がそろう)。
    const EntityId leftWire = DrawRectangleAtByHand(window, 0.18, 0.35, 0.40, 0.65);
    const EntityId rightWire = DrawRectangleAtByHand(window, 0.40, 0.35, 0.62, 0.65);
    if (!Explain("隣り合う矩形を 2 つ引ける", !leftWire.IsNil() && !rightWire.IsNil())) {
        return false;
    }
    const EntityId left = SurfaceFromRectangle(window, leftWire);
    const EntityId right = SurfaceFromRectangle(window, rightWire);
    if (!Explain("面が 2 枚できる", !left.IsNil() && !right.IsNil()
                && CountOfKind(window, EntityKind::GuideSurface) == 2)) {
        return false;
    }
    window.Viewport().SelectAt(QPointF(2.0, 2.0), Qt::NoModifier);
    window.RunCommand("surface.match");
    if (!Explain("面を合わせるを押すと棚が構える", window.ShelfShown(Shelf::SurfaceEdit)
                && window.SurfaceEdit().Input().operation == SurfaceEditOperation::Match)) {
        return false;
    }
    if (!Explain("直す面(左)を右の縁の近くで押せる", ClickSurfaceAt(window, left, 0.90, 0.5))
        || !Explain("合わせ先(右)を左の縁の近くで押せる", ClickSurfaceAt(window, right, 0.10, 0.5))) {
        return false;
    }
    const auto& input = window.SurfaceEdit().Input();
    if (!Explain("縁が 2 本決まる", input.edges.size() == 2 && input.edges[0].edgeIndex >= 0
                && input.edges[1].edgeIndex >= 0)
        || !Explain("合わせ方(G1)の欄が出ている", window.SurfaceEdit().Dock()->ContinuityRowShown(false))) {
        return false;
    }
    const std::string refusal = window.SurfaceEdit().Outcome().refusalJa;
    if (!Explain(("合わせた面の下見が出る(" + refusal + ")").c_str(),
            window.SurfaceEdit().Outcome().available && !window.Viewport().ToolPreview().empty())) {
        return false;
    }
    if (!Explain("Enter で確定できる", window.HandleToolKey(Qt::Key_Return, nullptr))
        || !Explain("面が 1 枚増え、元の面も残る", CountOfKind(window, EntityKind::GuideSurface) == 3)) {
        return false;
    }
    window.RunCommand("edit.undo");
    return Explain("1 回の取り消しで消える", CountOfKind(window, EntityKind::GuideSurface) == 2);
}

} // namespace

bool SurfacePreviewOwnership(V2MainWindow& w)
{
    const auto wire=DrawRectangleAtByHand(w,.22,.35,.46,.65);if(wire.IsNil())return false;
    const auto surface=SurfaceFromRectangle(w,wire);if(surface.IsNil())return false;
    w.SelectTool(modeling::DrawingTool::Select);w.Viewport().SetSelection({});
    const auto baseline=kernel::CachedShapeCount();
    w.ActivateCommand("surface.mirror");if(!ClickSurfaceAt(w,surface,.5,.5))return false;
    for(int i=0;i<6;++i){
        if(!w.SurfaceEdit().Dock()->ChooseMirrorPlane(i%2?MirrorPlaneChoice::CenterYZ:MirrorPlaneChoice::CenterXZ))return false;
        if(!Explain("surface preview replaces its native handle",w.SurfaceEdit().Outcome().available&&kernel::CachedShapeCount()==baseline+1))return false;
    }
    w.SelectTool(modeling::DrawingTool::Select);
    if(!Explain("cancel releases only the preview",kernel::CachedShapeCount()==baseline&&CountOfKind(w,EntityKind::GuideSurface)==1))return false;
    w.Viewport().SetSelection({});w.ActivateCommand("surface.mirror");
    if(!Explain("cancelled surface tool can pick its next source",ClickSurfaceAt(w,surface,.5,.5)))return false;
    const auto handles=w.SurfaceEdit().PreviewSurfaces();if(!Explain("retry has one native preview",handles.size()==1))return false;
    w.SurfaceEdit().Confirm();
    if(!Explain("committing transfers the native surface to the document",CountOfKind(w,EntityKind::GuideSurface)==2
        &&kernel::CaptureOutputShape(handles.front()).HasValue()&&kernel::CachedShapeCount()==baseline+1))return false;
    w.ActivateCommand("edit.undo");if(CountOfKind(w,EntityKind::GuideSurface)!=1)return false;
    w.ActivateCommand("edit.redo");
    return Explain("surface edit still survives history and reopen",w.SaveAndReopen("surface-preview-ownership.kcd2")
        &&CountOfKind(w,EntityKind::GuideSurface)==2&&w.RebuildProblems().isEmpty());
}
bool SurfaceCreationOwnership(V2MainWindow& w)
{
    const auto wire=DrawRectangleAtByHand(w,.22,.35,.46,.65);if(wire.IsNil())return false;
    w.SelectTool(modeling::DrawingTool::Select);const auto baseline=kernel::CachedShapeCount();
    if(!ClickOnCurveOf(w,wire))return false;w.ActivateCommand("surface.create");
    for(int i=0;i<6;++i){
        w.ChooseSurfaceMethod(modeling::GuideSurfaceMethod::PlanarBoundary);
        if(!Explain("surface creation owns only the current preview",w.SurfacePreviewShown()&&kernel::CachedShapeCount()==baseline+1))return false;
    }
    w.SelectTool(modeling::DrawingTool::Select);
    if(!Explain("surface creation cancellation returns to baseline",kernel::CachedShapeCount()==baseline))return false;
    if(!ClickOnCurveOf(w,wire))return false;w.ActivateCommand("surface.create");w.ConfirmSurface();
    if(!Explain("surface creation retains committed native shape",CountOfKind(w,EntityKind::GuideSurface)==1&&kernel::CachedShapeCount()==baseline+1))return false;
    return true;
}
bool SurfaceHistoryOwnership(V2MainWindow& w)
{
    const auto wire=DrawRectangleAtByHand(w,.22,.35,.46,.65);if(wire.IsNil()||SurfaceFromRectangle(w,wire).IsNil())return false;
    const auto count=kernel::CachedShapeCount();
    for(int i=0;i<6;++i){w.ActivateCommand("edit.undo");w.ActivateCommand("edit.redo");
        if(!Explain("Undo/Redo rebuild releases replaced native surfaces",CountOfKind(w,EntityKind::GuideSurface)==1&&kernel::CachedShapeCount()==count&&w.RebuildProblems().isEmpty()))return false;}
    return true;
}


bool SurfaceFolderPreview(V2MainWindow& w){
    w.RunCommand("file.new");const auto wire=DrawRectangleAtByHand(w,.22,.35,.46,.65);if(wire.IsNil())return false;
    const auto surface=SurfaceFromRectangle(w,wire);if(surface.IsNil())return false;
    w.SelectTool(modeling::DrawingTool::Select);w.Viewport().SetSelection({});w.EntityTree()->clearSelection();w.RunCommand("group.create");
    const auto hidden=w.Session().GetDocument().Snapshot().groups.back().id;
    const auto row=[&]{return w.EntityTree()->FindKey("group:"+QString::fromStdString(hidden.ToString()));};
    row()->setCheckState(0,Qt::Unchecked);QApplication::processEvents();
    w.ActivateCommand("surface.mirror");if(!ClickSurfaceAt(w,surface,.5,.5))return false;
    const auto handles=w.SurfaceEdit().PreviewSurfaces();if(handles.size()!=1)return false;const auto count=kernel::CachedShapeCount();
    auto* button=w.findChild<QPushButton*>("explorerSiblingGroup");if(!button)return false;const QPointF point=button->rect().center();
    QMouseEvent press(QEvent::MouseButtonPress,point,button->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,point,button->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(button,&press);QApplication::sendEvent(button,&release);QApplication::processEvents();
    const auto retained=[&]{return w.SurfaceEdit().Active()&&w.SurfaceEdit().Outcome().available&&w.SurfaceEdit().PreviewSurfaces().size()==1&&w.SurfaceEdit().PreviewSurfaces().front().value==handles.front().value&&kernel::CachedShapeCount()==count;};
    if(!Explain("empty folder mouse creation does not recalculate a valid surface preview",retained()))return false;
    w.EntityTree()->DropOnto({w.ItemOfEntity(surface)},row());
    if(!Explain("folder move preserves preview and hides source geometry",retained()&&std::none_of(w.Viewport().ShapeViews().begin(),w.Viewport().ShapeViews().end(),[&](const auto& shape){return shape.entityId==surface;})))return false;
    row()->setCheckState(0,Qt::Checked);QApplication::processEvents();if(!Explain("group visibility keeps the same native preview",retained()))return false;
    auto settings=w.Session().GetDocument().Snapshot();settings.settings.tolerance.interactiveJoinMm=.02;
    const auto changed=w.Session().GetDocument().ResetTo(std::move(settings));
    if(std::any_of(changed.begin(),changed.end(),[](const auto& d){return d.IsError();}))return false;
    w.SurfaceEdit().HandleSelectionChanged();const auto withTolerance=w.SurfaceEdit().PreviewSurfaces();
    if(!Explain("join tolerance alone invalidates the native preview",withTolerance.size()==1&&withTolerance.front().value!=handles.front().value&&w.SurfaceEdit().Outcome().available))return false;
    const auto* entity=w.Session().GetDocument().FindEntity(wire);const auto* feature=w.Session().GetDocument().FindFeature(entity->createdBy);
    auto definition=std::get<domain::CreateWireDefinition>(feature->definition);
    for(auto& segment:definition.segments)segment=geometry::CurveSegment::MakeLine(segment.StartPoint()+geometry::Vector3{3,0,0},segment.EndPoint()+geometry::Vector3{3,0,0}).Value();
    if(!w.Session().GetDocument().Run(document::UpdateFeatureDefinitionCommand(feature->id,definition,feature->inputEntityIds,"source geometry change")).committed)return false;
    w.RebuildKernelShapes();w.SurfaceEdit().HandleSelectionChanged();const auto fresh=w.SurfaceEdit().PreviewSurfaces();
    return Explain("a real native source change still recalculates the preview",fresh.size()==1&&fresh.front().value!=withTolerance.front().value&&w.SurfaceEdit().Outcome().available&&w.RebuildProblems().isEmpty());
}

std::vector<SelfTestCase> SurfaceEditCases()
{
    return {
        {"HP-SE-01 面を2枚作り、向かい合う縁を押して G1 でつなぎ、取り消しで消える",
            CaseSurfaceEditBridge},
        {"HP-SE-02 U/V 線を取り出し、平面を整えるのは断り、対称に写すと面が増える",
            CaseSurfaceEditIsoRefitMirror},
        {"HP-SE-03 隣り合う面の縁を押して G1 で合わせ、元の面は残り、取り消しで消える",
            CaseSurfaceEditMatch},
        {"HP-SE-04 surface preview ownership survives replace cancel and commit",SurfacePreviewOwnership},
        {"HP-SE-05 surface creation preview ownership",SurfaceCreationOwnership},
        {"HP-SE-06 surface history rebuild ownership",SurfaceHistoryOwnership},
        {"HP-SE-07 folder metadata preserves preview but geometry invalidates it",SurfaceFolderPreview},
    };
}

} // namespace kachakacha::v2::selftest
