#include <QEvent>
#include "V2SelfTest.h"
#include "V2ToolIcons.h"
#include "kachakacha/app/CommandCatalog.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QPixmap>
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2Ribbon.h"
#include "V2OperationPanelHost.h"
#include "V2SolidTool.h"
#include "V2SolidDock.h"
#include <QPushButton>
#include "V2EdgeFinishTool.h"
#include "V2EdgeFinishDock.h"
#include "V2ShellSplitTool.h"
#include "V2SurfaceAnalysisTool.h"
#include "V2SurfaceEditTool.h"
#include "V2LoopFacesTool.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QString>
#include <array>
namespace kachakacha::v2::selftest {
bool AlternateShortcutRoutes(V2MainWindow&);
bool AlternateViewDuringInput(V2MainWindow&);
bool AlternateFirstPointUndo(V2MainWindow&);
bool AlternateSaveDuringInput(V2MainWindow&);
bool AlternateOwnerModel(V2MainWindow&);
namespace {
int Active(V2MainWindow& w) {
    const bool temporary=w.OperationHost().HasTemporaryPage()&&w.Mode()!=app::UiMode::Instructions;
    const bool displayed=w.OperationHost().isVisible();
    return int(w.SolidTool().Active())+int(w.EdgeFinishTool().Active())+int(w.ShellSplitTool().Active())
        +int(w.SurfaceEdit().Active())+int(w.LoopFacesTool().Active())+int(w.BooleanShelfShown())
        +int(w.ThickenShelfShown())+int(w.ApproxShelfShown())+int(temporary)
        +int(displayed&&!temporary&&w.OperationHost().Shows(app::Shelf::Surface))
        +int(displayed&&!temporary&&w.OperationHost().Shows(app::Shelf::Extrude));
}
bool Illustrations(V2MainWindow& w) {
    const auto icon=[](const char* id,int variant=-1){return V2ToolIcon(id,"",variant).pixmap(64,64).toImage();};
    for(const auto& group:std::vector<std::vector<QImage>>{
        {icon("part.boolean_add"),icon("part.boolean_cut"),icon("part.boolean_intersect")},
        {icon("part.extrude"),icon("part.thicken"),icon("part.loft_solid"),icon("part.sweep")},
        {icon("instructions.scene_editor"),icon("instructions.sheet_editor"),icon("instructions.paper"),icon("instructions.place_scene")},
        {icon("part.revolve"),icon("guide.revolve"),icon("wire.rotate")},
        {icon("wire.project_surface"),icon("wire.wrap_project"),icon("part.curved_emboss")},
        {icon("measure.open",0),icon("measure.open",1),icon("measure.open",2),icon("measure.open",3),icon("measure.open",4)},
        {icon("surface.create",0),icon("surface.create",1),icon("surface.create",2),icon("surface.create",4),icon("surface.create",5),icon("surface.create",6),icon("surface.create",8)}})
        for(std::size_t i=0;i<group.size();++i)for(std::size_t j=i+1;j<group.size();++j)if(!Explain("related operations have different illustrations",group[i]!=group[j]))return false;
    const auto output=qEnvironmentVariable("KACHACAD_TOOL_AUDIT_DIR");
    if(!output.isEmpty()){
        QDir dir(output);dir.mkpath("icons");
        for(const auto& command:app::CommandCatalog())V2ToolIcon(command.id,command.labelJa).pixmap(96,96).save(dir.filePath("icons/"+QString::fromUtf8(command.id.data(),int(command.id.size()))+".png"));
        for(int m:{0,1,2,4,5,6,8})V2ToolIcon("surface.create","",m).pixmap(96,96).save(dir.filePath("icons/surface.create-"+QString::number(m)+".png"));
        for(int m=0;m<5;++m)V2ToolIcon("measure.open","",m).pixmap(96,96).save(dir.filePath("icons/measure.open-"+QString::number(m)+".png"));
    }
    w.SetMode(app::UiMode::Drawing);return true;
}
bool Lifecycle(V2MainWindow& w) {
    for (const auto* command : {"draw.line", "wire.fillet", "wire.chamfer", "wire.corner_fillet", "wire.corner_chamfer"}) {
        w.SelectTool(modeling::DrawingTool::Select);
        w.ActivateCommand("draw.line");
        w.ActivateCommand("view.surface_analysis");
        if (!Explain("analysis opens from an armed drawing tool", w.ShelfShown(app::Shelf::SurfaceAnalysis))) return false;
        w.ActivateCommand(command);
        const auto expected = w.Session().CurrentTool() == modeling::DrawingTool::ChamferOrFilletPair
            ? app::Shelf::Corner : app::Shelf::Drawing;
        if (!Explain((std::string("analysis does not cover inputs: ") + command).c_str(),
            w.ShelfShown(expected))) return false;
    }
    w.SurfaceAnalysis().Close();
    const std::array<const char*,21> tools={"surface.create","surface.from_lines","part.extrude","part.revolve","part.loft_solid","part.sweep",
        "part.thicken","part.thicken_to_plane","part.boolean_add","part.boolean_cut","part.fillet","part.chamfer","part.shell","part.split",
        "surface.match","surface.refit","wire.project_surface","wire.wrap_project","dimension.open","wire.offset","fabrication.create"};
    for(const auto* from:tools)for(const auto* to:tools){
        w.SelectTool(modeling::DrawingTool::Select);w.RunCommand(from);w.RunCommand(to);QApplication::processEvents();
        if(!Explain((std::string("exclusive tools: ")+from+" -> "+to).c_str(),(Active(w)==1||(std::string_view(from)=="wire.offset"&&std::string_view(to)=="wire.offset"&&Active(w)==0))))return false;
    }
    for(const auto* tool:tools){
        w.SetMode(app::UiMode::Drawing);w.SelectTool(modeling::DrawingTool::Select);w.RunCommand(tool);
        for(auto* action:w.findChildren<QAction*>())if(action->shortcut()==QKeySequence("L")){action->trigger();break;}
        if(!Explain((std::string("shortcut exits old tool: ")+tool).c_str(),Active(w)==0&&w.Session().CurrentTool()==modeling::DrawingTool::Line))return false;
        for(auto mode:app::AllUiModes()){
            w.SetMode(app::UiMode::Drawing);w.RunCommand(tool);w.SetMode(mode);
            if(!Explain((std::string("mode exits old tool: ")+tool).c_str(),Active(w)==0&&!w.Viewport().ReferencePickPending()&&!w.Viewport().ExtrudeHandleShown()))return false;
        }
    }
    w.SetMode(app::UiMode::Part);w.Ribbon().ShowCategory(4);
    if(!Explain("part move uses a visible enabled ribbon action",w.Ribbon().ToolEnabled(QStringLiteral("移動"))))return false;
    w.SelectTool(modeling::DrawingTool::Select);
    if(!Explain("preview update is available before a model exists",w.CommandEnabled("fabrication.preview_update",nullptr)))return false;
    const auto revision=w.Session().GetDocument().Revision();
    w.RunCommand("fabrication.preview_update");w.RunCommand("fabrication.preview_update");
    return Explain("preview update opens inputs without committing",w.ApproxShelfShown()&&w.Session().GetDocument().Revision()==revision);
}
bool ActivationAudit(V2MainWindow& w) {
    const QString directory = qEnvironmentVariable("KACHACAD_TOOL_ACTIVATION_AUDIT_DIR");
    QJsonArray report;
    if (!directory.isEmpty()) QDir().mkpath(directory);
    for (const auto& command : app::CommandCatalog()) {
        w.SetMode(app::UiMode::Drawing);
        w.ActivateCommand("selection.activate");
        w.Viewport().SetSelection({});
        QElapsedTimer timer; timer.start();
        w.ActivateCommand(command.id);
        QApplication::processEvents();
        const bool page = w.OperationHost().TemporaryPage() || w.OperationHost().CurrentPage();
        const bool tool = command.mode == app::CommandMode::Tool && command.id != "selection.activate";
        if (!Explain((std::string("activation gives visible input or reason: ") + std::string(command.id)).c_str(),
            !w.StatusText().isEmpty() && (!tool || page || !w.PendingCommandLabel().isEmpty()
                || w.Session().CurrentTool() != modeling::DrawingTool::Select))) return false;
        if (w.Session().CurrentTool() == modeling::DrawingTool::ChamferOrFilletPair
            && !Explain("corner settings are actually displayed", w.ShelfShown(app::Shelf::Corner))) return false;
        const QString id = QString::fromUtf8(command.id.data(), int(command.id.size()));
        report.append(QJsonObject{{"command", id}, {"status", w.StatusText()},
            {"panel", w.OperationHost().CurrentShelfTitle()}, {"has_input_page", page},
            {"pending", w.PendingCommandLabel()}, {"elapsed_ms", timer.elapsed()},
            {"scope", QStringLiteral("entry with no selected geometry; not proof of generated output")}});
        if (!directory.isEmpty()) w.grab().save(QDir(directory).filePath(id + ".png"));
        w.Viewport().CancelPointPick();
        w.Viewport().CancelReferencePick();
    }
    if (!directory.isEmpty()) {
        QFile output(QDir(directory).filePath("activation.json"));
        if (!output.open(QIODevice::WriteOnly)) return false;
        output.write(QJsonDocument(report).toJson());
    }
    w.SetMode(app::UiMode::Drawing);
    return true;
}

bool ModalKeys(V2MainWindow& w) {
    w.SelectTool(modeling::DrawingTool::Line);const auto before=w.Session().GetDocument().Revision();
    w.Viewport().ClickAt({100,100});QDialog dialog(&w);dialog.setModal(true);dialog.show();QApplication::processEvents();
    for(int key:{int(Qt::Key_Return),int(Qt::Key_Escape)}){QKeyEvent press(QEvent::KeyPress,key,Qt::NoModifier);QApplication::sendEvent(&dialog,&press);}
    const bool intact=w.Session().CurrentTool()==modeling::DrawingTool::Line&&w.Session().HasPlacedPoints()&&w.Session().GetDocument().Revision()==before;
    dialog.close();return Explain("dialog keys do not confirm or cancel the background tool",intact);
}
bool PreviewLifetime(V2MainWindow& w) {
    const auto committedExports = [&] {
        kachakacha::v2::base::EntityId newest;
        for (const auto& entity:w.Session().GetDocument().Snapshot().entities)
            if(entity.kind==kachakacha::v2::domain::EntityKind::Part)newest=entity.id;
        w.SelectTool(modeling::DrawingTool::Select);
        return !newest.IsNil()&&w.SelectTreeRowForEntity(newest)&&w.CanExportSelectedParts();
    };
    if(!w.ApplyToolShotState(QStringLiteral("ui-tool-revolve"))){
        const auto message=std::string("revolve fixture: ")+w.StatusText().toStdString()+" active="+std::to_string(w.SolidTool().Active())+" profiles="+std::to_string(w.SolidTool().Input().profiles.size())+" axis="+w.SolidTool().Input().axis.ToString();Note(message.c_str());w.grab().save("out/all-tools-preview-failure.png");return false;}
    const auto revolveCount=kernel::CachedShapeCount();
    for(int i=0;i<5;++i){
        bool clicked=false;for(auto* button:w.findChildren<QPushButton*>())if(button->text()==w.SolidTool().Dock()->MethodCardTextJa(0)&&button->isVisible()){button->click();clicked=true;break;}
        if(!Explain("revolve method card can be clicked",clicked))return false;
        if(!Explain("revolve preview replaces its owned shape",kernel::CachedShapeCount()==revolveCount))return false;}
    w.SelectTool(modeling::DrawingTool::Select);
    if(!Explain("revolve cancel releases preview",kernel::CachedShapeCount()+1==revolveCount))return false;
    if(!Explain("revolve fixture can be reopened",w.ApplyToolShotState(QStringLiteral("ui-tool-revolve"))))return false;
    w.SolidTool().Confirm();
    if(!Explain("committed revolve native shape survives selection callbacks",committedExports()))return false;
    w.RunCommand("file.new");w.SetMode(app::UiMode::Drawing);
    if(!Explain(w.StatusText().toUtf8().constData(),w.ApplyToolShotState(QStringLiteral("ui-tool-fillet"))))return false;
    const auto edgeCount=kernel::CachedShapeCount();
    for(int i=0;i<5;++i){if(!w.EdgeFinishTool().Dock()->TypeSize(i%2?2.0:1.8))return false;
        if(!Explain("edge preview replaces its owned shape",kernel::CachedShapeCount()==edgeCount))return false;}
    w.SelectTool(modeling::DrawingTool::Select);
    if(!Explain("edge cancel releases preview",kernel::CachedShapeCount()+1==edgeCount))return false;
    w.RunCommand("file.new");w.SetMode(app::UiMode::Drawing);
    if(!Explain("edge fixture can be reopened",w.ApplyToolShotState(QStringLiteral("ui-tool-fillet"))))return false;
    w.EdgeFinishTool().Confirm();
    if(!Explain("committed edge shape survives selection callbacks and save",committedExports()
        && w.SaveAndReopen(QStringLiteral("tool-contract-committed.kcd2")) && committedExports()))return false;
    const auto beforeWindow=kernel::CachedShapeCount();
    {
        V2MainWindow temporary;temporary.show();QApplication::processEvents();
        if(!Explain("transient preview built",temporary.ApplyToolShotState(QStringLiteral("ui-tool-revolve"))))return false;
        if(!Explain("transient owns one native preview",kernel::CachedShapeCount()==beforeWindow+1))return false;
    }
    return Explain("window destruction releases uncommitted preview",kernel::CachedShapeCount()==beforeWindow);
}
}
std::vector<SelfTestCase> ToolContractCases(){std::vector<SelfTestCase> cases={{"HP-TOOLS all catalog toolbar activations have input or reason",ActivationAudit},{"HP-TOOLS distinct tool illustrations",Illustrations},{"HP-TOOLS exclusive tool transition matrix and shortcuts",Lifecycle},
    {"HP-TOOLS modal dialog keys stay in dialog",ModalKeys},{"HP-TOOLS preview shape ownership stays bounded",PreviewLifetime},{"HP-TOOLS alternate shortcut routes preserve inputs",AlternateShortcutRoutes},{"HP-TOOLS alternate view routes preserve pending targets",AlternateViewDuringInput},
    {"HP-TOOLS first drawing point Undo without document history",AlternateFirstPointUndo},{"HP-TOOLS save and save-as preserve pending targets",AlternateSaveDuringInput}};
    if(qEnvironmentVariableIsSet("KACHACAD_MODELING_OWNER_INPUT"))cases.push_back({"HP-TOOLS actual owner model thickness and history",AlternateOwnerModel});
    return cases;}
}
