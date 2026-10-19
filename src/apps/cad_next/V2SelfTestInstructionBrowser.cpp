#include <QEvent>
#include <QPointF>
#include <QWidget>
#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2InstructionMode.h"
#include <QApplication>
#include <QAbstractButton>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QPixmap>
#include <QString>
#include <QUuid>

namespace kachakacha::v2::selftest {
namespace {
void ClickBrowser(QWidget* widget,QPointF point){for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent e(type,point,point,Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(widget,&e);}QApplication::processEvents();}
template<class T>T* Shown(V2MainWindow& w,const char* name){for(auto* v:w.findChildren<T*>(name))if(v->isVisible())return v;return nullptr;}
QJsonArray PaperItems(V2InstructionMode& mode){const auto s=mode.Snapshot();return s["sheets"].toArray()[s["currentSheet"].toInt()].toObject()["items"].toArray();}
void SelectRow(QListWidget* list,int row){list->scrollToItem(list->item(row));QApplication::processEvents();ClickBrowser(list->viewport(),list->visualItemRect(list->item(row)).center());}
}
bool InstructionBrowserWorkflow(V2MainWindow& w)
{
    QTemporaryDir folder;w.resize(1500,1000);w.SetMode(app::UiMode::Instructions);QApplication::processEvents();
    V2InstructionMode* mode=nullptr;for(auto* child:w.centralWidget()->children())if(auto* m=dynamic_cast<V2InstructionMode*>(child))mode=m;if(!mode)return false;
    struct Cleanup{V2InstructionMode* mode;QString path;~Cleanup(){mode->Save(path);}}cleanup{mode,folder.filePath("browser.kci")};
    if(!mode->Save(cleanup.path))return false;w.RunCommand("file.new");
    auto* list=Shown<QListWidget>(w,"instructionSceneList");auto* dock=w.findChild<QDockWidget*>("entityDock");
    if(!Explain("instruction scenes replace the CAD list",list&&list->count()==1&&dock&&dock->isHidden()))return false;
    mode->Command("instructions.text");auto* text=Shown<QPlainTextEdit>(w,"instructionText");if(!text)return false;text->setPlainText("Roof");
    auto* scene=w.findChild<QWidget*>("instructionCanvas3d");ClickBrowser(scene,{210,180});mode->Command("instructions.duplicate");
    const auto pages=mode->Snapshot()["pages"].toArray();const auto first=pages[0].toObject()["id"].toString(),second=pages[1].toObject()["id"].toString();
    if(!Explain("duplicate appears as a distinct scene",list->count()==2&&first!=second&&list->currentRow()==1))return false;
    SelectRow(list,0);if(!Explain("scene list opens its editor",mode->Snapshot()["current"]==0&&mode->Snapshot()["workspace"]=="scene"))return false;
    QEventLoop loop;QTimer::singleShot(150,&loop,&QEventLoop::quit);loop.exec();
    if(!Explain("visible scenes have generated thumbnails",!list->item(0)->icon().isNull()&&!list->item(1)->icon().isNull()))return false;
    auto* place=Shown<QPushButton>(w,"instructionBrowserPlace");if(!place)return false;place->click();
    auto* paper=dynamic_cast<V2InstructionSheetView*>(w.findChild<QWidget*>("instructionPaperCanvas"));if(!paper)return false;
    ClickBrowser(paper,paper->ToScreen({20,20}));
    if(!Explain("left scene places a 3D reference on paper",PaperItems(*mode).size()==1&&PaperItems(*mode)[0].toObject()["sceneId"]==first))return false;
    SelectRow(list,1);ClickBrowser(paper,paper->ToScreen({20,110}));
    if(!Explain("another scene can be chosen without leaving paper",mode->Snapshot()["workspace"]=="sheet"&&PaperItems(*mode).size()==2&&PaperItems(*mode)[1].toObject()["sceneId"]==second))return false;
    mode->Command("instructions.move");SelectRow(list,1);ClickBrowser(paper,paper->ToScreen({110,20}));
    if(!Explain("browsing a scene in move mode does not accidentally place it",PaperItems(*mode).size()==2))return false;
    place=Shown<QPushButton>(w,"instructionBrowserPlace");if(!place)return false;place->click();ClickBrowser(paper,paper->ToScreen({110,20}));
    if(!Explain("explicit place button rearms the selected scene",PaperItems(*mode).size()==3&&PaperItems(*mode).last().toObject()["sceneId"]==second))return false;
    const auto placed=mode->Snapshot();w.RunCommand("edit.undo");const auto undone=mode->Snapshot();ClickBrowser(paper,paper->ToScreen({110,150}));
    if(!Explain("Undo cancels a stale scene placement",PaperItems(*mode)==undone["sheets"].toArray()[0].toObject()["items"].toArray()))return false;
    w.RunCommand("edit.redo");if(!Explain("Redo restores the placement and browser selection",mode->Snapshot()==placed&&list->currentRow()==1))return false;
    const auto removeFromList=[list](bool accept){list->setFocus();QApplication::processEvents();bool observed=false;QTimer timer;timer.setSingleShot(true);
        QObject::connect(&timer,&QTimer::timeout,&timer,[&]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){observed=true;box->button(accept?QMessageBox::Yes:QMessageBox::Cancel)->click();}});
        timer.start(40);QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(list,&remove);timer.stop();return observed;};
    if(!Explain("Delete on the scene list opens confirmation and can be cancelled",removeFromList(false)&&mode->Snapshot()==placed))return false;
    if(!Explain("list Delete confirms its scene rather than a selected paper figure",removeFromList(true)&&mode->Snapshot()["workspace"]=="sheet"&&list->count()==1&&PaperItems(*mode).size()==1))return false;
    w.RunCommand("edit.undo");if(!Explain("list deletion Undo restores paper and scene references",mode->Snapshot()==placed&&list->count()==2&&list->currentRow()==1))return false;
    auto* edit=Shown<QPushButton>(w,"instructionBrowserEdit");if(!edit)return false;edit->click();
    if(!Explain("edit returns to the chosen scene",mode->Snapshot()["workspace"]=="scene"&&mode->Snapshot()["current"]==1))return false;
    auto* title=w.findChild<QLineEdit*>("instructionPageTitle");if(!title)return false;title->selectAll();
    QKeyEvent rename(QEvent::KeyPress,0,Qt::NoModifier,"Roof detail");QApplication::sendEvent(title,&rename);
    if(!Explain("typing a scene name updates the left list",list->item(1)->text().contains("Roof detail")))return false;
    const auto beforeDelete=mode->Snapshot();QTimer::singleShot(40,[]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))box->button(QMessageBox::Yes)->click();});mode->Command("instructions.delete_page");
    if(!Explain("deleted scene and its placements disappear together",list->count()==1&&mode->Snapshot()["sheets"].toArray()[0].toObject()["items"].toArray().size()==1))return false;
    w.RunCommand("edit.undo");if(!Explain("Undo restores scene UUID list and references",mode->Snapshot()==beforeDelete&&list->count()==2&&list->currentRow()==1))return false;
    mode->Command("instructions.sheet_editor");if(!mode->Save(cleanup.path))return false;
    const auto saved=mode->Snapshot();QFile bad(folder.filePath("bad.kci"));if(!bad.open(QIODevice::WriteOnly))return false;bad.write("{}");bad.close();
    V2InstructionMode::SetPathChooser(w,[&](bool){return bad.fileName();});w.RunCommand("file.open");
    if(!Explain("failed alternate file open keeps list and paper",mode->Snapshot()==saved&&list->count()==2&&list->currentRow()==1))return false;
    V2InstructionMode::SetPathChooser(w,[](bool){return QString();});w.RunCommand("file.open");if(mode->Snapshot()!=saved)return false;
    const auto output=qEnvironmentVariable("KACHACAD_INSTRUCTION_PROOF_DIR");if(!output.isEmpty()){QDir().mkpath(output);w.grab().save(output+"/instruction-scene-browser-paper.png");}
    w.RunCommand("file.new");if(!Explain("new document clears old scene UUIDs",list->count()==1&&list->item(0)->data(Qt::UserRole).toUuid().toString()!=first&&PaperItems(*mode).isEmpty()))return false;
    if(!mode->Load(cleanup.path)||!Explain("load synchronizes scene list and current source",mode->Snapshot()==saved&&list->count()==2&&list->currentRow()==1))return false;
    auto many=saved;QJsonArray manyPages;for(int i=0;i<200;++i){auto page=pages[0].toObject();page["id"]=QUuid::createUuid().toString();page["title"]=QStringLiteral("Scene %1").arg(i+1);manyPages.append(page);}
    many["pages"]=manyPages;many["sheets"]=QJsonArray{QJsonObject{{"id",QUuid::createUuid().toString()},{"title","Paper"},{"widthMm",210},{"heightMm",297},{"items",QJsonArray{}}}};
    if(!mode->Restore(many))return false;w.resize(1500,600);QApplication::processEvents();
    QEventLoop compactWait;QTimer::singleShot(160,&compactWait,&QEventLoop::quit);compactWait.exec();
    const auto generated=[list]{int n=0;for(int i=0;i<list->count();++i)if(!list->item(i)->icon().isNull())++n;return n;};const int initial=generated();
    if(!Explain("large scene list generates only visible thumbnails",list->count()==200&&initial>0&&initial<20))return false;
    w.resize(1500,1000);QApplication::processEvents();QEventLoop larger;QTimer::singleShot(160,&larger,&QEventLoop::quit);larger.exec();
    if(!Explain("resizing generates newly visible scene thumbnails",generated()>initial))return false;
    if(!mode->Restore(saved))return false;
    w.SetMode(app::UiMode::Part);if(!Explain("leaving instruction restores CAD list",!dock->isHidden()&&!list->isVisible()))return false;
    dock->hide();w.SetMode(app::UiMode::Instructions);w.SetMode(app::UiMode::Part);
    return Explain("an originally hidden CAD list stays hidden",dock->isHidden());
}
}
