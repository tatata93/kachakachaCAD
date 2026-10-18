#include <QChar>
#include <QEvent>
#include <QEventLoop>
#include <QPointF>
#include <QWidget>
#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2InstructionMode.h"
#include "V2Viewport.h"
#include <QApplication>
#include <QAbstractButton>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QPixmap>
#include <QStyle>
#include <QFont>
#include <QString>
#include <algorithm>
namespace kachakacha::v2::selftest {
namespace {
V2InstructionMode* Editor(V2MainWindow& window){for(auto* child:window.centralWidget()->children())if(auto* m=dynamic_cast<V2InstructionMode*>(child))return m;return nullptr;}
template<class T>T* Visible(V2MainWindow& w,const char* name){for(auto* v:w.findChildren<T*>(name))if(v->isVisible())return v;return nullptr;}
void Key(QWidget* w,int key,Qt::KeyboardModifiers mods=Qt::NoModifier,const QString& text={}){QKeyEvent e(QEvent::KeyPress,key,mods,text);QApplication::sendEvent(w,&e);QApplication::processEvents();}
void Click(QWidget* w,QPointF at){for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent e(type,at,at,Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(w,&e);}QApplication::processEvents();}
void Type(QPlainTextEdit* edit,const QString& text){edit->setFocus();edit->selectAll();Key(edit,0,Qt::NoModifier,text);}
QJsonArray Marks(V2InstructionMode& m){const auto s=m.Snapshot();return s["pages"].toArray()[s["current"].toInt()].toObject()["marks"].toArray();}
QJsonArray Items(V2InstructionMode& m){const auto s=m.Snapshot();return s["sheets"].toArray()[s["currentSheet"].toInt()].toObject()["items"].toArray();}
void Capture(V2MainWindow& w,const char* name){const auto output=qEnvironmentVariable("KACHACAD_INSTRUCTION_PROOF_DIR");if(output.isEmpty())return;QEventLoop ready;QTimer::singleShot(160,&ready,&QEventLoop::quit);ready.exec();QDir().mkpath(output);w.grab().save(output+"/"+name+".png");}
void ConfirmPageDelete(V2InstructionMode& mode,bool accept){QTimer::singleShot(50,[accept]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))box->button(accept?QMessageBox::Yes:QMessageBox::Cancel)->click();});
    mode.Command("instructions.delete_page");QApplication::processEvents();}
