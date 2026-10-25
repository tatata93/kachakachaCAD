#include "V2SelfTest.h"
#include "V2ProjectionTool.h"
#include "V2ToolSelection.h"
#include "V2ExtrudeDock.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2EntityTree.h"
#include "V2Calculation.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctSurfaceExtrude.h"
#include "kachakacha/modeling/MeshPick.h"
#include "kachakacha/geometry/WireChain.h"
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QLabel>
#include <QListWidget>
#include <QDir>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QPixmap>
#include <QPointF>
#include <QTemporaryDir>
#include <QString>
#include <QWidget>
#include <algorithm>
#include <QTreeWidgetItem>
#include <QBrush>
#include <cstdlib>
namespace kachakacha::v2::selftest {
bool VerifyProjectedLampNative(V2MainWindow& window,double expectedVolume);
namespace {
base::EntityId WireId(){return *base::EntityId::Parse("b0000000-0000-4000-8000-000000000001");}
base::EntityId TargetId(){return *base::EntityId::Parse("b0000000-0000-4000-8000-000000000002");}
std::vector<geometry::CurveSegment> Square(double z,double size=2) {
    const std::vector<geometry::Vector3> p={{-size,-size,z},{size,-size,z},{size,size,z},{-size,size,z},{-size,-size,z}};
    std::vector<geometry::CurveSegment> curves;for(int i=0;i<4;++i)curves.push_back(geometry::CurveSegment::MakeLine(p[i],p[i+1]).Value());return curves;
}
bool Fixture(V2MainWindow& window,bool solid=false,int circle=0) {
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::PlanarBoundary;
    modeling::GuideChain chain;chain.role=modeling::ChainRole::OuterBoundary;chain.closed=true;chain.segments=Square(0,8);request.chains={chain};
    if(circle==2) {
        request.method=modeling::GuideSurfaceMethod::RuledSections;request.chains.clear();
        for(int i=0;i<2;++i) {
            modeling::GuideChain section;section.role=modeling::ChainRole::Section;section.index=i+1;
            section.segments={geometry::CurveSegment::MakeCircularArc({0,0,i?4.0:-4.0},{0,0,1},{1,0,0},10,-.8,1.6).Value()};
            request.chains.push_back(section);
        }
    }
    const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,{});if(!analysis.HasValue())return false;
    const auto surface=kernel::BuildGuideSurface(request,analysis.Value(),{});if(!surface.HasValue())return false;
    auto handle=surface.Value().handle;
    if(solid) {
        const auto built=kernel::BuildSurfaceExtrude(handle,{0,0,1},0,2,{});kernel::ReleaseShape(handle);
        if(!built.HasValue()||built.Value().parts.size()!=1)return false;handle=built.Value().parts.front().handle;
    }
    const auto bytes=kernel::CaptureOutputShape(handle);kernel::ReleaseShape(handle);if(!bytes.HasValue())return false;
    io::DocumentFile file;file.snapshot=window.Session().GetDocument().Snapshot();
    domain::Feature wire;wire.id=*base::FeatureId::Parse("b0000000-0000-4000-8000-000000000101");wire.type=domain::FeatureType::CreateWire;wire.displayName="投影元";
    domain::CreateWireDefinition definition;definition.segments=Square(10);
    if(circle)definition.segments={geometry::CurveSegment::MakeCircle(circle==2?geometry::Vector3{20,0,0}:geometry::Vector3{0,0,10},
        circle==2?geometry::Vector3{1,0,0}:geometry::Vector3{0,0,1},circle==2?geometry::Vector3{0,1,0}:geometry::Vector3{1,0,0},1).Value()};
    for(std::size_t i=1;i<=definition.segments.size();++i)definition.segmentIds.push_back(*base::SegmentId::Parse("b0000000-0000-4000-8000-00000000020"+std::to_string(i)));
    wire.definition=definition;domain::Entity w;w.id=WireId();w.kind=domain::EntityKind::Wire;w.createdBy=wire.id;w.displayName=wire.displayName;wire.outputs.push_back({"wire",w.id,w.kind});
    domain::Feature target;target.id=*base::FeatureId::Parse("b0000000-0000-4000-8000-000000000102");target.type=domain::FeatureType::FreezeDerived;target.displayName="投影先";
    domain::FreezeDerivedDefinition frozen;frozen.frozenBrep=bytes.Value();target.definition=frozen;domain::Entity t;
    t.id=TargetId();t.kind=solid?domain::EntityKind::Part:domain::EntityKind::GuideSurface;t.createdBy=target.id;t.editPolicy=domain::EditPolicy::Frozen;t.displayName=target.displayName;target.outputs.push_back({"shape",t.id,t.kind});
    file.snapshot.features.push_back(wire);file.snapshot.features.push_back(target);file.snapshot.entities.push_back(w);file.snapshot.entities.push_back(t);
    const auto saved=io::SaveDocument(file);QTemporaryDir folder;if(!saved.HasValue())return false;
    const auto path=folder.filePath("projection.kcd2");return io::WriteFileAtomically(path.toStdString(),saved.Value()).HasValue()&&window.OpenDocumentFile(path);
}
bool AddGuideInputs(V2ProjectionTool& tool) {
    app::SelectionRef wire;wire.entityId=WireId();app::SelectionRef face;face.entityId=TargetId();return tool.AddWire(wire)&&tool.AddTarget(face);
}
bool ToolFirst(V2MainWindow& window) {
    window.Viewport().SetSelection({});window.RunCommand("wire.project_surface");
    auto* tool=dynamic_cast<V2ProjectionTool*>(window.findChild<QWidget*>("projectionPanel"));if(!Explain("PROJ tool starts without preselection",tool!=nullptr))return false;
    auto* mode=tool->findChild<QComboBox*>("projectionMode");mode->setCurrentIndex(2);
    const auto revision=window.Session().GetDocument().Revision();tool->CancelInput();
    return Explain("PROJ Esc preserves tool and mode",tool->isVisible()&&mode->currentIndex()==2&&revision==window.Session().GetDocument().Revision());
}
bool SelectionDeleteKeepsProjectionInputs(V2MainWindow& window) {
    if(!Fixture(window))return false;
    const auto added=window.Session().AddWire({geometry::CurveSegment::MakeLine({20,0,0},{24,0,0}).Value()},false,"unrelated");
    if(!added.committed)return false;
    app::SelectionSet selected;selected.entityIds={WireId(),TargetId()};window.Viewport().SetSelection(selected);
    auto* tool=V2ProjectionTool::Open(window);if(!tool||tool->WireCount()!=1||tool->TargetCount()!=1)return false;
    auto& view=window.Viewport();V2ToolSelection::Begin(view);
    if(!Explain("role inputs do not leak into scratch deletion",view.Selection().entityIds.empty()))return false;
    app::SelectionSet scratch;scratch.entityIds={added.createdEntityIds.front()};view.SetSelection(scratch);
    window.RunCommand("edit.delete");V2ToolSelection::End(view);
    if(!Explain("projection retains inputs after unrelated delete",window.Session().GetDocument().FindEntity(WireId())
        &&window.Session().GetDocument().FindEntity(TargetId())&&tool->WireCount()==1&&tool->TargetCount()==1&&tool->Preview()))return false;
    window.RunCommand("edit.undo");
    if(!Explain("projection inputs remain usable after Undo",tool->Preview()
        &&window.Session().GetDocument().FindEntity(added.createdEntityIds.front())))return false;
    window.RunCommand("edit.redo");
    if(!Explain("projection inputs remain usable after Redo",tool->Preview()
        &&!window.Session().GetDocument().FindEntity(added.createdEntityIds.front())))return false;
    for(int i=0;i<3;++i) {
        V2ToolSelection::Begin(view);V2ToolSelection::End(view);
        if(!Explain("repeated selection detours retain filled projection roles",tool->WireCount()==1
            &&tool->TargetCount()==1&&tool->Preview()))return false;
    }
    window.RunCommand("part.sweep");
    if(!Explain("switching a filled projection clears old role highlights",!view.RoleColorOf(WireId())
        &&!view.RoleColorOf(TargetId())))return false;
    window.RunCommand("selection.activate");
    selected.entityIds={WireId(),TargetId()};view.SetSelection(selected);tool=V2ProjectionTool::Open(window);
    return Explain("projection can be reacquired after another tool",tool&&tool->WireCount()==1
        &&tool->TargetCount()==1&&tool->Preview());
}
bool GuideCommit(V2MainWindow& window) {
    if(!Fixture(window))return false;window.RunCommand("wire.project_surface");auto* tool=dynamic_cast<V2ProjectionTool*>(window.findChild<QWidget*>("projectionPanel"));if(!tool||!AddGuideInputs(*tool))return false;
    const auto before=window.Session().GetDocument().Snapshot();const auto revision=window.Session().GetDocument().Revision();if(!Explain("PROJ native preview",tool->Preview()))return false;
    if(!Explain("PROJ preview is document immutable",window.Session().GetDocument().Revision()==revision))return false;
    if(!Explain("PROJ commit uses displayed preview",tool->Commit()))return false;
    const auto after=window.Session().GetDocument().Snapshot();if(!Explain("PROJ output added with inputs unchanged",after.features.size()==before.features.size()+1&&window.Session().GetDocument().FindEntity(WireId())&&window.Session().GetDocument().FindEntity(TargetId())))return false;
    io::DocumentFile file;file.snapshot=after;const auto bytes=io::SaveDocument(file);QTemporaryDir folder;
    if(!bytes.HasValue()||!io::WriteFileAtomically(folder.filePath("result.kcd2").toStdString(),bytes.Value()).HasValue())return false;
    window.RunCommand("edit.undo");if(!Explain("PROJ single undo",window.Session().GetDocument().Snapshot().features.size()==before.features.size()))return false;
    return Explain("PROJ save reopen",window.OpenDocumentFile(folder.filePath("result.kcd2"))&&window.Session().Scene().curves.size()>=8);
}
bool Preselection(V2MainWindow& window) {
    if(!Fixture(window))return false;app::SelectionSet selected;selected.entityIds={WireId(),TargetId()};window.Viewport().SetSelection(selected);
    window.RunCommand("wire.project_surface");auto* tool=dynamic_cast<V2ProjectionTool*>(window.findChild<QWidget*>("projectionPanel"));
    return Explain("PROJ preselection imported to separate inputs",tool&&tool->WireCount()==1&&tool->TargetCount()==1&&tool->Preview());
}
bool SolidFaceClick(V2MainWindow& window) {
    window.resize(1500,950);QApplication::processEvents();
    if(!Fixture(window,true))return false;auto& viewport=window.Viewport();viewport.SetViewDirection(ViewDirection::Top);viewport.SetViewCenter({0,0,0});viewport.SetVisibleWidthMm(40);
    window.RunCommand("wire.project_surface");auto* tool=dynamic_cast<V2ProjectionTool*>(window.findChild<QWidget*>("projectionPanel"));if(!tool)return false;
    app::SelectionRef wire;wire.entityId=WireId();tool->AddWire(wire);
    for(auto* button:tool->findChildren<QPushButton*>())if(button->text().startsWith(QStringLiteral("②")))button->click();
    QApplication::processEvents(); // Let the projection panel finish layout before choosing a visible model pixel.
    const auto screen=viewport.Mapping().Project({0,0,2});if(!screen)return false;
    QMouseEvent mouse(QEvent::MouseButtonPress,QPointF(screen->x,screen->y),QPointF(screen->x,screen->y),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&viewport,&mouse);
    if(!Explain("PROJ tool-first solid face picked inside viewport",tool->TargetCount()==1&&tool->Preview()))return false;
    if(const auto* path=std::getenv("KACHACAD_PROJECTION_SCREENSHOT")){viewport.SetViewDirection(ViewDirection::Isometric);QApplication::processEvents();window.grab().save(QString::fromLocal8Bit(path));}
    return Explain("PROJ solid face commit",tool->Commit());
}
bool WrapClosestAndFraction(V2MainWindow& window) {
    if(!Fixture(window))return false;window.RunCommand("wire.wrap_project");auto* tool=dynamic_cast<V2ProjectionTool*>(window.findChild<QWidget*>("projectionPanel"));if(!tool||!AddGuideInputs(*tool))return false;
    auto* mode=tool->findChild<QComboBox*>("projectionMode");if(!Explain("PROJ wrap preview",mode->currentIndex()==1&&tool->Preview()))return false;
    auto* tolerance=tool->findChild<QDoubleSpinBox*>("projectionTolerance");auto* entry=tolerance->findChild<QLineEdit*>();entry->setText(QStringLiteral("0.0025 mm"));tolerance->interpretText();
    mode->setCurrentIndex(2);if(!Explain("PROJ closest and decimal input",std::abs(tolerance->value()-0.0025)<1e-10&&tool->Preview()))return false;
    tool->CancelInput();return Explain("PROJ cancel then reuse",mode->currentIndex()==2&&AddGuideInputs(*tool)&&tool->Preview());
}
bool ManualDirectionPreserved(V2MainWindow& window) {
    if(!Fixture(window))return false;auto* tool=V2ProjectionTool::Open(window);
    app::SelectionRef wire;wire.entityId=WireId();tool->AddWire(wire);
    QPushButton* reverse=nullptr;for(auto* b:tool->findChildren<QPushButton*>())if(b->text()==QStringLiteral("方向を反転"))reverse=b;
    if(!reverse)return false;reverse->click();app::SelectionRef face;face.entityId=TargetId();tool->AddTarget(face);
    if(!Explain("PROJ manual direction is not automatically reversed",!tool->Preview()))return false;
    reverse->click();return Explain("PROJ one-button reverse restores projection",tool->Preview());
}
bool RoleMarks(V2MainWindow& window) {
    if(!Fixture(window,true))return false;
    auto* row=window.ItemOfEntity(WireId());auto* category=row->parent();category->setExpanded(false);
    const auto name=row->text(0);const auto snapshot=window.Session().GetDocument().Snapshot();
    auto* tool=V2ProjectionTool::Open(window);app::SelectionRef w;w.entityId=WireId();tool->AddWire(w);
    app::SelectionRef t;t.entityId=TargetId();t.pickedFaceIndex=0;tool->AddTarget(t);
    const auto source=window.Viewport().RoleColorOf(WireId());
    if(!Explain("PROJ source marked in viewport and collapsed tree",source&&row->text(0).contains(QStringLiteral("元 1"))&&!category->isExpanded()))return false;
    if(!Explain("PROJ target marks only chosen native face",window.Viewport().FaceRoleColorOf(TargetId(),0)
        &&!window.Viewport().FaceRoleColorOf(TargetId(),1)))return false;
    tool->CancelInput();
    return Explain("PROJ cancel restores tree and transient colors without document changes",
        row->text(0)==name&&!category->isExpanded()&&!window.Viewport().RoleColorOf(WireId())
        &&!window.Viewport().FaceRoleColorOf(TargetId(),0)&&window.Viewport().ToolRoleLabels().empty()
        &&window.Session().GetDocument().Snapshot().revision==snapshot.revision);
}
bool TreeInput(V2MainWindow& window) {
    if(!Fixture(window))return false;auto* tool=V2ProjectionTool::Open(window);
    auto* source=tool->findChild<QPushButton*>("projectionPickWire");auto* target=tool->findChild<QPushButton*>("projectionPickTarget");
    if(!source->isChecked()||target->isChecked())return false;
    window.SelectTreeRowForEntity(WireId());window.EntityTree()->itemClicked(window.ItemOfEntity(WireId()),0);
    if(!Explain("PROJ tree click directly adds source",tool->WireCount()==1))return false;
    target->click();window.SelectTreeRowForEntity(TargetId());window.EntityTree()->itemClicked(window.ItemOfEntity(TargetId()),0);
    if(!Explain("PROJ active slot and tree target",target->isChecked()&&!source->isChecked()&&tool->TargetCount()==1&&tool->Preview()))return false;
    window.RunCommand("selection.activate");
    return Explain("PROJ tool switch removes all input marks",window.Viewport().ToolRoleLabels().empty()
        &&!window.Viewport().RoleColorOf(WireId())&&!window.Viewport().FaceRoleColorOf(TargetId(),0)
        &&window.ItemOfEntity(WireId())->text(0)==QStringLiteral("投影元"));
}
bool SegmentMarks(V2MainWindow& window) {
    if(!Fixture(window))return false;const auto name=window.ItemOfEntity(WireId())->text(0);
    auto* tool=V2ProjectionTool::Open(window);app::SelectionRef ref;ref.entityId=WireId();
    ref.segmentId=*base::SegmentId::Parse("b0000000-0000-4000-8000-000000000201");tool->AddWire(ref);
    ref.segmentId=*base::SegmentId::Parse("b0000000-0000-4000-8000-000000000202");tool->AddWire(ref);
    if(!Explain("PROJ only explicit source segments are colored",!window.Viewport().RoleColorOf(WireId())
        &&window.Viewport().SegmentRoleColorOf(WireId(),*base::SegmentId::Parse("b0000000-0000-4000-8000-000000000201"))
        &&!window.Viewport().SegmentRoleColorOf(WireId(),*base::SegmentId::Parse("b0000000-0000-4000-8000-000000000203"))))return false;
    tool->CancelInput();return Explain("PROJ multiple segment labels restore original editable name",
        window.ItemOfEntity(WireId())->text(0)==name&&(window.ItemOfEntity(WireId())->flags()&Qt::ItemIsEditable));
}
bool RegionOutput(V2MainWindow& window) {
    if(!Fixture(window,true))return false;auto* tool=V2ProjectionTool::Open(window);
    app::SelectionRef source;source.entityId=WireId();tool->AddWire(source);
    app::SelectionRef face;face.entityId=TargetId();face.pickedFaceIndex=5;tool->AddTarget(face);
    // Face index 5 is the box's top face (confirmed by the existing viewport pick test).
    auto* output=tool->findChild<QComboBox*>("projectionOutput");output->setCurrentIndex(1);
    if(!Explain("PROJ closed region preview contains native face",tool->Preview()&&window.Viewport().ToolPreviewFaceCount()>0))return false;
    const auto count=window.Session().GetDocument().Snapshot().entities.size();
    if(!tool->Commit())return false;const auto& entities=window.Session().GetDocument().Snapshot().entities;
    if(!Explain("PROJ native face and wire committed together",entities.size()==count+2))return false;
    window.RunCommand("edit.undo");return Explain("PROJ region projection one Undo",window.Session().GetDocument().Snapshot().entities.size()==count);
}
bool SourcePlaneDefault(V2MainWindow& window) {
    if(!Fixture(window))return false;
    auto plane=window.Viewport().WorkPlane();plane.normal={1,0,0};plane.uAxis={0,1,0};plane.vAxis={0,0,1};
    window.Viewport().SetWorkPlane(plane);auto* tool=V2ProjectionTool::Open(window);
    return Explain("PROJ source plane supplies direction independently of current drawing plane",AddGuideInputs(*tool)&&tool->Preview());
}
bool ProjectedProfileExtrudes(V2MainWindow& window) {
    if(!Fixture(window))return false;auto* tool=V2ProjectionTool::Open(window);
    if(!AddGuideInputs(*tool)||!tool->Preview()||!tool->Commit())return false;
    const auto& entities=window.Session().GetDocument().Snapshot().entities;
    app::SelectionSet selected;selected.entityIds={entities.back().id};window.Viewport().SetSelection(selected);
    window.ActivateCommand("part.extrude");
    if(!Explain("PROJ derived closed wire remains a usable extrusion profile",window.Viewport().ExtrudeHandleShown()))return false;
    window.ExtrudeDock().TypeDistanceMm(.5);
    if(!window.HandleToolKey(Qt::Key_Return,nullptr)||CountOfKind(window,domain::EntityKind::Part)!=1)return false;
    window.RunCommand("edit.undo");if(CountOfKind(window,domain::EntityKind::Part)!=0)return false;
    window.RunCommand("edit.redo");return Explain("PROJ extrusion persists through Undo and save",window.SaveAndReopen("projected-extrusion.kcd2")&&CountOfKind(window,domain::EntityKind::Part)==1);
}
bool CircleOutput(V2MainWindow& window,bool ordinary,bool curved,int wireOutput=-1,bool insidePick=false) {
    if(!Fixture(window,false,curved?2:1))return false;
    const auto before=window.Session().GetDocument().Snapshot();auto* tool=V2ProjectionTool::Open(window);
    if(!AddGuideInputs(*tool))return false;
    tool->findChild<QComboBox*>("projectionOutput")->setCurrentIndex(curved&&!insidePick?1:0);
    tool->findChild<QDoubleSpinBox*>("projectionTolerance")->setValue(.000001);
    auto* form=tool->findChild<QComboBox*>("projectionWireForm");if(!form||form->count()!=2||form->currentIndex()!=0)return false;
    if(!tool->Preview())return false;
    const auto lines=window.Viewport().ToolPreviewFaceCount();form->setCurrentIndex(ordinary?1:0);
    if(!Explain("PROJ storage choice does not alter preview geometry",window.Viewport().ToolPreviewFaceCount()==lines)||!tool->Commit())return false;
    const auto snapshot=window.Session().GetDocument().Snapshot();
    base::EntityId wire,face;
    for(std::size_t i=before.entities.size();i<snapshot.entities.size();++i) {
        if(snapshot.entities[i].kind==domain::EntityKind::Wire)wire=snapshot.entities[i].id;
        if(snapshot.entities[i].kind==domain::EntityKind::GuideSurface)face=snapshot.entities[i].id;
    }
    const auto* entity=window.Session().GetDocument().FindEntity(wire);if(!entity)return false;
    const auto* feature=window.Session().GetDocument().FindFeature(entity->createdBy);if(!feature)return false;
    if(!Explain("PROJ selected storage and independent dependencies",entity->editPolicy==(ordinary?domain::EditPolicy::Source:domain::EditPolicy::Derived)
        &&feature->type==(ordinary?domain::FeatureType::CreateWire:domain::FeatureType::ProjectWire)
        &&(ordinary?feature->inputEntityIds.empty():feature->inputEntityIds.size()==2)))return false;
    app::SelectionSet selection;selection.entityIds={wire};
    const auto curves=app::SelectedCurves(selection,window.Session().Scene());
    if(!Explain("PROJ circle boundary remains closed",geometry::SegmentsFormClosedLoop(curves,snapshot.settings.tolerance)))return false;
    if(curved&&!insidePick) {
        if(face.IsNil())return false;
        selection.entityIds={face};
    } else if(!curved&&curves.size()!=1)return false;
    window.RunCommand("selection.activate");window.Viewport().SetSelection(selection);window.ActivateCommand("part.extrude");
    if(insidePick) {
        window.Viewport().IsolateSelection();
        window.HandleToolKey(Qt::Key_Escape,nullptr);window.Viewport().SetSelection({});window.ActivateCommand("part.extrude");
        geometry::Vector3 center{};int count=0;
        for(const auto& c:curves)for(int i=0;i<32;++i){center=center+c.Evaluate(i/32.0);++count;}
        center=center*(1.0/count);window.Viewport().SetViewDirection(ViewDirection::Right);window.Viewport().FitToDocument();
        const auto at=window.Viewport().Mapping().Project(center);if(!at)return false;
        window.Viewport().SelectAt(QPointF(at->x,at->y),Qt::NoModifier);
        if(!Explain("PROJ curved wire selected by clicking its interior",std::find(window.Viewport().Selection().entityIds.begin(),
            window.Viewport().Selection().entityIds.end(),wire)!=window.Viewport().Selection().entityIds.end()))return false;
    }
    if(!Explain("PROJ lamp input shows extrusion handle",window.Viewport().ExtrudeHandleShown()))return false;
    window.ExtrudeDock().ChooseDirection(modeling::ExtrudeDirectionMode::CustomXYZ);
    window.ExtrudeDock().SetCustomDirection(curved?geometry::Vector3{1,0,0}:geometry::Vector3{0,0,1});
    if(wireOutput>=0) window.ExtrudeDock().ShowOutputs({wireOutput==1,true,true,true});
    window.ExtrudeDock().TypeDistanceMm(.5);
    const int beforeWires=CountOfKind(window,domain::EntityKind::Wire);
    if(wireOutput>=0) {
        if(!window.HandleToolKey(Qt::Key_Return,nullptr))return false;
        const int madeWires=CountOfKind(window,domain::EntityKind::Wire)-beforeWires;
        if(!Explain("PROJ native selected outputs",CountOfKind(window,domain::EntityKind::Part)==wireOutput&&madeWires>=2))return false;
        window.RunCommand("edit.undo");
        if(CountOfKind(window,domain::EntityKind::Part)!=0||CountOfKind(window,domain::EntityKind::Wire)!=beforeWires)return false;
        window.RunCommand("edit.redo");
        return window.SaveAndReopen("projected-lamp-wires.kcd2")&&CountOfKind(window,domain::EntityKind::Part)==wireOutput
            &&CountOfKind(window,domain::EntityKind::Wire)==beforeWires+madeWires;
    }
    if(!Explain("PROJ actual lamp solid committed",window.HandleToolKey(Qt::Key_Return,nullptr)&&CountOfKind(window,domain::EntityKind::Part)==1))return false;
    if(!Explain("PROJ live native solid has half-mm volume before any rebuild",VerifyProjectedLampNative(window,geometry::kPi*.5)))return false;
    window.RunCommand("edit.undo");if(CountOfKind(window,domain::EntityKind::Part)!=0)return false;
    window.RunCommand("edit.redo");if(CountOfKind(window,domain::EntityKind::Part)!=1)return false;
    if(!window.SaveAndReopen("projected-lamp.kcd2"))return false;
    const auto* restored=window.Session().GetDocument().FindEntity(wire);
    return Explain("PROJ lamp solid and wire policy survive saving",CountOfKind(window,domain::EntityKind::Part)==1
        &&restored&&restored->editPolicy==(ordinary?domain::EditPolicy::Source:domain::EditPolicy::Derived));
}
bool CurvedWindowHole(V2MainWindow& window) {
    if(!Fixture(window,false,2))return false;
    const auto file=window.Session().GetDocument().Snapshot();modeling::KernelShapeHandle support;
    for(const auto& f:file.features)if(const auto* frozen=std::get_if<domain::FreezeDerivedDefinition>(&f.definition)) {
        const auto shape=kernel::RestoreOutputShape(frozen->frozenBrep);if(shape.HasValue())support=shape.Value();
    }
    if(!support.Valid())return false;
    const auto outer=kernel::OutputEdges(support,.000001);kernel::ReleaseShape(support);if(!outer.HasValue())return false;
    auto* tool=V2ProjectionTool::Open(window);if(!AddGuideInputs(*tool)||!tool->Preview()||!tool->Commit())return false;
    const auto windowWire=window.Session().GetDocument().Snapshot().entities.back().id;
    const auto boundary=window.Session().AddWire(outer.Value(),false,"車体外周");if(!boundary.committed)return false;
    window.RunCommand("selection.activate");app::SelectionSet selection;selection.entityIds={boundary.createdEntityIds.front(),windowWire};
    window.Viewport().SetSelection(selection);const int before=CountOfKind(window,domain::EntityKind::GuideSurface);
    window.RunCommand("surface.create");if(!window.HandleToolKey(Qt::Key_Return,nullptr))return false;
    if(!Explain("PROJ curved outer and window wires form a surface with a hole",CountOfKind(window,domain::EntityKind::GuideSurface)==before+1))return false;
    const auto result=window.Session().GetDocument().Snapshot().entities.back().id;
    bool hasHole=true,hasMaterial=false;
    for(const auto& shape:window.Viewport().ShapeViews())if(shape.entityId==result)for(const auto& triangle:shape.mesh.triangles) {
        hasHole=hasHole&&!modeling::RayHitsTriangle({20,0,0},{-1,0,0},triangle).has_value();
        hasMaterial=hasMaterial||modeling::RayHitsTriangle({20,3,0},{-1,0,0},triangle).has_value();
    }
    if(!Explain("PROJ window center is empty while surrounding curved body remains",hasHole&&hasMaterial))return false;
    window.resize(1500,950);selection.entityIds={result};window.Viewport().SetSelection(selection);window.Viewport().IsolateSelection();
    window.Viewport().SetSelection({});window.Viewport().SetViewDirection(ViewDirection::Isometric);
    window.Viewport().SetViewCenter({9,0,0});window.Viewport().SetVisibleWidthMm(22);
    const auto output=qEnvironmentVariable("KACHACAD_SOLID_TRIM_GUIDE_OUTPUT");
    if(!output.isEmpty()){QDir().mkpath(output);QApplication::processEvents();window.grab().save(output+"/curved-window-hole.png");}
    window.RunCommand("edit.undo");if(CountOfKind(window,domain::EntityKind::GuideSurface)!=before)return false;
    window.RunCommand("edit.redo");return window.SaveAndReopen("curved-window-hole.kcd2")&&window.RebuildProblems().isEmpty();
}
bool ExplicitPlanarInterior(V2MainWindow& window) {
    if(!Fixture(window,true,1))return false;
    window.RunCommand("selection.activate");window.Viewport().SetSelection({});
    window.Viewport().SetViewDirection(ViewDirection::Top);window.Viewport().SetViewCenter({0,0,2});window.Viewport().SetVisibleWidthMm(22);
    window.RunCommand("part.extrude");
    auto* pick=window.findChild<QPushButton*>("extrudePickProfile");
    if(!pick||!pick->isVisible())return false;pick->click();
    const auto at=window.Viewport().Mapping().Project({0,0,10});if(!at)return false;
    window.Viewport().SelectAt(QPointF(at->x,at->y),Qt::NoModifier);
    const auto& selected=window.Viewport().Selection().entityIds;
    if(!Explain("PROJ explicit profile role picks planar interior over a solid",selected.size()==1&&selected.front()==WireId()
        &&window.Viewport().ExtrudeHandleShown()))return false;
    window.ExtrudeDock().ChooseDirection(modeling::ExtrudeDirectionMode::WorldZ);window.ExtrudeDock().TypeDistanceMm(.5);
    return Explain("PROJ explicit planar profile creates a separate solid",window.HandleToolKey(Qt::Key_Return,nullptr)
        &&CountOfKind(window,domain::EntityKind::Part)==2);
}
}
std::vector<SelfTestCase> OwnerProjectionCases();
std::vector<SelfTestCase> WindowHoleOwnerCases();
bool PanoramaTwoPlanes(V2MainWindow& window);
std::vector<SelfTestCase> ProjectionCases(){std::vector<SelfTestCase> cases={{"PROJ tool-first and Esc",ToolFirst},{"PROJ preview commit Undo save reopen",GuideCommit},
    {"PROJ preselection",Preselection},{"PROJ solid face viewport click",SolidFaceClick},{"PROJ wrap closest fractional numeric retry",WrapClosestAndFraction},
    {"PROJ manual direction and one-button reverse",ManualDirectionPreserved},{"PROJ role marks preserve tree and document",RoleMarks},
    {"PROJ direct tree inputs and lifecycle",TreeInput},{"PROJ explicit segments and multiple marks",SegmentMarks},
    {"PROJ native region output and Undo",RegionOutput},{"PROJ source-plane direction default",SourcePlaneDefault},
    {"PROJ projected wire extrudes and survives save",ProjectedProfileExtrudes},
    {"PROJ planar circle projected storage extrusion",[](auto& w){return CircleOutput(w,false,false);}},
    {"PROJ planar circle ordinary storage extrusion",[](auto& w){return CircleOutput(w,true,false);}},
    {"PROJ curved lamp projected face extrusion",[](auto& w){return CircleOutput(w,false,true);}},
    {"PROJ curved lamp ordinary storage face extrusion",[](auto& w){return CircleOutput(w,true,true);}}};
    cases.push_back({"HP-UX-PROJ selection delete preserves existing projection roles",SelectionDeleteKeepsProjectionInputs});
    cases.push_back({"PROJ panorama two workplanes per-row directions",PanoramaTwoPlanes});
    cases.push_back({"PROJ curved lamp wires only Undo save",[](auto& w){return CircleOutput(w,true,true,0);}});
    cases.push_back({"PROJ curved lamp wires and solid Undo save",[](auto& w){return CircleOutput(w,true,true,1);}});
    cases.push_back({"PROJ curved ordinary wire interior click extrusion",[](auto& w){return CircleOutput(w,true,true,-1,true);}});
    cases.push_back({"PROJ curved projected wire interior click extrusion",[](auto& w){return CircleOutput(w,false,true,-1,true);}});
    cases.push_back({"PROJ curved body outer wire with window hole Undo save",CurvedWindowHole});
    cases.push_back({"PROJ explicit planar profile interior over a solid",ExplicitPlanarInterior});
    const auto owner=OwnerProjectionCases();cases.insert(cases.end(),owner.begin(),owner.end());
    const auto windows=WindowHoleOwnerCases();cases.insert(cases.end(),windows.begin(),windows.end());return cases;}
}
