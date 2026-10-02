#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2OutputTool.h"
#include "V2OutputPreview.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/io/DocumentFile.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <QString>
#include <QPointF>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QApplication>
#include <algorithm>
#include <QWidget>
#include <QTemporaryDir>
#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <cmath>
namespace kachakacha::v2::selftest {
namespace {
bool DrawOutputRectangle(V2MainWindow& window) {
    auto& viewport=window.Viewport();viewport.SetViewDirection(ViewDirection::Top);
    viewport.SetViewCenter({});viewport.SetVisibleWidthMm(200);
    window.SelectTool(modeling::DrawingTool::Rectangle);viewport.SetSnapSuppressed(true);
    viewport.ClickAt(QPointF(viewport.width()*0.35,viewport.height()*0.35));
    viewport.HoverAt(QPointF(viewport.width()*0.65,viewport.height()*0.65));
    viewport.ClickAt(QPointF(viewport.width()*0.65,viewport.height()*0.65));viewport.SetSnapSuppressed(false);
    window.SelectTool(modeling::DrawingTool::Select);
    viewport.SetSelection(app::SelectAllOfKind(window.Session().GetDocument().Snapshot(),domain::EntityKind::Wire));
    return !viewport.Selection().entityIds.empty();
}
bool OutputBox(V2MainWindow& window){window.RunCommand("file.new");if(!DrawOutputRectangle(window))return false;window.RunCommand("part.extrude");window.RunCommand("part.extrude");return CountOfKind(window,domain::EntityKind::Part)==1;}
app::SelectionSet OutputSelection(V2MainWindow& window,bool wires){app::SelectionSet selection;for(const auto& entity:window.Session().GetDocument().Snapshot().entities)if(entity.kind==domain::EntityKind::Part || (wires&&entity.kind==domain::EntityKind::Wire))selection.entityIds.push_back(entity.id);return selection;}
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
    output->SetPlacement(placement);if(!Explain("配置した別KCDを保存",output->SaveTo(folder.filePath("placed.kcd2"))))return false;
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
bool CaseOutputPlacementInvalid(V2MainWindow& window){
    if(!OutputBox(window))return false;window.Viewport().SetSelection(OutputSelection(window,false));auto* output=V2OutputTool::Open(window,3);
    geometry::OutputPlacement placement;placement.keepPosition=false;placement.destination.normal={};output->SetPlacement(placement);
    const auto before=window.Session().GetDocument().Revision();const bool ok=!output->SaveTo(QString())&&before==window.Session().GetDocument().Revision();output->close();return Explain("不正な配置は文書を変更しない",ok);
}
}
std::vector<SelfTestCase> OutputCases(){return {{"HP-OUT-04 近似の指定部材と生成配置の取消",CaseOutputManufacturing},{"HP-OUT-05 3D選択と選び直し",CaseOutputSelectionRecovery},{"HP-OUT-01 選択先行と道具先行の出力・3Dプレビュー・混在形式",CaseSelectedOutput},{"HP-OUT-02 同一と別KCDの配置・BRep保存・Undo再生成",CaseOutputPlacement},{"HP-OUT-03 不正な配置を拒否し元文書を保持",CaseOutputPlacementInvalid}};}
}