void NewWithAnswer(V2MainWindow& window,QMessageBox::StandardButton answer){QTimer::singleShot(50,[answer]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))box->button(answer)->click();});window.RunCommand("file.new");QApplication::processEvents();}
}
bool InstructionWorkspaceWorkflow(V2MainWindow& window)
{
    QTemporaryDir folder;window.resize(1500,1000);window.show();window.SetMode(app::UiMode::Instructions);QApplication::processEvents();
    auto* mode=Editor(window);if(!mode)return false;
    const auto path=folder.filePath("workspace.kci");struct Cleanup{V2InstructionMode* mode;V2MainWindow& window;QString path;~Cleanup(){mode->Save(path);V2InstructionMode::SetPathChooser(window,[](bool){return QString();});}}cleanup{mode,window,path};if(!mode->Save(path))return false;
    window.RunCommand("file.new");QApplication::processEvents();const auto revision=window.Session().GetDocument().Revision();
    if(!Explain("new instruction retains one scene and one sheet",mode->Snapshot()["pages"].toArray().size()==1&&mode->Snapshot()["sheets"].toArray().size()==1))return false;
    if(!Explain("empty scene PDF is refused",!mode->ExportPdf(folder.filePath("empty.pdf"))))return false;
    auto* scene=dynamic_cast<V2InstructionScene*>(window.findChild<QWidget*>("instructionCanvas3d"));
    if(!Explain("workspace toolbar visible",scene&&Visible<QToolButton>(window,"instructionAction_instructions.text")&&Visible<QPushButton>(window,"instructionAddPage")))return false;
    mode->Command("instructions.text");auto* text=Visible<QPlainTextEdit>(window,"instructionText");if(!text)return false;Type(text,QStringLiteral("Roof assembly"));
    const auto before=mode->Snapshot();Click(scene,{200,160});
    if(!Explain("scene click takes input focus",scene->hasFocus()&&Marks(*mode).size()==1&&Marks(*mode)[0].toObject()["text"]=="Roof assembly"))return false;
    Key(scene,Qt::Key_Z,Qt::ControlModifier);
    if(!Explain("real Ctrl Z undoes placement instead of editing text",mode->Snapshot()==before))return false;
    Key(scene,Qt::Key_Y,Qt::ControlModifier);scene->SetSelectedMark(0);mode->Command("instructions.text");mode->Command("instructions.remove");
    text=Visible<QPlainTextEdit>(window,"instructionText");if(!text)return false;const auto draft=text->toPlainText();Click(scene,{200,160});
    if(!Explain("recreated input agrees with placed text",Marks(*mode).size()==1&&Marks(*mode)[0].toObject()["text"].toString()==draft))return false;
    mode->Command("instructions.move");auto* marks=Visible<QComboBox>(window,"instructionMarks");if(!marks)return false;marks->setCurrentIndex(1);
    text=Visible<QPlainTextEdit>(window,"instructionSelectedText");auto* apply=Visible<QPushButton>(window,"instructionApplySelectedText");if(!text||!apply)return false;
    Type(text,QStringLiteral("Attach the roof"));apply->click();
    if(!Explain("selection inspector edits text without another placement",Marks(*mode).size()==1&&Marks(*mode)[0].toObject()["text"]=="Attach the roof"))return false;
    scene->setFocus();Key(scene,Qt::Key_Delete);
    if(!Explain("real Delete removes selected annotation",Marks(*mode).isEmpty()))return false;
    Key(scene,Qt::Key_Z,Qt::ControlModifier);if(Marks(*mode).size()!=1)return false;
    window.RunCommand("view.fit_all");if(!Explain("fit includes annotations without parts",mode->Snapshot()["pages"].toArray()[0].toObject()["span"].toDouble()<100))return false;
    if(!Explain("scene PDF has content without changing paper document",mode->ExportPdf(folder.filePath("scene.pdf"))&&QFile(folder.filePath("scene.pdf")).size()>2000&&mode->Snapshot()["sheets"].toArray()[0].toObject()["items"].toArray().isEmpty()))return false;
    Capture(window,"instruction-scene-inspector");
    mode->Command("instructions.sheet_editor");mode->Command("instructions.place_scene");
    auto* sheet=dynamic_cast<V2InstructionSheetView*>(window.findChild<QWidget*>("instructionPaperCanvas"));if(!sheet)return false;Click(sheet,sheet->ToScreen({20,20}));
    if(!Explain("scene placement is a 3D UUID reference",Items(*mode).size()==1&&Items(*mode)[0].toObject()["kind"]=="scene"&&!Items(*mode)[0].toObject().contains("imageData")))return false;
    mode->Command("instructions.text");text=Visible<QPlainTextEdit>(window,"instructionText");if(!text)return false;Type(text,"Step 1");Click(sheet,sheet->ToScreen({20,100}));
    mode->Command("instructions.remove");text=Visible<QPlainTextEdit>(window,"instructionText");if(!text)return false;const auto paperDraft=text->toPlainText();Click(sheet,sheet->ToScreen({20,100}));
    if(!Explain("paper draft and placed text agree after deletion",Items(*mode).last().toObject()["text"].toString()==paperDraft))return false;
    mode->Command("instructions.move");sheet->SetSelected(1);text=Visible<QPlainTextEdit>(window,"instructionSelectedText");apply=Visible<QPushButton>(window,"instructionApplySelectedText");if(!text||!apply)return false;
    Type(text,"1. Attach the roof\n2. Fit the sides");apply->click();Capture(window,"instruction-paper-inspector");
    if(!mode->Save(path))return false;int pathRequests=0;V2InstructionMode::SetPathChooser(window,[&](bool){++pathRequests;return folder.filePath("copy.kci");});
    window.RunCommand("file.save");if(!Explain("Ctrl S reuses path",pathRequests==0))return false;
    window.RunCommand("file.save_as");if(!Explain("Save As chooses a separate file",pathRequests==1&&QFile::exists(folder.filePath("copy.kci"))))return false;
    const auto saved=mode->Snapshot();if(!mode->Load(folder.filePath("copy.kci"))||mode->Snapshot()!=saved)return false;
    mode->Command("instructions.scene_editor");const auto sceneAndReference=mode->Snapshot();ConfirmPageDelete(*mode,false);
    if(!Explain("page deletion cancel retains references",mode->Snapshot()==sceneAndReference))return false;
    ConfirmPageDelete(*mode,true);
    if(!Explain("delete scene also removes its placements",mode->Snapshot()["pages"].toArray().size()==1&&mode->Snapshot()["sheets"].toArray()[0].toObject()["items"].toArray().size()==1))return false;
    window.RunCommand("edit.undo");if(!Explain("scene and placements undo together",mode->Snapshot()==sceneAndReference))return false;
    window.RunCommand("edit.redo");window.RunCommand("edit.undo");
    mode->Command("instructions.sheet_editor");mode->Command("instructions.duplicate");ConfirmPageDelete(*mode,true);
    if(!Explain("duplicated paper can be removed",mode->Snapshot()["sheets"].toArray().size()==1))return false;
    const auto limited=mode->Snapshot();mode->Command("instructions.text");text=Visible<QPlainTextEdit>(window,"instructionText");if(!text)return false;
    text->setPlainText(QString(100001,QChar('A')));Click(sheet,sheet->ToScreen({20,150}));
    if(!Explain("oversized paragraph placement does not change document",mode->Snapshot()==limited))return false;
    mode->Command("instructions.move");sheet->SetSelected(1);text=Visible<QPlainTextEdit>(window,"instructionSelectedText");apply=Visible<QPushButton>(window,"instructionApplySelectedText");if(!text||!apply)return false;
    text->setPlainText(QString(100001,QChar('A')));apply->click();
    if(!Explain("oversized paragraph edit does not change document",mode->Snapshot()==limited))return false;
    mode->Command("instructions.text");text=Visible<QPlainTextEdit>(window,"instructionText");if(!text)return false;text->setPlainText(QString(100000,QChar('A')));Click(sheet,sheet->ToScreen({20,150}));
    if(!Explain("maximum allowed paragraph is placed",Items(*mode).last().toObject()["text"].toString().size()==100000))return false;
    if(!mode->Save(path)||!mode->Load(path))return false;
    if(!mode->Restore(limited)||!mode->Save(path))return false;
    mode->Command("instructions.duplicate");const auto unsaved=mode->Snapshot();NewWithAnswer(window,QMessageBox::Cancel);
    if(!Explain("new instruction cancellation preserves all pages",mode->Snapshot()==unsaved))return false;
    NewWithAnswer(window,QMessageBox::Discard);
    if(!Explain("new instruction can discard changes without touching CAD",mode->Snapshot()["pages"].toArray().size()==1&&mode->Snapshot()["sheets"].toArray().size()==1&&Items(*mode).isEmpty()))return false;
    mode->Command("instructions.duplicate");const auto saveBeforeNew=folder.filePath("saved-before-new.kci");V2InstructionMode::SetPathChooser(window,[saveBeforeNew](bool){return saveBeforeNew;});NewWithAnswer(window,QMessageBox::Save);
    if(!Explain("new instruction can save the previous document",QFile::exists(saveBeforeNew)&&mode->Snapshot()["pages"].toArray().size()==1))return false;
    V2InstructionMode::SetPathChooser(window,[](bool){return QString();});window.SetMode(app::UiMode::Part);
    return Explain("instruction workflow does not modify CAD",window.Session().GetDocument().Revision()==revision);
}
bool InstructionCaptureWorkflow(V2MainWindow& window)
{
    window.SetMode(app::UiMode::Part);window.RunCommand("file.new");
    const auto line=geometry::CurveSegment::MakeLine({0,0,0},{20,10,5});
    const auto circle=geometry::CurveSegment::MakeCircle({10,10,0},{0,0,1},{1,0,0},4);
    if(!line.HasValue()||!circle.HasValue()||!window.Session().AddWire({line.Value()},false,"Line").committed||!window.Session().AddWire({circle.Value()},false,"Circle").committed)return false;
    QTemporaryDir folder;const auto modelPath=folder.filePath("wires.kcd2");window.SetPathChooser([modelPath](bool){return modelPath;});window.RunCommand("file.save_as");window.SetPathChooser([](bool){return QString();});
    window.SetMode(app::UiMode::Instructions);auto* mode=Editor(window);if(!mode)return false;
    struct Cleanup{V2InstructionMode* mode;QString path;~Cleanup(){mode->Save(path);}}cleanup{mode,folder.filePath("cleanup.kci")};
    if(!mode->Save(cleanup.path))return false;window.RunCommand("file.new");
    window.ApplyTheme(UiTheme::Normal);const auto font=QApplication::font();const auto style=QApplication::style()->objectName();
    if(!Explain("wire-only CAD can be read into instruction editor",mode->ReadModel(modelPath)))return false;
    if(!Explain("reading model preserves application theme",window.Theme()==UiTheme::Normal&&QApplication::font()==font&&QApplication::style()->objectName()==style))return false;
    auto* list=Visible<QListWidget>(window,"instructionModelParts");if(!list||list->count()!=2)return false;
    list->item(1)->setCheckState(Qt::Unchecked);list->setCurrentRow(0);
    window.SetMode(app::UiMode::Part);window.SetMode(app::UiMode::Instructions);QApplication::processEvents();
    list=Visible<QListWidget>(window,"instructionModelParts");
    if(!Explain("picker retains check state and current row across mode changes",list&&list->item(0)->checkState()==Qt::Checked&&list->item(1)->checkState()==Qt::Unchecked&&list->currentRow()==0))return false;
    mode->AcceptParts();const auto saved=mode->Snapshot();
    if(!Explain("only checked wire is captured as editable 3D scene asset",saved["pages"].toArray()[0].toObject()["parts"].toArray().size()==1&&saved["assets"].toArray().size()==1&&saved["assets"].toArray()[0].toObject()["triangles"].toArray().isEmpty()&&!saved["assets"].toArray()[0].toObject()["edges"].toArray().isEmpty()))return false;
    if(!mode->Save(cleanup.path)||!mode->Load(cleanup.path)||mode->Snapshot()!=saved)return false;
    auto* view=dynamic_cast<V2InstructionScene*>(window.findChild<QWidget*>("instructionCanvas3d"));if(!view)return false;
    const auto image=view->Render({640,480});int ink=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(qGray(image.pixel(x,y))<200)++ink;
    if(!Explain("wire-only instruction scene renders visible outlines",ink>40))return false;
    if(!mode->Save(cleanup.path))return false;window.RunCommand("file.new");
    std::vector<V2Viewport::ShapeView> many;
    modeling::MeshTriangle triangle;triangle.points={geometry::Vector3{0,0,0},geometry::Vector3{1,0,0},geometry::Vector3{0,1,0}};
    const auto entity=window.Session().Scene().curves.front().entityId;
    for(int i=0;i<128;++i){V2Viewport::ShapeView shape;shape.entityId=entity;shape.mesh.triangles.push_back(triangle);many.push_back(shape);}
    window.Viewport().SetShapeViews(std::move(many));mode->Capture();list=Visible<QListWidget>(window,"instructionModelParts");
    if(!Explain("128 surfaces plus wires can enter candidate selection",list&&list->count()==130))return false;
    mode->AcceptParts();
    if(!Explain("candidate count does not prevent selecting 128 surfaces",mode->Snapshot()["pages"].toArray()[0].toObject()["parts"].toArray().size()==128))return false;
    if(!mode->Save(cleanup.path)||!mode->Load(cleanup.path))return false;window.Viewport().SetShapeViews({});
    window.ApplyTheme(UiTheme::Windows95);
    const auto actual=qEnvironmentVariable("KACHACAD_INSTRUCTION_ACCEPTANCE_MODEL");
    if(!actual.isEmpty()){
        window.SetMode(app::UiMode::Part);if(!window.OpenDocumentFile(actual))return false;
        window.SetMode(app::UiMode::Instructions);if(!mode->Save(cleanup.path))return false;window.RunCommand("file.new");mode->Capture();
        list=Visible<QListWidget>(window,"instructionModelParts");if(!list||list->count()<1)return false;
        mode->AcceptParts();
        mode->Command("instructions.arrow");Click(view,{140,140});Click(view,{260,220});mode->Command("instructions.move");Capture(window,"instruction-real-model-scene");
        mode->Command("instructions.duplicate");mode->Command("instructions.text");
        auto* step=Visible<QPlainTextEdit>(window,"instructionText");if(!step)return false;step->setPlainText("2");Click(view,{220,100});mode->Command("instructions.move");Capture(window,"instruction-real-model-scene");
        mode->Command("instructions.sheet_editor");mode->Command("instructions.place_scene");auto* paper=dynamic_cast<V2InstructionSheetView*>(window.findChild<QWidget*>("instructionPaperCanvas"));if(!paper)return false;
        auto* scenes=Visible<QListWidget>(window,"instructionSceneList");if(!scenes)return false;
        scenes->setCurrentRow(0);Click(paper,paper->ToScreen({15,15}));scenes->setCurrentRow(1);Click(paper,paper->ToScreen({110,15}));mode->Command("instructions.move");Capture(window,"instruction-real-model-paper");
        const auto output=qEnvironmentVariable("KACHACAD_INSTRUCTION_PROOF_DIR");if(!output.isEmpty()&&(!mode->Save(output+"/example.kci")||!mode->ExportPdf(output+"/example.pdf")))return false;
    }
    if(!mode->Save(cleanup.path))return false;window.SetMode(app::UiMode::Part);return true;
}

}
