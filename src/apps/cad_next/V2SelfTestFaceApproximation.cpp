#include "V2SelfTest.h"
#include "V2SelfTestPrimitiveUi.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2EntityTree.h"
#include "V2FabricationDock.h"
#include "V2ShapeLifetime.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/io/AtomicFile.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointF>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <optional>

namespace kachakacha::v2::selftest {
namespace {
using base::EntityId;
using domain::EntityKind;
std::optional<geometry::Vector3> Center(V2MainWindow& w,EntityId id) {
    for(const auto& shape:w.Viewport().ShapeViews())if(shape.entityId==id)
        return (shape.mesh.minimum+shape.mesh.maximum)*.5;
    return std::nullopt;
}
bool Mouse(QWidget& widget,QPointF at,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    if(!widget.isVisible()||!widget.isEnabled()||!widget.rect().contains(at.toPoint()))return false;
    QMouseEvent move(QEvent::MouseMove,at,QPointF(widget.mapToGlobal(at.toPoint())),Qt::NoButton,Qt::NoButton,modifiers);
    QApplication::sendEvent(&widget,&move);
    for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
        QMouseEvent event(type,at,QPointF(widget.mapToGlobal(at.toPoint())),Qt::LeftButton,
            type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,modifiers);
        QApplication::sendEvent(&widget,&event);
    }
    QApplication::processEvents();return true;
}
bool Face(V2MainWindow& w,geometry::Vector3 center,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    QApplication::processEvents();const auto at=w.Viewport().Mapping().Project(center);
    return at && Mouse(w.Viewport(),QPointF(at->x,at->y),modifiers);
}
bool Setup(V2MainWindow& w,geometry::Vector3& a,geometry::Vector3* b=nullptr) {
    w.resize(1600,1000);QApplication::processEvents();
    w.RunCommand("file.new");
    const auto first=MakeBoxByHand(w,.2,.35,.4,.65);if(first.IsNil())return false;
    const auto pa=Center(w,first);if(!pa)return false;a=*pa;
    if(b) {
        const auto second=MakeBoxByHand(w,.6,.35,.8,.65);if(second.IsNil())return false;
        const auto pb=Center(w,second);if(!pb)return false;*b=*pb;
    }
    w.RunCommand("selection.activate");w.Viewport().SetViewDirection(ViewDirection::Top);
    w.Viewport().FitToDocument();QApplication::processEvents();
    return Mouse(w.Viewport(),{2,2}); // Real empty click clears prior input.
}
bool MultiplePreselection(V2MainWindow& w) {
    geometry::Vector3 a,b;if(!Setup(w,a,&b))return false;
    Note(("a="+std::to_string(a.x)+","+std::to_string(a.y)+" b="+std::to_string(b.x)+","+std::to_string(b.y)).c_str());
    if(!Face(w,a))return false;
    Note(("first count="+std::to_string(w.Viewport().Selection().ordered.size())).c_str());
    for(const auto& r:w.Viewport().Selection().ordered)Note(("first "+r.entityId.ToString()+" kind="+std::to_string(int(r.kind))).c_str());
    if(!Face(w,b,Qt::ControlModifier))return false;
    for(const auto& r:w.Viewport().Selection().ordered)Note((r.entityId.ToString()+" kind="+std::to_string(int(r.kind))
        +" face="+(r.pickedFaceIndex?std::to_string(*r.pickedFaceIndex):"none")).c_str());
    if(!Explain("two actual solid faces are selected",w.Viewport().Selection().ordered.size()==2
        &&std::all_of(w.Viewport().Selection().ordered.begin(),w.Viewport().Selection().ordered.end(),
            [](const auto& r){return r.kind==app::SelectionElementKind::Face&&r.pickedFaceIndex.has_value();})))return false;
    w.RunCommand("fabrication.create");
    if(!Explain("normal approximation accepts only both selected faces, not their complete solids",
        CountOfKind(w,EntityKind::GuideSurface)==2 && CountOfKind(w,EntityKind::Part)==2
        &&w.ApproxInput().sources.size()==2 && !w.Viewport().ToolPreview().empty()))return false;
    w.RunCommand("selection.activate");const auto native=kernel::CachedShapeCount();w.RunCommand("edit.undo");
    if(!Explain("both extracted faces undo together without removing either solid",CountOfKind(w,EntityKind::GuideSurface)==0
        &&CountOfKind(w,EntityKind::Part)==2&&kernel::CachedShapeCount()==native-2))return false;
    w.RunCommand("edit.redo");
    return Explain("both native faces redo and reopen without leaked handles",CountOfKind(w,EntityKind::GuideSurface)==2
        &&kernel::CachedShapeCount()==native&&w.SaveAndReopen("two-solid-faces.kcd2")&&w.RebuildProblems().isEmpty());
}
bool ExplicitCtrl(V2MainWindow& w) {
    geometry::Vector3 a;if(!Setup(w,a))return false;
    w.RunCommand("fabrication.from_face");
    if(!Face(w,a,Qt::ControlModifier))return false;
    return Explain("face selection accepts Ctrl-click through the visible viewport",
        CountOfKind(w,EntityKind::GuideSurface)==1 && CountOfKind(w,EntityKind::Part)==1
        &&w.ApproxInput().sources.size()==1 && !w.Viewport().ToolPreview().empty());
}
bool CancelFace(V2MainWindow& w) {
    geometry::Vector3 a;if(!Setup(w,a))return false;
    const auto revision=w.Session().GetDocument().Revision();
    w.RunCommand("fabrication.from_face");
    auto* panel=w.findChild<QWidget*>("faceActionPanel");if(!panel)return false;
    QPushButton* cancel=nullptr;
    for(auto* button:panel->findChildren<QPushButton*>())if(button->isVisible()&&button->text().contains(QStringLiteral("取消")))cancel=button;
    if(!cancel||!Mouse(*cancel,cancel->rect().center())||!Face(w,a))return false;
    return Explain("cancel exits face picking without an extraction on the next ordinary click",
        !w.Viewport().ReferencePickPending()&&CountOfKind(w,EntityKind::GuideSurface)==0
        &&w.Session().GetDocument().Revision()==revision);
}
bool Press(QWidget* widget) {
    if(!widget)return false;
    for(auto* p=widget->parentWidget();p;p=p->parentWidget())if(auto* scroll=dynamic_cast<QScrollArea*>(p))scroll->ensureWidgetVisible(widget);
    QApplication::processEvents();return Mouse(*widget,widget->rect().center());
}
bool Key(QWidget& widget,int key) {
    for(auto type:{QEvent::KeyPress,QEvent::KeyRelease}) {QKeyEvent event(type,key,Qt::NoModifier);QApplication::sendEvent(&widget,&event);}
    QApplication::processEvents();return true;
}
bool Confirm(V2MainWindow& w) {
    for(auto* button:w.findChildren<QAbstractButton*>("panelConfirm"))if(button->isVisible())return Press(button);
    return false;
}
bool HollowFixture(V2MainWindow& w) {
    // Fixture preparation is not the tested selection route. The native part
    // enters through the ordinary file loader; all tested face inputs use Qt events.
    w.resize(1600,1000);QApplication::processEvents();w.RunCommand("file.new");
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::PlanarBoundary;
    for(int i=0;i<3;++i) {
        modeling::GuideChain chain;chain.role=i?modeling::ChainRole::HoleBoundary:modeling::ChainRole::OuterBoundary;
        chain.closed=true;chain.index=i?i:1;
        chain.segments={geometry::CurveSegment::MakeCircle({i?(i==1?-5.:5.):0,0,0},{0,0,1},{1,0,0},i?2:12).Value()};
        request.chains.push_back(chain);
    }
    const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,{});if(!analysis.HasValue())return Explain(analysis.FirstSummaryJa().c_str(),false);
    const auto guide=kernel::BuildGuideSurface(request,analysis.Value(),{});if(!guide.HasValue())return Explain(guide.FirstSummaryJa().c_str(),false);
    v2ui::ShapeListOwner temporary({guide.Value().handle});
    const auto solid=kernel::BuildSurfaceExtrude(guide.Value().handle,{0,0,1},0,2,{});
    if(!solid.HasValue()||solid.Value().parts.size()!=1)return Explain(solid.FirstSummaryJa().c_str(),false);
    temporary.values.push_back(solid.Value().parts.front().handle);
    const auto bytes=kernel::CaptureOutputShape(solid.Value().parts.front().handle);if(!bytes.HasValue())return false;
    io::DocumentFile file;file.snapshot=w.Session().GetDocument().Snapshot();
    domain::Feature feature;feature.id=*base::FeatureId::Parse("a0000000-0000-4000-8000-000000000001");
    feature.type=domain::FeatureType::FreezeDerived;feature.displayName="穴付き円柱";
    domain::FreezeDerivedDefinition definition;definition.frozenBrep=bytes.Value();feature.definition=definition;
    domain::Entity entity;entity.id=*EntityId::Parse("a0000000-0000-4000-8000-000000000002");
    entity.kind=EntityKind::Part;entity.createdBy=feature.id;entity.displayName=feature.displayName;entity.editPolicy=domain::EditPolicy::Frozen;
    feature.outputs.push_back({"part",entity.id,entity.kind});file.snapshot.features.push_back(feature);file.snapshot.entities.push_back(entity);
    const auto archive=io::SaveDocument(file);QTemporaryDir folder;if(!archive.HasValue()||!folder.isValid())return Explain(archive.FirstSummaryJa().c_str(),false);
    const auto path=folder.filePath("native-hole-part.kcd2");
    if(!io::WriteFileAtomically(path.toStdString(),archive.Value()).HasValue())return false;
    return w.OpenDocumentFile(path)||Explain(w.StatusText().toStdString().c_str(),false);
}
bool HoleAndCurve(V2MainWindow& w,bool curved,bool explicitPick) {
    if(!HollowFixture(w))return Explain("native holed part fixture opens",false);
    const auto original=std::get<domain::FreezeDerivedDefinition>(w.Session().GetDocument().Snapshot().features.back().definition).frozenBrep;
    w.RunCommand("selection.activate");w.Viewport().SetViewDirection(curved?ViewDirection::Right:ViewDirection::Top);
    w.Viewport().SetViewCenter({0,0,1});w.Viewport().SetVisibleWidthMm(45);QApplication::processEvents();
    const geometry::Vector3 target=curved?geometry::Vector3{12*std::cos(.4),12*std::sin(.4),1}:geometry::Vector3{0,8,2};
    if(explicitPick)w.RunCommand("fabrication.from_face");
    if(!Face(w,target,explicitPick?Qt::ControlModifier:Qt::NoModifier))return false;
    if(!explicitPick)w.RunCommand("fabrication.create");
    if(!Explain("selected native face alone reaches approximation",w.ApproxShelfShown()&&w.ApproxInput().sources.size()==1
        &&CountOfKind(w,EntityKind::Part)==1&&CountOfKind(w,EntityKind::GuideSurface)==1&&!w.Viewport().ToolPreview().empty()))return false;
    const auto id=w.ApproxInput().sources.front();const auto* entity=w.Session().GetDocument().FindEntity(id);
    const auto* feature=entity?w.Session().GetDocument().FindFeature(entity->createdBy):nullptr;
    const auto* frozen=feature?std::get_if<domain::FreezeDerivedDefinition>(&feature->definition):nullptr;if(!frozen)return false;
    const auto native=kernel::RestoreOutputShape(frozen->frozenBrep);if(!native.HasValue())return false;
    v2ui::ShapeListOwner faceOwner({native.Value()});
    const auto boundary=kernel::FaceBoundaryOf(native.Value(),0,{});
    if(!Explain("native extraction preserves the selected curvature and both real holes",
        curved?(!boundary.HasValue()&&boundary.FirstDiagnostic().code==kernel::kFaceNotPlanar)
            :(boundary.HasValue()&&boundary.Value().holeLoops.size()==2)))return false;
    for(auto* combo:w.findChildren<QComboBox*>("fabricationAutomaticGeneration"))if(combo->isVisible()) {
        combo->setFocus();for(int i=0;i<3&&combo->currentIndex()!=0;++i)Key(*combo,Qt::Key_Up);
    }
    if(!Confirm(w)||!Explain("selected face approximation commits",w.FabricationModelCount()==1))return false;
    if(!Key(w.Viewport(),Qt::Key_Escape))return false;
    w.RunCommand("edit.undo");
    if(!Explain("approximation Undo preserves the source solid and extracted face",w.FabricationModelCount()==0&&CountOfKind(w,EntityKind::Part)==1&&CountOfKind(w,EntityKind::GuideSurface)==1))return false;
    w.RunCommand("edit.redo");
    if(!w.SaveAndReopen(curved?"curved-solid-face.kcd2":"holed-solid-face.kcd2"))return false;
    const auto& snapshot=w.Session().GetDocument().Snapshot();
    const auto originalFeature=std::find_if(snapshot.features.begin(),snapshot.features.end(),[](const auto& f){return f.id.ToString()=="a0000000-0000-4000-8000-000000000001";});
    return Explain("reopen preserves the native solid and face-only fabrication input",w.FabricationModelCount()==1&&w.RebuildProblems().isEmpty()
        &&originalFeature!=snapshot.features.end()&&std::get<domain::FreezeDerivedDefinition>(originalFeature->definition).frozenBrep==original);
}
bool RepeatFace(V2MainWindow& w,bool explicitPick=false) {
    geometry::Vector3 a;if(!Setup(w,a)||!Face(w,a))return false;w.RunCommand("fabrication.create");
    if(!Explain("first surface is extracted",CountOfKind(w,EntityKind::GuideSurface)==1))return false;
    const auto revision=w.Session().GetDocument().Revision();
    if(explicitPick)w.RunCommand("fabrication.from_face");
    if(!Face(w,a,Qt::ControlModifier))return false;
    return Explain("repeated source face toggles its existing input without creating duplicate surfaces",
        CountOfKind(w,EntityKind::GuideSurface)==1&&w.ApproxInput().sources.empty()
        &&w.Session().GetDocument().Revision()==revision);
}
bool MixedInput(V2MainWindow& w) {
    geometry::Vector3 a,b;if(!Setup(w,a,&b)||!Face(w,a))return false;w.RunCommand("fabrication.create");
    const auto prior=w.ApproxInput().sources;
    if(prior.size()!=1||!Face(w,b,Qt::ControlModifier))return false;
    return Explain("adding another solid face retains the earlier independent approximation input",
        CountOfKind(w,EntityKind::GuideSurface)==2&&w.ApproxInput().sources.size()==2
        &&std::find(w.ApproxInput().sources.begin(),w.ApproxInput().sources.end(),prior.front())!=w.ApproxInput().sources.end());
}
bool EscapeOrSwitch(V2MainWindow& w,bool changeTool) {
    geometry::Vector3 a;if(!Setup(w,a))return false;const auto revision=w.Session().GetDocument().Revision();
    w.RunCommand("fabrication.from_face");
    if(changeTool)w.RunCommand("draw.line");else Key(w.Viewport(),Qt::Key_Escape);
    if(!Face(w,a))return false;
    return Explain("Escape and tool switching remove the old face picker",!w.Viewport().ReferencePickPending()
        &&CountOfKind(w,EntityKind::GuideSurface)==0&&w.Session().GetDocument().Revision()==revision);
}
bool WholeTree(V2MainWindow& w) {
    geometry::Vector3 a;if(!Setup(w,a))return false;EntityId part;
    for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.kind==EntityKind::Part)part=e.id;
    w.RunCommand("fabrication.from_face");auto* tree=w.EntityTree();auto* row=w.ItemOfEntity(part);if(!row)return false;
    tree->setFocus();for(auto* p=row->parent();p;p=p->parent())p->setExpanded(true);
    tree->doItemsLayout();tree->scrollToItem(row,QAbstractItemView::PositionAtCenter);QApplication::processEvents();
    row=w.ItemOfEntity(part);if(!row)return false;
    const auto at=tree->visualItemRect(row).intersected(tree->viewport()->rect()).center();
    if(!Mouse(*tree->viewport(),at)||!Explain("a whole solid row never silently chooses face zero",
        w.Viewport().ReferencePickPending()&&CountOfKind(w,EntityKind::GuideSurface)==0))return false;
    return Face(w,a)&&Explain("the real surface can still be picked after a whole tree row is refused",
        !w.Viewport().ReferencePickPending()&&CountOfKind(w,EntityKind::GuideSurface)==1);
}
bool SameSolidFaces(V2MainWindow& w) {
    geometry::Vector3 a;if(!Setup(w,a)||!Face(w,a))return false;
    w.Viewport().SetViewDirection(ViewDirection::Right);QApplication::processEvents();
    if(!Face(w,a,Qt::ControlModifier))return false;
    const auto refs=w.Viewport().Selection().ordered;
    if(!Explain("Ctrl retains two actual native faces of the same solid",refs.size()==2
        &&refs[0].entityId==refs[1].entityId&&refs[0].pickedFaceIndex!=refs[1].pickedFaceIndex))return false;
    w.RunCommand("fabrication.create");
    return Explain("both selected faces, rather than the whole solid, are approximation inputs",
        CountOfKind(w,EntityKind::Part)==1&&CountOfKind(w,EntityKind::GuideSurface)==2
        &&w.ApproxInput().sources.size()==2&&!w.Viewport().ToolPreview().empty());
}
}
std::vector<SelfTestCase> SolidFaceApproximationCases() {
    return {{"HP-SFA multiple preselected solid faces",MultiplePreselection},
        {"HP-SFA explicit Ctrl face input",ExplicitCtrl},
        {"HP-SFA cancel face input",CancelFace},
        {"HP-SFA repeat same source face",[](auto& w){return RepeatFace(w);}},
        {"HP-SFA repeat explicit source face",[](auto& w){return RepeatFace(w,true);}},
        {"HP-SFA mixed independent and solid face inputs",MixedInput},
        {"HP-SFA Escape exits face input",[](auto& w){return EscapeOrSwitch(w,false);}},
        {"HP-SFA another tool exits face input",[](auto& w){return EscapeOrSwitch(w,true);}},
        {"HP-SFA whole tree row never guesses a face",WholeTree},
        {"HP-SFA two native faces of one solid",SameSolidFaces},
        {"HP-SFA two-hole face before approximation",[](auto& w){return HoleAndCurve(w,false,false);}},
        {"HP-SFA two-hole face tool first Ctrl",[](auto& w){return HoleAndCurve(w,false,true);}},
        {"HP-SFA curved solid face before approximation",[](auto& w){return HoleAndCurve(w,true,false);}},
        {"HP-SFA curved solid face tool first Ctrl",[](auto& w){return HoleAndCurve(w,true,true);}}};
}
}
