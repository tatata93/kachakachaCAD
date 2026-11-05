#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2EntityTree.h"
#include "V2OutputTool.h"
#include "V2ExportDock.h"
#include "V2OperationPanelHost.h"
#include "V2OutputPreview.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/app/ExtrudeOptions.h"
#include "kachakacha/app/ExtrudePlan.h"
#include "kachakacha/app/GroupTree.h"
#include <QCoreApplication>
#include <QEvent>
#include "kachakacha/kernel/OcctTessellate.h"
#include <QString>
#include <QPointF>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QApplication>
#include <QDir>
#include <QPixmap>
#include <algorithm>
#include <QWidget>
#include <QTemporaryDir>
#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QToolButton>
#include <cmath>
#include <QElapsedTimer>
#include <QPointer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QIODevice>
#include <iostream>
namespace kachakacha::v2::selftest {
namespace {
bool DrawOutputRectangle(V2MainWindow& window,double visibleWidth=200) {
    auto& viewport=window.Viewport();viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter({});viewport.SetVisibleWidthMm(visibleWidth);
    window.SelectTool(modeling::DrawingTool::Rectangle);viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(viewport.width()*0.35,viewport.height()*0.35));
    viewport.HoverAt(QPointF(viewport.width()*0.65,viewport.height()*0.65));
    viewport.ClickAt(QPointF(viewport.width()*0.65,viewport.height()*0.65));viewport.SetSnapSuppressed(false);
    window.SelectTool(modeling::DrawingTool::Select);
    viewport.SetSelection(app::SelectAllOfKind(window.Session().GetDocument().Snapshot(),domain::EntityKind::Wire));
    return !viewport.Selection().entityIds.empty();
}
bool OutputBox(V2MainWindow& window){window.RunCommand("file.new");if(!DrawOutputRectangle(window))return false;window.RunCommand("part.extrude");window.RunCommand("part.extrude");return CountOfKind(window,domain::EntityKind::Part)==1;}
struct HiddenOutputFixture {base::EntityId wire,part,visiblePart;base::GroupId parent;};
bool PrepareHiddenOutput(V2MainWindow& w,HiddenOutputFixture& made)
{
    if(!OutputBox(w))return false;
    for(const auto& e:w.Session().GetDocument().Snapshot().entities){
        if(e.kind==domain::EntityKind::Wire)made.wire=e.id;
        if(e.kind==domain::EntityKind::Part)made.part=e.id;
    }
    w.Viewport().SetSelection(app::SelectionSet{{made.part}});
    auto* copy=V2OutputTool::Open(w,3);if(!copy->SaveTo(QString()))return false;copy->close();
    for(const auto& e:w.Session().GetDocument().Snapshot().entities)
        if(e.kind==domain::EntityKind::Part&&e.id!=made.part)made.visiblePart=e.id;
    if(made.visiblePart.IsNil())return false;
    w.EntityTree()->clearSelection();w.Viewport().SetSelection({});w.RunCommand("group.create");
    made.parent=w.Session().GetDocument().Snapshot().groups.back().id;
    auto* parent=w.EntityTree()->FindKey("group:"+QString::fromStdString(made.parent.ToString()));
    if(!parent)return false;parent->setSelected(true);w.RunCommand("group.create");
    const auto child=w.Session().GetDocument().Snapshot().groups.back().id;
    auto* childRow=w.EntityTree()->FindKey("group:"+QString::fromStdString(child.ToString()));
    if(!childRow||child==made.parent)return false;
    w.EntityTree()->DropOnto({w.ItemOfEntity(made.wire),w.ItemOfEntity(made.part)},childRow);
    parent=w.EntityTree()->FindKey("group:"+QString::fromStdString(made.parent.ToString()));
    if(!parent)return false;parent->setCheckState(0,Qt::Unchecked);QApplication::processEvents();
    const auto& snapshot=w.Session().GetDocument().Snapshot();
    const auto* part=w.Session().GetDocument().FindEntity(made.part);
    return part&&part->groupId==child&&part->visibility==domain::Visibility::Visible
        &&!app::EntityEffectivelyVisible(snapshot,*part);
}
bool VisibleOutputHonorsAncestors(V2MainWindow& w)
{
    HiddenOutputFixture made;if(!PrepareHiddenOutput(w,made))return false;
    w.Viewport().SetSelection({});w.SetMode(app::UiMode::Output);
    w.OperationHost().SetShelves({app::Shelf::Export});
    auto& dock=w.ExportDock();
    Note(dock.TargetRowText(0).toUtf8().constData());
    if(!Explain("visible output count includes ancestor visibility",dock.TargetRowText(0).endsWith(QStringLiteral("表示している部品(1)"))))return false;
    if(!dock.ChooseTarget(app::ExportTarget::VisibleParts)||!dock.ChooseFormat(app::ExportFormat::Step))return false;
    QApplication::processEvents();
    // The dock's body is hosted in the current-operation panel.
    bool pressed=false;for(auto* b:w.findChildren<QToolButton*>())
        if(b->isVisible()&&b->isEnabled()&&b->visibleRegion().contains(b->rect().center())
            &&b->text()==QStringLiteral("プレビュー・配置…")){b->click();pressed=true;break;}
    if(!Explain("visible export preview action is reachable",pressed))return false;
    V2OutputTool* output=nullptr;for(auto* dialog:w.findChildren<QDialog*>())
        if(dialog->isVisible())if(auto* candidate=dynamic_cast<V2OutputTool*>(dialog))output=candidate;
    if(!Explain("visible output preview excludes the hidden ancestor's part",output&&output->AssetCount()==1
        &&w.Viewport().Selection().entityIds==std::vector<base::EntityId>{made.visiblePart}))return false;
    output->close();return true;
}
bool ExplicitHiddenOutput(V2MainWindow& w)
{
    HiddenOutputFixture made;if(!PrepareHiddenOutput(w,made))return false;
    w.Viewport().SetSelection(app::SelectionSet{{made.wire,made.part}});
    const auto before=w.Session().GetDocument().Snapshot();auto* output=V2OutputTool::Open(w,2);
    if(!Explain("explicitly selected hidden wire and solid are captured",output->AssetCount()==2))return false;
    QTemporaryDir folder;const auto path=folder.filePath("explicit-hidden.kcd2");
    if(!output->SaveTo(path))return false;output->close();
    const auto bytes=io::ReadWholeFile(path.toStdString());if(!bytes.HasValue())return false;
    const auto file=io::LoadDocument(bytes.Value());
    if(!Explain("hidden selection exports only its independent wire and solid",file.HasValue()&&file.Value().snapshot.entities.size()==2))return false;
    const auto* originalWire=w.Session().GetDocument().FindEntity(made.wire);
    const auto* originalFeature=originalWire?w.Session().GetDocument().FindFeature(originalWire->createdBy):nullptr;
    const auto* originalDefinition=originalFeature?std::get_if<domain::CreateWireDefinition>(&originalFeature->definition):nullptr;
    if(!originalDefinition||originalDefinition->segmentIds.empty())return false;
    app::SelectionRef edge;edge.entityId=made.wire;edge.kind=app::SelectionElementKind::Edge;
    edge.segmentId=originalDefinition->segmentIds.front();app::SelectionSet subset;
    subset.entityIds={made.wire};subset.ordered={edge};w.Viewport().SetSelection(subset);
    auto* partial=V2OutputTool::Open(w,2);auto* scope=partial->findChild<QComboBox*>("outputScope");
    if(!scope)return false;scope->setCurrentIndex(1);
    if(!Explain("an explicitly selected edge in a hidden ancestor is captured",partial->AssetCount()==1))return false;
    const auto edgePath=folder.filePath("explicit-hidden-edge.kcd2");
    if(!partial->SaveTo(edgePath))return false;partial->close();
    const auto edgeBytes=io::ReadWholeFile(edgePath.toStdString());if(!edgeBytes.HasValue())return false;
    const auto edgeFile=io::LoadDocument(edgeBytes.Value());
    if(!edgeFile.HasValue()||edgeFile.Value().snapshot.features.size()!=1)return false;
    const auto* exported=std::get_if<domain::CreateWireDefinition>(&edgeFile.Value().snapshot.features.front().definition);
    if(!Explain("partial output preserves just the selected source segment",exported&&exported->segments.size()==1
        &&exported->segments.front().Kind()==originalDefinition->segments.front().Kind()
        &&geometry::Distance(exported->segments.front().StartPoint(),originalDefinition->segments.front().StartPoint())<1e-9
        &&geometry::Distance(exported->segments.front().EndPoint(),originalDefinition->segments.front().EndPoint())<1e-9))return false;
    const auto* wire=w.Session().GetDocument().FindEntity(made.wire);
    const auto* part=w.Session().GetDocument().FindEntity(made.part);
    if(!Explain("reading hidden inputs does not alter their source visibility or revision",wire&&part
        &&wire->visibility==domain::Visibility::Visible&&part->visibility==domain::Visibility::Visible
        &&w.Session().GetDocument().Snapshot().revision==before.revision
        &&!app::EntityEffectivelyVisible(w.Session().GetDocument().Snapshot(),*wire)))return false;
    if(!w.OpenDocumentFile(path))return false;
    return Explain("independent hidden selection reopens as editable wire and closed solid",CountOfKind(w,domain::EntityKind::Wire)==1
        &&CountOfKind(w,domain::EntityKind::Part)==1&&w.Viewport().ShapeViewCount()==1&&w.Viewport().ShapeViews().front().mesh.closed);
}
app::SelectionSet OutputSelection(V2MainWindow& window,bool wires){app::SelectionSet selection;for(const auto& entity:window.Session().GetDocument().Snapshot().entities)if(entity.kind==domain::EntityKind::Part || (wires&&entity.kind==domain::EntityKind::Wire))selection.entityIds.push_back(entity.id);return selection;}
bool OutputPreviewReuse(V2MainWindow& window) {
    const auto model=qEnvironmentVariable("KACHACAD_OUTPUT_PERF_MODEL");
    if(model.isEmpty()){if(!OutputBox(window))return false;}else if(!window.OpenDocumentFile(model))return false;
    QTemporaryDir folder;if(!folder.isValid())return false;
    window.Viewport().SetSelection(OutputSelection(window,false));
    QElapsedTimer timer;timer.start();auto* output=V2OutputTool::Open(window,1);const double capture=timer.nsecsElapsed()/1e6;
    const auto builds=output->property("outputMeshBuilds").toULongLong();
    timer.restart();output->ChooseFormat(0);output->ChooseFormat(2);output->ChooseFormat(1);const double formats=timer.nsecsElapsed()/1e6;
    if(!Explain("format changes reuse prepared geometry and meshes",builds>0&&output->property("outputMeshBuilds").toULongLong()==builds))return false;
    geometry::OutputPlacement placement;placement.keepPosition=false;placement.destination.origin={10,3,4};
    output->SetPlacement(placement);const auto moved=output->property("outputMeshBuilds").toULongLong();
    if(!Explain("changed placement rebuilds preview once",moved==builds*2))return false;
    timer.restart();if(!output->SaveTo(folder.filePath("reuse.step")))return false;const double step=timer.nsecsElapsed()/1e6;
    output->ChooseFormat(0);timer.restart();if(!output->SaveTo(folder.filePath("reuse.stl")))return false;const double stl=timer.nsecsElapsed()/1e6;
    if(!Explain("saving the exact preview does not retessellate it",output->property("outputMeshBuilds").toULongLong()==moved))return false;
    const auto bytes=io::ReadWholeFile(folder.filePath("reuse.step").toStdString());
    if(!Explain("reused preview still exports complete STEP bytes",bytes.HasValue()&&bytes.Value().find("END-ISO-10303-21;")!=std::string::npos))return false;
    if(!model.isEmpty()) {
        const QJsonObject row{{"capture_ms",capture},{"three_format_changes_ms",formats},{"step_ms",step},{"stl_ms",stl},{"parts",output->AssetCount()},{"mesh_builds",double(moved)}};
        std::cout<<QJsonDocument(row).toJson(QJsonDocument::Compact).constData()<<std::endl;
        const auto path=qEnvironmentVariable("KACHACAD_OUTPUT_PERF_OUTPUT");
        if(!path.isEmpty()){QFile file(path);if(!file.open(QIODevice::WriteOnly))return false;file.write(QJsonDocument(row).toJson());}
    }
    output->close();return true;
}
bool CaseSelectedOutput(V2MainWindow& window){
    if(!OutputBox(window))return false;QTemporaryDir folder;if(!folder.isValid())return false;
    window.Viewport().SetSelection({});auto* output=V2OutputTool::Open(window,1);
    window.EntityTree()->clearSelection();
    for(const auto& id:OutputSelection(window,true).entityIds){auto* item=window.ItemOfEntity(id);if(!item)return false;item->setSelected(true);}
    QApplication::processEvents();output->TakeSelection();
    if(!Explain("道具先行で選択を取り込める",output->AssetCount()>=2))return false;
    if(!Explain("出力専用の3Dプレビューがある",output->findChild<QWidget*>("outputPreview3d")!=nullptr))return false;
    const auto before=window.Session().GetDocument().Revision();
    if(!Explain("線と立体のSTEPを作る",output->SaveTo(folder.filePath("selected.step"))))return false;
    const auto step=io::ReadWholeFile(folder.filePath("selected.step").toStdString());
    if(!Explain("STEPの内容がある",step.HasValue()&&step.Value().find("ISO-10303-21")!=std::string::npos))return false;
    output->ChooseFormat(0);
    if(!Explain("STLに線を黙って混ぜない",!output->SaveTo(folder.filePath("rejected.stl"))))return false;
    window.Viewport().SetSelection(OutputSelection(window,false));output->TakeSelection();
    if(!Explain("選択した立体だけSTLへ出せる",output->SaveTo(folder.filePath("solid.stl"))))return false;
    const auto stl=io::ReadWholeFile(folder.filePath("solid.stl").toStdString());
    const bool ok=Explain("出力で元の文書を変えない",before==window.Session().GetDocument().Revision())&&Explain("STLに三角形がある",stl.HasValue()&&stl.Value().size()>84);
    output->close();return ok;
}
bool CaseOutputPlacement(V2MainWindow& window){
    if(!OutputBox(window))return false;QTemporaryDir folder;if(!folder.isValid())return false;
    window.Viewport().SetSelection(OutputSelection(window,false));
    const auto original=window.Viewport().ShapeViews().front().mesh;auto* output=V2OutputTool::Open(window,2);
    geometry::OutputPlacement placement;placement.keepPosition=false;placement.source.origin={1,2,3};placement.destination={{30,20,10},{0,1,0},{1,0,0}};
    if(!Explain("基準点は配置点に一致",geometry::Distance(placement.Point(placement.source.origin),placement.destination.origin)<1e-9))return false;
    output->SetPlacement(placement);QApplication::processEvents();output->grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-selected-output.png"));if(!Explain("配置した別KCDを保存",output->SaveTo(folder.filePath("placed.kcd2"))))return false;
    const auto data=io::ReadWholeFile(folder.filePath("placed.kcd2").toStdString());if(!data.HasValue())return false;
    const auto file=io::LoadDocument(data.Value());if(!Explain("独立した1部品のKCD",file.HasValue()&&file.Value().snapshot.entities.size()==1))return false;
    const auto* frozen=std::get_if<domain::FreezeDerivedDefinition>(&file.Value().snapshot.features.front().definition);
    if(!Explain("三角形ではなくBRepを保存",frozen!=nullptr&&!frozen->frozenBrep.empty()))return false;
    const auto shape=kernel::RestoreOutputShape(frozen->frozenBrep);if(!shape.HasValue())return false;
    const auto mesh=kernel::BuildShapeMesh(shape.Value());if(!Explain("読み戻した立体が閉じている",mesh.HasValue()&&mesh.Value().closed))return false;
    geometry::Bounds3 expected;
    for(int i=0;i<8;++i)expected.Add(placement.Point({(i&1)?original.maximum.x:original.minimum.x,(i&2)?original.maximum.y:original.minimum.y,(i&4)?original.maximum.z:original.minimum.z}));
    if(!Explain("保存BRepの位置と向きが配置指定に一致",geometry::Distance(expected.minimum,mesh.Value().minimum)<1e-5 && geometry::Distance(expected.maximum,mesh.Value().maximum)<1e-5))return false;
    output->ChooseFormat(3);if(!Explain("同一KCDにも同じ配置で出力",output->SaveTo(QString())))return false;output->close();
    if(!Explain("元を残した独立コピー",CountOfKind(window,domain::EntityKind::Part)==2))return false;
    window.RunCommand("edit.undo");if(!Explain("1回のUndoでコピーだけ戻る",CountOfKind(window,domain::EntityKind::Part)==1))return false;
    window.RunCommand("edit.redo");if(!window.SaveAndReopen(QStringLiteral("output-placement.kcd2")))return false;
    return Explain("再読込後にも2つの実形状",window.Viewport().ShapeViewCount()==2);
}
bool CaseOutputManufacturing(V2MainWindow& window){
    window.RunCommand("file.new");if(!MakeCurvedGuideSurface(window))return false;
    auto* automatic=window.findChild<QComboBox*>("fabricationAutomaticGeneration");if(!automatic)return false;automatic->setCurrentIndex(0);
    auto& dock=window.FabricationDock();dock.SetGenerationDestination(0);dock.SetEqualPartCount(3);dock.SetSplitAxisIndex(4);
    window.RunCommand("fabrication.create");window.RunCommand("fabrication.create");if(window.FabricationModelCount()!=1)return false;
    dock.SetPartNumbersText(QStringLiteral("2"));dock.TypeAssemblyPercent(37);dock.PressApplyAssembly();
    window.Viewport().SetSelection(app::SelectAllOfKind(window.Session().GetDocument().Snapshot(),domain::EntityKind::FabricationModel));
    const auto before=window.Session().GetDocument().Revision();auto* output=V2OutputTool::Open(window,2);QTemporaryDir folder;
    if(!Explain("近似の対象部材だけを出力",output->AssetCount()==1&&output->SaveTo(folder.filePath("panel.kcd2"))))return false;
    output->close();if(!Explain("モデルの出力評価は文書を変えない",window.Session().GetDocument().Revision()==before))return false;
    auto* contents=window.findChild<QComboBox*>("fabricationGenerationOutput");if(!contents)return false;contents->setCurrentIndex(3);
    V2OutputTool::Generate(window,"fabrication.freeze_state");
    V2OutputTool* generated=nullptr;for(auto* d:window.findChildren<QDialog*>())if(d->isVisible()&&d->objectName()=="selectionOutput")generated=dynamic_cast<V2OutputTool*>(d);
    if(!Explain("生成も共通プレビューで指定部材の四辺と面",generated&&generated->AssetCount()==5))return false;
    generated->close();return Explain("生成配置の取消は元を変えない",window.Session().GetDocument().Revision()==before);
}
bool CaseOutputSelectionRecovery(V2MainWindow& window){
    if(!OutputBox(window))return false;auto* output=V2OutputTool::Open(window,2);
    window.Viewport().SetSelection({});output->TakeSelection();output->ChooseFormat(1);QTemporaryDir folder;
    if(!Explain("選択解除後は古い対象を書き出さない",output->AssetCount()==0&&!output->SaveTo(folder.filePath("empty.step"))))return false;
    const auto wires=app::SelectAllOfKind(window.Session().GetDocument().Snapshot(),domain::EntityKind::Wire);
    if(wires.entityIds.empty()||!ClickOnCurveOf(window,wires.entityIds.front()))return false;output->TakeSelection();
    const bool ok=Explain("3Dで選び直した線を出せる",output->AssetCount()==1&&output->SaveTo(folder.filePath("wire.step")));output->close();return ok;
}
bool CaseOutputSubelements(V2MainWindow& window){
    if(!OutputBox(window))return false;const auto id=OutputSelection(window,false).entityIds.front();
    app::SelectionRef ref;ref.entityId=id;ref.kind=app::SelectionElementKind::Face;ref.pickedFaceIndex=0;
    window.Viewport().SetSelection(app::SelectionSet{{id},{ref}});auto* output=V2OutputTool::Open(window,2);
    auto* scope=output->findChild<QComboBox*>("outputScope");if(!scope)return false;scope->setCurrentIndex(1);QTemporaryDir folder;
    if(!output->SaveTo(folder.filePath("face.kcd2")))return false;
    const auto bytes=io::ReadWholeFile(folder.filePath("face.kcd2").toStdString());if(!bytes.HasValue())return false;const auto file=io::LoadDocument(bytes.Value());
    if(!Explain("選択面だけを独立した面へ",file.HasValue()&&file.Value().snapshot.entities.size()==1&&file.Value().snapshot.entities.front().kind==domain::EntityKind::GuideSurface))return false;
    ref.kind=app::SelectionElementKind::Vertex;ref.hitPoint={3,4,5};window.Viewport().SetSelection(app::SelectionSet{{id},{ref}});output->TakeSelection();
    if(!output->SaveTo(folder.filePath("point.kcd2")))return false;
    output->ChooseFormat(0);const bool ok=Explain("選択点のSTLは明示的に拒否",!output->SaveTo(folder.filePath("point.stl")));output->close();return ok;
}
bool CaseOutputPlacementInvalid(V2MainWindow& window){
    if(!OutputBox(window))return false;window.Viewport().SetSelection(OutputSelection(window,false));auto* output=V2OutputTool::Open(window,3);
    geometry::OutputPlacement placement;placement.keepPosition=false;placement.destination.normal={};output->SetPlacement(placement);
    const auto before=window.Session().GetDocument().Revision();const bool ok=!output->SaveTo(QString())&&before==window.Session().GetDocument().Revision();output->close();return Explain("不正な配置は文書を変更しない",ok);
}
bool OutputAcrossDocuments(V2MainWindow& w) {
    if(!OutputBox(w))return false;
    QTemporaryDir folder;if(!folder.isValid())return false;
    auto other=io::DocumentFile{};other.snapshot=w.Session().GetDocument().Snapshot();
    const auto original=other.snapshot;
    // Clone entity IDs and revision deliberately: revision alone cannot identify a document.
    other.snapshot.id=*base::DocumentId::Parse("b0000000-0000-4000-8000-000000000901");
    const auto archive=io::SaveDocument(other);const auto path=folder.filePath("other.kcd2");
    if(!archive.HasValue()||!io::WriteFileAtomically(path.toStdString(),archive.Value()).HasValue())return false;
    w.Viewport().SetSelection(OutputSelection(w,false));
    QPointer<V2OutputTool> output=V2OutputTool::Open(w,1);
    if(!output||output->AssetCount()!=1)return false;
    if(!Explain("failed open retains source and usable export preview",!w.OpenDocumentFile(folder.filePath("missing.kcd2"))
        &&w.Session().GetDocument().Snapshot().id==original.id&&output&&output->SaveTo(folder.filePath("before.step"))))return false;
    if(!w.OpenDocumentFile(path))return false;
    const auto revision=w.Session().GetDocument().Revision();
    const auto entities=w.Session().GetDocument().Snapshot().entities.size();
    if(!Explain("document clone keeps revision but changes identity",revision==original.revision
        &&w.Session().GetDocument().Snapshot().id!=original.id))return false;
    if(output) {
        if(!Explain("stale preview cannot export a previous document",!output->SaveTo(folder.filePath("stale.step"))
            &&!QFile::exists(folder.filePath("stale.step"))))return false;
        output->ChooseFormat(3);
        if(!Explain("stale preview cannot append to a different document",!output->SaveTo(QString())
            &&w.Session().GetDocument().Revision()==revision
            &&w.Session().GetDocument().Snapshot().entities.size()==entities))return false;
        w.Viewport().SetSelection(OutputSelection(w,false));output->TakeSelection();output->ChooseFormat(1);
    }else {
        w.Viewport().SetSelection(OutputSelection(w,false));output=V2OutputTool::Open(w,1);
    }
    const bool passed=Explain("reacquiring the current document restores export",output&&output->AssetCount()==1
        &&output->SaveTo(folder.filePath("after.step")));
    if(output)output->close();return passed;
}
bool SelectedPatternPanels(V2MainWindow& w)
{
    w.RunCommand("file.new");if(!MakeCurvedGuideSurface(w))return false;
    auto* automatic=w.findChild<QComboBox*>("fabricationAutomaticGeneration");if(!automatic)return false;automatic->setCurrentIndex(0);
    auto& parts=w.FabricationDock();parts.SetGenerationDestination(0);parts.SetEqualPartCount(3);parts.SetSplitAtCorners(false);
    w.RunCommand("fabrication.create");w.RunCommand("fabrication.create");
    if(!Explain("three fabrication panels for selection",w.FabricationPanelCount()==3))return false;
    w.RunCommand("fabrication.create_pattern");parts.SetPartNumbersText(QStringLiteral("2"));w.RunCommand("export.svg");
    auto& out=w.ExportDock();
    if(!Explain("export count is the chosen panel rather than all three",out.Counts().selectedFabricationPanels==1))return false;
    QTemporaryDir dir;const auto revision=w.Session().GetDocument().Revision();
    if(!out.ChooseTarget(app::ExportTarget::SelectedFabricationPanels))return false;
    for(const auto format:{app::ExportFormat::Svg,app::ExportFormat::Dxf,app::ExportFormat::Pdf}) {
        if(!out.ChooseFormat(format))return false;
        const auto suffix=QString::fromUtf8(app::ExportFormatExtension(format).data());
        parts.SetPartNumbersText(QStringLiteral("すべて"));out.ChoosePath(dir.filePath("all"+suffix));
        if(!out.RunNow())return false;const auto all=io::ReadWholeFile(dir.filePath("all"+suffix).toStdString());
        parts.SetPartNumbersText(QStringLiteral("2"));out.ChoosePath(dir.filePath("one"+suffix));
        if(!out.RunNow())return false;const auto one=io::ReadWholeFile(dir.filePath("one"+suffix).toStdString());
        if(!Explain("every 2D format excludes non-selected panel geometry",all.HasValue()&&one.HasValue()
            &&one.Value().size()>100&&one.Value().size()<all.Value().size()))return false;
    }
    for(const auto bad:{QStringLiteral("99"),QStringLiteral("abc"),QStringLiteral(","),QString()}) {
        parts.SetPartNumbersText(bad);out.ChoosePath(dir.filePath("bad.pdf"));
        if(!Explain("invalid or blank target never exports all panels",out.Counts().selectedFabricationPanels==0
            &&!out.RunNow()&&!QFile::exists(dir.filePath("bad.pdf"))))return false;
    }
    parts.SetPartNumbersText(QStringLiteral("2, 2"));
    return Explain("selection and export preserve source pattern and document",out.Counts().selectedFabricationPanels==1
        &&w.Session().GetDocument().Revision()==revision);
}

bool SmallAccurateStep(V2MainWindow& w) {
    w.RunCommand("file.new");if(!DrawOutputRectangle(w,.15))return false;
    w.SetExtrudeChooser([](const app::ExtrudeChoice& initial,const app::ExtrudeFacts&){auto choice=initial;choice.distanceMm=.05;return std::optional<app::ExtrudeChoice>{choice};});
    w.RunCommand("part.extrude");w.RunCommand("part.extrude");
    if(!Explain("small profile makes a real closed part",CountOfKind(w,domain::EntityKind::Part)==1&&w.Viewport().ShapeViews().front().mesh.closed))return false;
    w.Viewport().SetSelection(OutputSelection(w,false));const auto revision=w.Session().GetDocument().Revision();
    if(!Explain("STEP eligibility uses model accuracy rather than pointer joining distance",w.CanExportSelectedParts()))return false;
    QTemporaryDir folder;if(!folder.isValid())return false;auto* output=V2OutputTool::Open(w,1);
    const bool saved=output->SaveTo(folder.filePath("small-accurate.step"));output->close();
    const auto bytes=io::ReadWholeFile(folder.filePath("small-accurate.step").toStdString());
    return Explain("small part passes actual STEP roundtrip without changing the document",saved&&bytes.HasValue()
        &&bytes.Value().find("END-ISO-10303-21;")!=std::string::npos&&w.Session().GetDocument().Revision()==revision
        &&w.Session().GetDocument().Snapshot().settings.tolerance.interactiveJoinMm==.01);
}
bool WireCopyKeepsExistingShapes(V2MainWindow& w) {
    if(!OutputBox(w))return false;
    const auto parts=app::SelectAllOfKind(w.Session().GetDocument().Snapshot(),domain::EntityKind::Part);
    const auto wires=app::SelectAllOfKind(w.Session().GetDocument().Snapshot(),domain::EntityKind::Wire);
    if(parts.entityIds.size()!=1||wires.entityIds.size()!=1)return false;
    app::ExtrudeChoice choice;choice.makePart=true;choice.booleanMode=modeling::ExtrudeBooleanMode::AddToPart;
    app::ExtrudePlan plan;plan.targetSolid=parts.entityIds.front();
    const auto before=w.BooleanTargetShapeFor(choice,plan);
    const auto original=w.Viewport().ShapeViews().front().mesh;
    if(!before)return false;
    w.Viewport().SetSelection(wires);w.RunCommand("output.copy");
    V2OutputTool* output=nullptr;
    for(auto* dialog:w.findChildren<QDialog*>())
        if(dialog->isVisible()&&dialog->objectName()=="selectionOutput")output=dynamic_cast<V2OutputTool*>(dialog);
    auto* format=output?output->findChild<QComboBox*>("outputFormat"):nullptr;
    auto* confirm=output?output->findChild<QPushButton*>("outputConfirm"):nullptr;
    if(!format||!confirm||!confirm->isVisible())return false;
    format->setCurrentIndex(3);confirm->click();QApplication::processEvents();
    const auto after=w.BooleanTargetShapeFor(choice,plan);
    if(!Explain("wire-only independent copy retains existing native shape handles",after&&after->value==before->value
        &&CountOfKind(w,domain::EntityKind::Wire)==2&&CountOfKind(w,domain::EntityKind::Part)==1))return false;
    const auto& copy=w.Session().GetDocument().Snapshot().features.back();
    if(!Explain("copied wire is editable and independent of its source",copy.type==domain::FeatureType::CreateWire
        &&copy.inputEntityIds.empty()))return false;
    w.RunCommand("edit.undo");
    if(!Explain("one Undo removes only the independent wire",CountOfKind(w,domain::EntityKind::Wire)==1
        &&CountOfKind(w,domain::EntityKind::Part)==1))return false;
    w.RunCommand("edit.redo");
    if(!w.SaveAndReopen(QStringLiteral("wire-copy-keeps-shapes.kcd2")))return false;
    const auto& restored=w.Viewport().ShapeViews();
    return Explain("wire copy survives Redo and reopen without altering the original solid",CountOfKind(w,domain::EntityKind::Wire)==2
        &&restored.size()==1&&restored.front().mesh.closed
        &&geometry::Distance(restored.front().mesh.minimum,original.minimum)<1e-9
        &&geometry::Distance(restored.front().mesh.maximum,original.maximum)<1e-9);
}
bool CaseOutputOwnership(V2MainWindow& w){
    if(!OutputBox(w))return false;
    w.Viewport().SetSelection(OutputSelection(w,false));const auto baseline=kernel::CachedShapeCount();
    auto* output=V2OutputTool::Open(w,2);
    if(!Explain("output owns a native snapshot",output->AssetCount()==1&&kernel::CachedShapeCount()==baseline+1))return false;
    geometry::OutputPlacement placement;placement.keepPosition=false;
    for(int i=0;i<6;++i){placement.destination.origin.x=i;output->SetPlacement(placement);
        if(!Explain("output placement replaces its owned preview",kernel::CachedShapeCount()==baseline+2))return false;}
    QTemporaryDir folder;if(!output->SaveTo(folder.filePath("placed.kcd2")))return false;
    output->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    return Explain("closing output releases snapshots without affecting the source",kernel::CachedShapeCount()==baseline&&CountOfKind(w,domain::EntityKind::Part)==1);
}
}
std::vector<SelfTestCase> OutputCases(){return {{"HP-OUT-13 visible output respects hidden ancestors",VisibleOutputHonorsAncestors},{"HP-OUT-14 explicit hidden wire and solid selection remains exportable",ExplicitHiddenOutput},{"HP-OUT-12 wire-only independent copy preserves existing shapes",WireCopyKeepsExistingShapes},{"HP-OUT-11 small STEP uses model accuracy",SmallAccurateStep},{"HP-OUT-10 selected fabrication patterns exclude other panels",SelectedPatternPanels},{"HP-OUT-09 same revision different document rejects stale export",OutputAcrossDocuments},{"HP-OUT-08 prepared output reuse and format changes",OutputPreviewReuse},{"HP-OUT-07 output snapshot and placement ownership",CaseOutputOwnership},{"HP-OUT-06 面と点の選択部分出力",CaseOutputSubelements},{"HP-OUT-04 近似の指定部材と生成配置の取消",CaseOutputManufacturing},{"HP-OUT-05 3D選択と選び直し",CaseOutputSelectionRecovery},{"HP-OUT-01 選択先行と道具先行の出力・3Dプレビュー・混在形式",CaseSelectedOutput},{"HP-OUT-02 同一と別KCDの配置・BRep保存・Undo再生成",CaseOutputPlacement},{"HP-OUT-03 不正な配置を拒否し元文書を保持",CaseOutputPlacementInvalid}};}
}
