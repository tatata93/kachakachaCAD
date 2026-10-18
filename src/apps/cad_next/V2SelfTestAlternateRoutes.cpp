#include <QEvent>
#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2OperationPanelHost.h"
#include "V2ThickenDock.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QShortcutEvent>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QString>
#include <QFile>
#include <QIODevice>
#include <QByteArray>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <array>

namespace kachakacha::v2::selftest {
bool AlternateShortcutRoutes(V2MainWindow& w)
{
    w.SetMode(app::UiMode::Drawing);
    const std::array<std::pair<const char*,modeling::DrawingTool>,4> keys={{{"L",modeling::DrawingTool::Line},{"C",modeling::DrawingTool::Circle},{"V",modeling::DrawingTool::Select},{"M",modeling::DrawingTool::Measure}}};
    for(const auto& [key,tool]:keys){QAction* shortcut=nullptr;int count=0;
        for(auto* action:w.findChildren<QAction*>())if(action->isEnabled()&&action->isVisible()&&action->shortcut()==QKeySequence(key)){shortcut=action;++count;}
        if(!Explain((std::string("one registered shortcut: ")+key).c_str(),count==1))return false;
        w.SelectTool(modeling::DrawingTool::Select);QShortcutEvent event(QKeySequence(key),0,false);QApplication::sendEvent(shortcut,&event);
        if(!Explain((std::string("resolved Qt shortcut enters tool: ")+key).c_str(),w.Session().CurrentTool()==tool))return false;
    }
    w.ActivateCommand("draw.line");w.Viewport().ClickAt({160,180});
    const auto points=w.Session().PlacedPointCount();w.ActivateCommand("view.number_settings");
    QShortcutEvent repeat(QKeySequence("L"),0,false);QApplication::sendEvent(w.ActionFor("draw.line"),&repeat);
    return Explain("same tool shortcut restores pane and preserves unfinished points",points>0&&w.Session().PlacedPointCount()==points&&w.ShelfShown(app::Shelf::Drawing));
}
bool AlternateViewDuringInput(V2MainWindow& w)
{
    w.SetMode(app::UiMode::Drawing);
    for(const auto end:{geometry::Vector3{20,0,0},geometry::Vector3{20,20,0}}){
        w.SelectTool(modeling::DrawingTool::Line);
        const auto start=w.Viewport().Mapping().Project(end.y==0?geometry::Vector3{0,0,0}:geometry::Vector3{20,0,0});
        const auto finish=w.Viewport().Mapping().Project(end);if(!start||!finish)return false;
        w.Viewport().ClickAt({start->x,start->y});w.Viewport().ClickAt({finish->x,finish->y});w.SelectTool(modeling::DrawingTool::Select);
    }
    w.Viewport().SetSelection({});w.ActivateCommand("wire.join");
    const auto emptyLabel=w.PendingCommandLabel();w.ActivateCommand("view.fit_selection");
    if(!Explain("view without targets gives a reason and keeps the editing tool",!emptyLabel.isEmpty()&&w.PendingCommandLabel()==emptyLabel&&w.OperationHost().TemporaryPage()))return false;
    auto point=w.Viewport().Mapping().Project({10,0,0});if(!point)return false;w.Viewport().SelectAt({point->x,point->y},Qt::NoModifier);
    const auto label=w.PendingCommandLabel();const auto revision=w.Session().GetDocument().Revision();
    for(const auto* view:{"view.fit_all","view.fit_selection"}){
        w.ActivateCommand(view);
        if(!Explain((std::string("view operation keeps pending input: ")+view).c_str(),!label.isEmpty()&&w.PendingCommandLabel()==label&&w.Viewport().Selection().entityIds.size()==1&&w.OperationHost().TemporaryPage()))return false;
    }
    point=w.Viewport().Mapping().Project({20,10,0});if(!point)return false;w.Viewport().SelectAt({point->x,point->y},Qt::NoModifier);
    if(!Explain("can collect the second target after zooming",w.Viewport().Selection().entityIds.size()==2))return false;
    w.ConfirmPendingCommand();
    return Explain("continued tool commits after view changes",w.Session().GetDocument().Revision()>revision&&w.PendingCommandLabel().isEmpty());
}
bool AlternateFirstPointUndo(V2MainWindow& w)
{
    for(const auto* command:{"draw.line","draw.polyline","draw.circle","draw.arc","draw.rectangle","draw.bezier","draw.spline"}){
        w.ActivateCommand("file.new");w.SetMode(app::UiMode::Drawing);w.Viewport().SetViewDirection(ViewDirection::Top);
        const auto revision=w.Session().GetDocument().Revision();const auto emptyHint=w.ActionFor("edit.undo")->toolTip();
        w.ActivateCommand(command);w.Viewport().ClickAt({220,230});
        auto* undo=w.ActionFor("edit.undo");
        if(!Explain((std::string("first point enables Undo and its explanation: ")+command).c_str(),w.Session().PlacedPointCount()==1&&undo&&undo->isEnabled()&&undo->toolTip()!=emptyHint))return false;
        QShortcutEvent event(QKeySequence("Ctrl+Z"),0,false);QApplication::sendEvent(undo,&event);
        if(!Explain("Undo removes only unfinished input and disables empty history",w.Session().PlacedPointCount()==0
            &&w.Session().GetDocument().Revision()==revision&&!undo->isEnabled()&&undo->toolTip()==emptyHint&&!w.ActionFor("edit.redo")->isEnabled()))return false;
    }
    return true;
}
bool AlternateSaveDuringInput(V2MainWindow& w)
{
    QTemporaryDir folder;if(!folder.isValid())return false;
    w.SetMode(app::UiMode::Drawing);
    for(const auto end:{geometry::Vector3{20,0,0},geometry::Vector3{20,20,0}}){
        w.SelectTool(modeling::DrawingTool::Line);
        const auto start=w.Viewport().Mapping().Project(end.y==0?geometry::Vector3{0,0,0}:geometry::Vector3{20,0,0});
        const auto finish=w.Viewport().Mapping().Project(end);if(!start||!finish)return false;
        w.Viewport().ClickAt({start->x,start->y});w.Viewport().ClickAt({finish->x,finish->y});w.SelectTool(modeling::DrawingTool::Select);
    }
    w.Viewport().SetSelection({});w.ActivateCommand("wire.join");
    auto point=w.Viewport().Mapping().Project({10,0,0});if(!point)return false;w.Viewport().SelectAt({point->x,point->y},Qt::NoModifier);
    const auto label=w.PendingCommandLabel();const auto selected=w.Viewport().Selection().entityIds;
    const auto revision=w.Session().GetDocument().Revision();auto* page=w.OperationHost().TemporaryPage();
    for(int route=0;route<4;++route){
        const auto path=folder.filePath(QStringLiteral("pending-%1.kcd2").arg(route));
        w.SetPathChooser([path,route](bool){return route==0||route==3?QString():path;});
        w.ActivateCommand(route<2?"file.save":"file.save_as");
        if(!Explain("save success or cancellation keeps the target role and editing page",!label.isEmpty()&&w.PendingCommandLabel()==label
            &&w.Viewport().Selection().entityIds==selected&&w.OperationHost().TemporaryPage()==page&&page
            &&w.Session().GetDocument().Revision()==revision))return false;
        if(!Explain("save writes only on accepted paths",QFileInfo::exists(path)==(route==1||route==2)))return false;
    }
    w.SetPathChooser([](bool){return QString();});
    point=w.Viewport().Mapping().Project({20,10,0});if(!point)return false;w.Viewport().SelectAt({point->x,point->y},Qt::NoModifier);
    w.ConfirmPendingCommand();
    return Explain("saved pending command can continue and commit",w.PendingCommandLabel().isEmpty()&&w.Session().GetDocument().Revision()>revision);
}
bool AlternateOwnerModel(V2MainWindow& w)
{
    const auto path=qEnvironmentVariable("KACHACAD_MODELING_OWNER_INPUT");
    const auto output=qEnvironmentVariable("KACHACAD_MODELING_OWNER_OUTPUT");
    QFile original(path);if(!original.open(QIODevice::ReadOnly)||output.isEmpty()||!QDir().mkpath(output))return false;
    const auto bytes=original.readAll();original.close();
    if(!Explain("actual owner model loads without rebuild problems",w.OpenDocumentFile(path)&&w.RebuildProblems().isEmpty()))return false;
    base::EntityId source;
    for(const auto& feature:w.Session().GetDocument().Snapshot().features)
        if(const auto* d=std::get_if<domain::CreateGuideSurfaceDefinition>(&feature.definition);d&&d->method==0&&!feature.outputs.empty())source=feature.outputs.front().entityId;
    if(!Explain("actual model has a planar surface for thickness",!source.IsNil()))return false;
    const int parts=CountOfKind(w,domain::EntityKind::Part),wires=CountOfKind(w,domain::EntityKind::Wire);
    w.SetMode(app::UiMode::Part);w.Viewport().SetSelection({});w.ActivateCommand("part.thicken");
    auto* amount=w.findChild<QDoubleSpinBox*>("thickenAmount");auto* out=w.findChild<QComboBox*>("thickenOutput");
    if(!amount||!out)return false;amount->setValue(.5);out->setCurrentIndex(2);
    if(!w.ThickenDock().ClickPlacementCard(fabrication::ThicknessPlacement::Outside))return false;
    app::SelectionSet selected;selected.entityIds={source};w.Viewport().SetSelection(selected);
    w.Viewport().SetViewDirection(ViewDirection::Isometric);w.Viewport().FitToDocument();QApplication::processEvents();
    w.grab().save(output+"/owner-thickening-preview.png");
    if(!Explain("actual surface produces both solid and boundary wires",w.ThickenDock().ClickConfirm()
        &&CountOfKind(w,domain::EntityKind::Part)==parts+1&&CountOfKind(w,domain::EntityKind::Wire)>wires))return false;
    w.ActivateCommand("selection.activate");const auto count=kernel::CachedShapeCount();
    for(int i=0;i<3;++i){w.ActivateCommand("edit.undo");if(CountOfKind(w,domain::EntityKind::Part)!=parts)return false;
        w.ActivateCommand("edit.redo");if(!Explain("actual model history retains shape count without native accumulation",
            CountOfKind(w,domain::EntityKind::Part)==parts+1&&kernel::CachedShapeCount()==count&&w.RebuildProblems().isEmpty()))return false;}
    if(!w.SaveAndReopen(output+"/owner-thickened-result.kcd2")||!w.RebuildProblems().isEmpty())return false;
    w.Viewport().SetViewDirection(ViewDirection::Isometric);w.Viewport().FitToDocument();QApplication::processEvents();w.grab().save(output+"/owner-thickened-result.png");
    QFile reread(path);return Explain("original owner file remains byte identical",reread.open(QIODevice::ReadOnly)&&reread.readAll()==bytes);
}
}
