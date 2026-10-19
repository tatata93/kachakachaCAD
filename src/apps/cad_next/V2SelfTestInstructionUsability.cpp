#include <QWidget>
#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2InstructionMode.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QImage>
#include <QFontComboBox>
#include <QGuiApplication>
#include <iostream>
#include <QPointer>
#include <QColor>
#include <QPoint>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLineF>
#include <QListWidget>
#include <QMetaObject>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPointF>
#include <QPushButton>
#include <QRectF>
#include <QSpinBox>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>
#include <cmath>

namespace kachakacha::v2::selftest {
namespace {
template<class T>T* Shown(V2MainWindow& w,const char* name){for(auto* item:w.findChildren<T*>(name))if(item->isVisible())return item;return nullptr;}
V2InstructionMode* Editor(V2MainWindow& w){for(auto* child:w.centralWidget()->children())if(auto* mode=dynamic_cast<V2InstructionMode*>(child))return mode;return nullptr;}
void Mouse(QWidget* w,QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons held){if(!w->window()->isActiveWindow()){w->window()->activateWindow();QApplication::processEvents();}QMouseEvent e(type,point,point,button,held,Qt::NoModifier);QApplication::sendEvent(w,&e);QApplication::processEvents();}
void Click(QWidget* w,QPointF point){Mouse(w,QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton);Mouse(w,QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton);}
void Key(QWidget* w,int key,Qt::KeyboardModifiers modifiers=Qt::NoModifier){QKeyEvent press(QEvent::KeyPress,key,modifiers);QApplication::sendEvent(w,&press);QKeyEvent release(QEvent::KeyRelease,key,modifiers);QApplication::sendEvent(w,&release);QApplication::processEvents();}
QJsonArray Items(V2InstructionMode& m){const auto s=m.Snapshot();return s["sheets"].toArray()[s["currentSheet"].toInt()].toObject()["items"].toArray();}
QRectF Rect(const QJsonObject& item){const auto a=item["rect"].toArray();return {a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble()};}
bool Press(V2MainWindow& w,const char* name){auto* button=Shown<QPushButton>(w,name);if(!button)return false;button->click();QApplication::processEvents();return true;}
void Capture(V2MainWindow& w,const char* name){const auto root=qEnvironmentVariable("KACHACAD_INSTRUCTION_PROOF_DIR");if(root.isEmpty())return;QDir().mkpath(root);QApplication::processEvents();w.grab().save(root+"/"+name+".png");}
struct Cleanup{V2InstructionMode* mode;QString path;~Cleanup(){mode->Save(path);}};
}
bool InstructionCancelAndSettings(V2MainWindow& w,V2InstructionMode& mode,V2InstructionSheetView* paper) {
    QTemporaryDir folder;const auto path=folder.filePath("cancel.kci");mode.Command("instructions.move");
    if(!mode.Save(path))return false;const auto original=mode.Snapshot();
    for(bool aspect:{false,true})for(int h=0;h<8;++h) {
        if(!mode.Load(path))return false;paper->SetSelected(0);paper->SetKeepAspect(aspect);
        const auto r=Rect(Items(mode)[0].toObject());const QPointF points[]={r.topLeft(),{r.center().x(),r.top()},r.topRight(),
            {r.right(),r.center().y()},r.bottomRight(),{r.center().x(),r.bottom()},r.bottomLeft(),{r.left(),r.center().y()}};
        const int dx[]={-1,0,1,1,1,0,-1,-1},dy[]={-1,-1,-1,0,1,1,1,0};const auto end=points[h]+QPointF(dx[h]*6,dy[h]*4);
        Mouse(paper,QEvent::MouseButtonPress,paper->ToScreen(points[h]),Qt::LeftButton,Qt::LeftButton);
        Mouse(paper,QEvent::MouseMove,paper->ToScreen(end),Qt::NoButton,Qt::LeftButton);
        if(!Explain("resize has a real preview to cancel",mode.Snapshot()!=original))return false;
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(paper,&escape);QApplication::processEvents();
        if(!Explain("Escape restores the frame before rebuilding the inspector",mode.Snapshot()==original))return false;
        const char* names[]={"instructionItemX","instructionItemY","instructionItemWidth","instructionItemHeight"};const double values[]={r.x(),r.y(),r.width(),r.height()};
        for(int i=0;i<4;++i){auto* input=Shown<QDoubleSpinBox>(w,names[i]);if(!Explain("cancelled inspector agrees with the actual frame",input&&std::abs(input->value()-values[i])<.001))return false;}
        Mouse(paper,QEvent::MouseButtonRelease,paper->ToScreen(end),Qt::LeftButton,Qt::NoButton);
        if(!Press(w,"instructionApplyItem")||!Explain("applying after cancel cannot reapply the cancelled preview",mode.Snapshot()==original))return false;
        w.RunCommand("edit.undo");if(!Explain("cancel and unchanged apply create no Undo entry",mode.Snapshot()==original))return false;
    }
    return mode.Load(path);
}
bool InstructionInlineFocus(V2MainWindow& w,V2InstructionMode& mode,V2InstructionSheetView* paper) {
    QTemporaryDir folder;const auto path=folder.filePath("focus.kci");if(!mode.Save(path))return false;
    const auto original=mode.Snapshot();const QJsonValue originalFont=Items(mode)[0].toObject()["font"];
    for(const char* tool:{"instructions.image","instructions.paper"}) {
        if(!mode.Load(path))return false;mode.Command(tool);paper->SetSelected(0);
        Mouse(paper,QEvent::MouseButtonDblClick,paper->ToScreen({25,25}),Qt::LeftButton,Qt::LeftButton);
        auto* edit=Shown<QPlainTextEdit>(w,"instructionInlineText");
        if(!Explain("double click from output or paper settings exposes text and font controls",edit
            &&Shown<QFontComboBox>(w,"instructionFont")&&Shown<QPlainTextEdit>(w,"instructionSelectedText")))return false;
        edit->setPlainText("Cancelled alternate-entry draft");
        QKeyEvent cancel(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(edit,&cancel);QApplication::processEvents();
        if(!Explain("alternate-entry Escape preserves the saved paragraph",mode.Snapshot()==original))return false;
    }
    for(bool fontTarget:{true,false}) {
        if(!mode.Load(path))return false;paper->SetSelected(0);
        Mouse(paper,QEvent::MouseButtonDblClick,paper->ToScreen({25,25}),Qt::LeftButton,Qt::LeftButton);
        auto* edit=Shown<QPlainTextEdit>(w,"instructionInlineText");if(!Explain("inline editor opens before inspector focus test",edit!=nullptr))return false;edit->setPlainText("Inline B");
        QPointer<QWidget> target=fontTarget?static_cast<QWidget*>(Shown<QFontComboBox>(w,"instructionFont")):Shown<QPlainTextEdit>(w,"instructionSelectedText");
        if(!Explain("inspector target exists before focus transfer",target!=nullptr))return false;target->setFocus(Qt::MouseFocusReason);QApplication::processEvents();
        if(!Explain("moving to the inspector keeps the same focused widget",target&&target->isVisible()&&target->hasFocus()
            &&Items(mode)[0].toObject()["text"]=="Inline B"))return false;
        if(fontTarget){
            auto* font=qobject_cast<QFontComboBox*>(target.data());if(!Explain("the focused inspector is the font family control",font!=nullptr))return false;
            std::cout<<"font families on "<<QGuiApplication::platformName().toStdString()<<"="<<font->count()<<std::endl;
            if(QGuiApplication::platformName()=="windows"&&!Explain("Windows lists real font families",font->count()>=2))return false;
            if(font->count()>=2)font->setCurrentIndex((font->currentIndex()+1)%font->count());
            else {auto* bold=Shown<QCheckBox>(w,"instructionTextBold");if(!bold)return false;bold->setChecked(!bold->isChecked());}
        }
        else {auto* text=qobject_cast<QPlainTextEdit*>(target.data());if(!text)return false;text->setPlainText("Inspector C");if(!mode.Save(folder.filePath("edited.kci")))return false;}
        w.RunCommand("edit.undo");QApplication::processEvents();
        if(!Explain("inspector change Undo preserves the committed inline text",Items(mode)[0].toObject()["text"]=="Inline B"
            &&Items(mode)[0].toObject()["font"]==originalFont))return false;
        w.RunCommand("edit.undo");QApplication::processEvents();
        if(!Explain("text Undo and pending focus callbacks preserve the restored document",mode.Snapshot()==original))return false;
    }
    return mode.Load(path);
}
bool InstructionResizeBounds() {
    InstructionSheet sheet;std::vector<InstructionPage> scenes;InstructionSheetItem item;item.kind=InstructionItemKind::Image;
    item.image=QImage(2,2,QImage::Format_RGB32);item.image.fill(Qt::blue);sheet.items.push_back(item);
    V2InstructionSheetView canvas;canvas.resize(900,700);canvas.SetDocument(&sheet,&scenes);
    for(const auto initial:{QRectF(-100,15,10,10),QRectF(15,-100,10,10),QRectF(100000,100000,100000,100000),
        QRectF(-100000,-100000,.01,.01),QRectF(20,20,80,.02),QRectF(20,20,.02,80)})for(bool aspect:{false,true})for(int h=0;h<8;++h) {
        sheet.items[0].rectMm=initial;canvas.SetSelected(0);canvas.SetKeepAspect(aspect);
        const QPointF points[]={initial.topLeft(),{initial.center().x(),initial.top()},initial.topRight(),
            {initial.right(),initial.center().y()},initial.bottomRight(),{initial.center().x(),initial.bottom()},initial.bottomLeft(),{initial.left(),initial.center().y()}};
        const auto start=canvas.ToScreen(points[h]),finish=start+QPointF(h<4?-80:80,h%2?-60:60);
        Mouse(&canvas,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
        Mouse(&canvas,QEvent::MouseMove,finish,Qt::NoButton,Qt::LeftButton);
        Mouse(&canvas,QEvent::MouseButtonRelease,finish,Qt::LeftButton,Qt::NoButton);
        const auto r=sheet.items[0].rectMm;std::vector<InstructionSheet> decoded;
        if(!Explain("eight resize handles keep negative huge and thin frames reloadable",r.width()>=.01&&r.width()<=100000
            &&r.height()>=.01&&r.height()<=100000&&r.x()>=-100000&&r.x()<=100000&&r.y()>=-100000&&r.y()<=100000
            &&DecodeInstructionSheets(EncodeInstructionSheets({sheet}),scenes,decoded)&&decoded[0].items[0].rectMm==r))return false;
        if(aspect&&!Explain("extreme-frame resize retains aspect",std::abs(r.height()/r.width()-initial.height()/initial.width())<1e-6))return false;
    }
    return true;
}
bool InstructionInlineKeys(V2MainWindow& w,V2InstructionMode& mode,V2InstructionSheetView* paper) {
    QTemporaryDir folder;const auto path=folder.filePath("keys.kci");if(!mode.Save(path))return false;
    const auto original=mode.Snapshot();const auto revision=w.Session().GetDocument().Revision();
    paper->SetSelected(0);Mouse(paper,QEvent::MouseButtonDblClick,paper->ToScreen({25,25}),Qt::LeftButton,Qt::LeftButton);
    auto* edit=Shown<QPlainTextEdit>(w,"instructionInlineText");if(!edit)return false;
    edit->setPlainText("Keyboard baseline");edit->selectAll();edit->insertPlainText("Keyboard replacement");
    Key(edit,Qt::Key_Z,Qt::ControlModifier);
    if(!Explain("inline Ctrl+Z changes the draft, preserving the paper item and CAD history",edit->toPlainText()=="Keyboard baseline"
        &&Items(mode).size()==1&&mode.Snapshot()==original&&w.Session().GetDocument().Revision()==revision))return false;
    edit->selectAll();Key(edit,Qt::Key_Delete);
    if(!Explain("inline Delete removes selected characters, not the paper item",edit->toPlainText().isEmpty()&&Items(mode).size()==1))return false;
    Key(edit,Qt::Key_Z,Qt::ControlModifier);Key(edit,Qt::Key_Return,Qt::ControlModifier);
    if(!Explain("inline Ctrl+Enter commits the paragraph and returns to paper selection",!edit->isVisible()
        &&Items(mode)[0].toObject()["text"]=="Keyboard baseline"&&paper->hasFocus()))return false;
    Key(paper,Qt::Key_Z,Qt::ControlModifier);
    if(!Explain("paper Ctrl+Z undoes the committed paragraph without changing CAD",mode.Snapshot()==original
        &&w.Session().GetDocument().Revision()==revision))return false;
    paper->SetSelected(0);Mouse(paper,QEvent::MouseButtonDblClick,paper->ToScreen({25,25}),Qt::LeftButton,Qt::LeftButton);
    edit=Shown<QPlainTextEdit>(w,"instructionInlineText");if(!edit)return false;edit->setPlainText("Keyboard saved paragraph");
    Key(edit,Qt::Key_S,Qt::ControlModifier);
    if(!Explain("inline Ctrl+S commits the draft before saving KCI",!edit->isVisible()&&Items(mode)[0].toObject()["text"]=="Keyboard saved paragraph"))return false;
    if(!mode.Load(path)||!Explain("Ctrl+S saved the paragraph to disk and preserved CAD",Items(mode)[0].toObject()["text"]=="Keyboard saved paragraph"
        &&w.Session().GetDocument().Revision()==revision))return false;
    return mode.Restore(original);
}
bool InstructionLayoutUsability(V2MainWindow& w) {
    if(!InstructionResizeBounds())return false;
    QTemporaryDir folder;w.resize(1550,1000);w.show();w.SetMode(app::UiMode::Instructions);QApplication::processEvents();
    auto* mode=Editor(w);if(!mode)return false;Cleanup cleanup{mode,folder.filePath("layout.kci")};
    const auto revision=w.Session().GetDocument().Revision();mode->Command("instructions.text");
    auto* text=Shown<QPlainTextEdit>(w,"instructionText");auto* scene=Shown<QWidget>(w,"instructionCanvas3d");if(!text||!scene)return false;
    text->setPlainText(QStringLiteral("屋根を取り付ける"));Click(scene,{180,140});mode->Command("instructions.duplicate");
    mode->Command("instructions.sheet_editor");auto* paper=dynamic_cast<V2InstructionSheetView*>(Shown<QWidget>(w,"instructionPaperCanvas"));
    auto* list=Shown<QListWidget>(w,"instructionSceneList");if(!paper||!list)return false;
    list->setCurrentRow(0);Click(list->viewport(),list->visualItemRect(list->item(0)).center());Click(paper,paper->ToScreen({20,20}));
    if(!Explain("choosing a scene while selecting does not insert a figure",Items(*mode).isEmpty()))return false;
    if(!Press(w,"instructionBrowserPlace"))return false;Click(paper,paper->ToScreen({20,20}));mode->Command("instructions.move");paper->SetSelected(0);
    const auto placed=mode->Snapshot();const auto point=paper->ToScreen({60,70});
    auto* zoom=Shown<QSpinBox>(w,"instructionPaperZoom");if(!zoom)return false;zoom->setValue(200);
    if(!Explain("paper zoom is view-only",paper->Zoom()==2&&mode->Snapshot()==placed))return false;
    const auto beforePan=paper->ToScreen({60,70});
    Mouse(paper,QEvent::MouseButtonPress,{200,200},Qt::MiddleButton,Qt::MiddleButton);
    Mouse(paper,QEvent::MouseMove,{240,230},Qt::NoButton,Qt::MiddleButton);
    Mouse(paper,QEvent::MouseButtonRelease,{240,230},Qt::MiddleButton,Qt::NoButton);
    if(!Explain("middle drag pans exactly without moving paper items",QLineF(paper->ToScreen({60,70}),beforePan+QPointF(40,30)).length()<1e-6&&mode->Snapshot()==placed))return false;
    const auto anchor=paper->ToScreen({60,70});QWheelEvent wheel(anchor,anchor,{},QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(paper,&wheel);
    if(!Explain("wheel keeps the cursor's paper coordinate fixed",paper->Zoom()>2&&QLineF(anchor,paper->ToScreen({60,70})).length()<1e-6))return false;
    if(!Press(w,"instructionPaperFit")||!Explain("fit restores navigation without changing the document",paper->Zoom()==1&&QLineF(point,paper->ToScreen({60,70})).length()<1e-6&&mode->Snapshot()==placed))return false;
    Capture(w,"instruction-layout-navigation");
    if(!Press(w,"instructionItem_duplicate"))return false;const auto duplicated=mode->Snapshot();
    if(!Explain("figure duplicate keeps its scene reference and gets a new identity",Items(*mode).size()==2&&Items(*mode)[0].toObject()["id"]!=Items(*mode)[1].toObject()["id"]&&Items(*mode)[0].toObject()["sceneId"]==Items(*mode)[1].toObject()["sceneId"]))return false;
    const QJsonValue second=Items(*mode)[1].toObject()["id"];
    if(!Press(w,"instructionItem_back")||!Explain("send backward changes only stacking order",Items(*mode)[0].toObject()["id"]==second))return false;
    w.RunCommand("edit.undo");if(!Explain("stacking order is one atomic Undo",mode->Snapshot()==duplicated))return false;w.RunCommand("edit.redo");
    paper->SetSelected(0);auto* align=Shown<QComboBox>(w,"instructionItemAlign");if(!align)return false;align->setCurrentIndex(1);
    if(!Press(w,"instructionAlignApply")||!Explain("align uses paper millimeters",std::abs(Rect(Items(*mode)[0].toObject()).center().x()-105)<1e-6))return false;
    auto* aspect=Shown<QCheckBox>(w,"instructionKeepAspect");auto* width=Shown<QDoubleSpinBox>(w,"instructionItemWidth");auto* height=Shown<QDoubleSpinBox>(w,"instructionItemHeight");
    if(!aspect||!width||!height)return false;aspect->setChecked(true);const auto old=Rect(Items(*mode)[0].toObject());width->setValue(100);QMetaObject::invokeMethod(width,"editingFinished",Qt::DirectConnection);
    if(!Press(w,"instructionApplyItem")||!Explain("persistent aspect setting applies to numeric dimensions",std::abs(Rect(Items(*mode)[0].toObject()).height()-100*old.height()/old.width())<.002))return false;
    paper->setFocus();const auto beforeKey=Rect(Items(*mode)[0].toObject());QKeyEvent left(QEvent::KeyPress,Qt::Key_Left,Qt::NoModifier);QApplication::sendEvent(paper,&left);
    if(!Explain("arrow key moves by one millimeter",std::abs(Rect(Items(*mode)[0].toObject()).x()-beforeKey.x()+1)<1e-6))return false;
    const auto handleStart=mode->Snapshot();aspect=Shown<QCheckBox>(w,"instructionKeepAspect");aspect->setChecked(false);
    for(int handle=0;handle<8;++handle) {
        if(!mode->Restore(handleStart))return false;mode->Command("instructions.move");paper->SetSelected(0);
        const auto r=Rect(Items(*mode)[0].toObject());const QPointF points[]={r.topLeft(),{r.center().x(),r.top()},r.topRight(),
            {r.right(),r.center().y()},r.bottomRight(),{r.center().x(),r.bottom()},r.bottomLeft(),{r.left(),r.center().y()}};
        const int dx[]={-1,0,1,1,1,0,-1,-1},dy[]={-1,-1,-1,0,1,1,1,0};
        const auto finish=points[handle]+QPointF(dx[handle]*3,dy[handle]*3);
        const auto before=mode->Snapshot();Mouse(paper,QEvent::MouseButtonPress,paper->ToScreen(points[handle]),Qt::LeftButton,Qt::LeftButton);
        Mouse(paper,QEvent::MouseMove,paper->ToScreen(finish),Qt::NoButton,Qt::LeftButton);
        Mouse(paper,QEvent::MouseButtonRelease,paper->ToScreen(finish),Qt::LeftButton,Qt::NoButton);
        const auto result=Rect(Items(*mode)[0].toObject());
        if(!Explain("all eight handles resize from the selected edge without moving its opposite",std::abs(result.width()-r.width()-(dx[handle]?3:0))<1e-6
            &&std::abs(result.height()-r.height()-(dy[handle]?3:0))<1e-6
            &&std::abs(result.left()-r.left()-(dx[handle]<0?-3:0))<1e-6&&std::abs(result.top()-r.top()-(dy[handle]<0?-3:0))<1e-6))return false;
        w.RunCommand("edit.undo");if(!Explain("handle drag is exactly one Undo step",mode->Snapshot()==before))return false;
    }
    paper->SetSelected(0);
    const auto handles=Rect(Items(*mode)[0].toObject());const auto selectedImage=paper->Render(paper->size(),true);
    for(const auto mm:{handles.topLeft(),QPointF(handles.center().x(),handles.top()),handles.topRight(),QPointF(handles.right(),handles.center().y()),
        handles.bottomRight(),QPointF(handles.center().x(),handles.bottom()),handles.bottomLeft(),QPointF(handles.left(),handles.center().y())}) {
        const auto px=paper->ToScreen(mm).toPoint();bool blue=false;
        for(int y=-5;y<=5;++y)for(int x=-5;x<=5;++x)if(selectedImage.rect().contains(px+QPoint(x,y))) {
            const auto c=selectedImage.pixelColor(px+QPoint(x,y));blue|=c.red()<60&&c.green()>60&&c.green()<160&&c.blue()>170;
        }
        if(!Explain("all selected handles remain visible above overlapping figures",blue))return false;
    }
    Capture(w,"instruction-layout-arrange");
    const auto contextStart=mode->Snapshot();const auto contextPoint=paper->ToScreen(Rect(Items(*mode)[0].toObject()).center());
    QContextMenuEvent menuEvent(QContextMenuEvent::Mouse,contextPoint.toPoint(),paper->mapToGlobal(contextPoint.toPoint()));QApplication::sendEvent(paper,&menuEvent);QApplication::processEvents();
    auto* menu=paper->findChild<QMenu*>("instructionItemMenu");auto* copy=menu?menu->findChild<QAction*>("instructionContext_duplicate"):nullptr;
    if(!copy)return false;copy->trigger();menu->close();QApplication::processEvents();
    if(!Explain("right click duplicates through the visible item menu",Items(*mode).size()==3))return false;
    w.RunCommand("edit.undo");if(!Explain("context duplicate is undoable",mode->Snapshot()==contextStart))return false;
    mode->Command("instructions.image");auto* output=Shown<QSpinBox>(w,"instructionImageWidth");if(!output)return false;output->setValue(2048);mode->Command("instructions.move");mode->Command("instructions.image");
    if(!Explain("image output dimensions survive a tool switch",Shown<QSpinBox>(w,"instructionImageWidth")->value()==2048))return false;
    if(!InstructionCancelAndSettings(w,*mode,paper))return false;
    if(!mode->Save(cleanup.path))return false;const auto saved=mode->Snapshot();
    return Explain("layout edits round-trip while CAD remains unchanged",mode->Load(cleanup.path)&&mode->Snapshot()==saved&&w.Session().GetDocument().Revision()==revision);
}
bool InstructionTextAndPageUsability(V2MainWindow& w) {
    QTemporaryDir folder;w.resize(1550,1000);w.show();w.SetMode(app::UiMode::Instructions);QApplication::processEvents();auto* mode=Editor(w);if(!mode)return false;
    Cleanup cleanup{mode,folder.filePath("text.kci")};mode->Command("instructions.sheet_editor");mode->Command("instructions.text");
    auto* input=Shown<QPlainTextEdit>(w,"instructionText");auto* paper=dynamic_cast<V2InstructionSheetView*>(Shown<QWidget>(w,"instructionPaperCanvas"));if(!input||!paper)return false;
    input->setPlainText(QStringLiteral("変更前の文章"));Click(paper,paper->ToScreen({20,20}));mode->Command("instructions.move");paper->SetSelected(0);
    auto* edit=Shown<QPlainTextEdit>(w,"instructionSelectedText");if(!edit)return false;edit->setPlainText(QStringLiteral("適用ボタンを押さずに保存した文章"));
    if(!mode->Save(cleanup.path)||!mode->Load(cleanup.path)||!Explain("save commits the selected text draft",Items(*mode)[0].toObject()["text"]==QStringLiteral("適用ボタンを押さずに保存した文章")))return false;
    if(!InstructionInlineFocus(w,*mode,paper))return false;
    if(!InstructionInlineKeys(w,*mode,paper))return false;
    paper->SetSelected(0);edit=Shown<QPlainTextEdit>(w,"instructionSelectedText");if(!edit)return false;edit->setFocus();edit->setPlainText(QStringLiteral("新規作成前の編集中の文章"));
    bool asked=false;QTimer::singleShot(50,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){asked=true;box->button(QMessageBox::Cancel)->click();}});
    w.RunCommand("file.new");if(!Explain("new shortcut commits the focused draft before its save question",asked&&Items(*mode)[0].toObject()["text"]==QStringLiteral("新規作成前の編集中の文章")))return false;
    if(!mode->Load(cleanup.path))return false;
    paper->SetSelected(0);Mouse(paper,QEvent::MouseButtonDblClick,paper->ToScreen({25,25}),Qt::LeftButton,Qt::LeftButton);
    edit=Shown<QPlainTextEdit>(w,"instructionInlineText");if(!Explain("double click edits the paragraph directly on paper",edit&&edit->hasFocus()))return false;
    edit->setPlainText(QStringLiteral("直接編集中の取消"));QKeyEvent cancel(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(edit,&cancel);QApplication::processEvents();
    if(!Explain("Escape discards only the inline draft",Items(*mode)[0].toObject()["text"]==QStringLiteral("適用ボタンを押さずに保存した文章")))return false;
    Mouse(paper,QEvent::MouseButtonDblClick,paper->ToScreen({25,25}),Qt::LeftButton,Qt::LeftButton);edit=Shown<QPlainTextEdit>(w,"instructionInlineText");if(!edit)return false;
    Capture(w,"instruction-inline-text");
    const auto formatStart=mode->Snapshot();const QJsonValue oldFont=Items(*mode)[0].toObject()["font"];
    edit->setPlainText(QStringLiteral("書式変更の直前の本文"));auto* bold=Shown<QCheckBox>(w,"instructionTextBold");if(!bold)return false;
    bold->setChecked(!bold->isChecked());
    w.RunCommand("edit.undo");
    if(!Explain("format Undo preserves the immediately committed inline paragraph",Items(*mode)[0].toObject()["font"]==oldFont
        &&Items(*mode)[0].toObject()["text"]==QStringLiteral("書式変更の直前の本文")))return false;
    w.RunCommand("edit.undo");if(!Explain("a second Undo restores the earlier paragraph and font",mode->Snapshot()==formatStart))return false;
    paper->SetSelected(0);Mouse(paper,QEvent::MouseButtonDblClick,paper->ToScreen({25,25}),Qt::LeftButton,Qt::LeftButton);
    edit=Shown<QPlainTextEdit>(w,"instructionInlineText");if(!edit)return false;
    edit->setPlainText(QStringLiteral("対象を切り替えても残る文章"));mode->Command("instructions.paper");
    if(!Explain("a tool switch commits the text to the original item",Items(*mode)[0].toObject()["text"]==QStringLiteral("対象を切り替えても残る文章")))return false;
    w.RunCommand("edit.undo");if(!Explain("the paragraph edit is one Undo step",Items(*mode)[0].toObject()["text"]==QStringLiteral("適用ボタンを押さずに保存した文章")))return false;w.RunCommand("edit.redo");
    mode->Command("instructions.duplicate");mode->Command("instructions.move");paper->SetSelected(0);
    Mouse(paper,QEvent::MouseButtonDblClick,paper->ToScreen({25,25}),Qt::LeftButton,Qt::LeftButton);
    edit=Shown<QPlainTextEdit>(w,"instructionInlineText");auto* pageList=Shown<QComboBox>(w,"instructionPages");if(!edit||!pageList)return false;
    const int originalPage=pageList->currentIndex();edit->setPlainText(QString());pageList->setCurrentIndex(0);
    if(!Explain("rejected inline input keeps the current page and editor",pageList->currentIndex()==originalPage&&edit->isVisible()&&edit->toPlainText().isEmpty()))return false;
    QKeyEvent revertDraft(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(edit,&revertDraft);QApplication::processEvents();
    const auto prior=mode->Snapshot();const QJsonValue first=prior["sheets"].toArray()[0].toObject()["id"];
    if(!Press(w,"instructionPageEarlier")||!Explain("page reorder keeps the same UUID references",mode->Snapshot()["sheets"].toArray()[1].toObject()["id"]==first))return false;
    w.RunCommand("edit.undo");if(!Explain("page order and selection restore together",mode->Snapshot()==prior))return false;
    mode->Command("instructions.scene_editor");mode->Command("instructions.duplicate");const auto scenes=mode->Snapshot();
    if(!Press(w,"instructionPageEarlier"))return false;w.RunCommand("edit.undo");if(!Explain("scene reorder preserves references and Undo",mode->Snapshot()==scenes))return false;
    mode->Command("instructions.text");input=Shown<QPlainTextEdit>(w,"instructionText");auto* scene=dynamic_cast<V2InstructionScene*>(Shown<QWidget>(w,"instructionCanvas3d"));if(!input||!scene)return false;
    input->setPlainText("First paragraph");Click(scene,{180,140});input=Shown<QPlainTextEdit>(w,"instructionText");input->setPlainText("Second paragraph");Click(scene,{260,210});mode->Command("instructions.move");scene->SetSelectedMark(0);
    mode->Command("instructions.remove");auto marks=mode->Snapshot()["pages"].toArray()[mode->Snapshot()["current"].toInt()].toObject()["marks"].toArray();
    if(!Explain("deleting an edited paragraph does not overwrite its next neighbor",marks.size()==1&&marks[0].toObject()["text"]=="Second paragraph"))return false;
    w.RunCommand("edit.undo");marks=mode->Snapshot()["pages"].toArray()[mode->Snapshot()["current"].toInt()].toObject()["marks"].toArray();
    if(!Explain("Undo restores both independent paragraphs",marks.size()==2&&marks[0].toObject()["text"]=="First paragraph"&&marks[1].toObject()["text"]=="Second paragraph"))return false;
    mode->Command("instructions.sheet_editor");mode->Command("instructions.move");paper->SetSelected(0);Capture(w,"instruction-text-autosave");
    return mode->Save(cleanup.path)&&mode->ExportPdf(folder.filePath("ordered.pdf"));
}
}
