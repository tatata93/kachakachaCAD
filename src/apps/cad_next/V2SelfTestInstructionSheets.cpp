#include "V2SelfTest.h"
#include "V2InstructionMode.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include <QApplication>
#include <QComboBox>
#include <QFontComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonObject>
#include <QListWidget>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QPixmap>
#include <QImage>
#include <QMouseEvent>
#include <QEvent>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QUuid>
#include <QWidget>
#include <QFont>
#include <QByteArray>
#include <QIODevice>
#include <QMetaObject>
#include <QRegularExpression>
#include <QTimer>
#include <QDialog>
#include <QMessageBox>
#include <QObject>
#include <cmath>
#include <iostream>
namespace kachakacha::v2::selftest {
bool MakeCurvedGuideSurface(V2MainWindow&);
namespace {
struct CleanInstructions {
    V2InstructionMode* mode;
    QString path;
    ~CleanInstructions(){if(mode&&!mode->Save(path))std::cerr<<"instruction paper cleanup save failed\n"<<std::flush;}
};
template<class T>T* Visible(V2MainWindow& window,const char* name) {
    for(auto* widget:window.findChildren<T*>(name))if(widget->isVisible())return widget;return nullptr;
}
V2InstructionMode* Mode(V2MainWindow& window) {
    for(auto* child:window.centralWidget()->children())if(auto* mode=dynamic_cast<V2InstructionMode*>(child))return mode;return nullptr;
}
void Mouse(QWidget* view,QEvent::Type type,QPointF at,Qt::MouseButtons held,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    QMouseEvent event(type,at,at,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,held,modifiers);
    QApplication::sendEvent(view,&event);QApplication::processEvents();
}
void Click(V2InstructionSheetView* view,QPointF mm) {
    const auto at=view->ToScreen(mm);Mouse(view,QEvent::MouseButtonPress,at,Qt::LeftButton);
    Mouse(view,QEvent::MouseButtonRelease,at,Qt::NoButton);
}
QJsonArray Items(V2InstructionMode& mode) {
    const auto doc=mode.Snapshot();return doc["sheets"].toArray()[doc["currentSheet"].toInt()].toObject()["items"].toArray();
}
bool AddText(V2MainWindow& window,V2InstructionMode& mode,V2InstructionSheetView* view) {
    mode.Command("instructions.text");auto* text=Visible<QLineEdit>(window,"instructionText");
    auto* size=Visible<QDoubleSpinBox>(window,"instructionFontSize");
    auto* bold=Visible<QCheckBox>(window,"instructionTextBold");auto* font=Visible<QFontComboBox>(window,"instructionFont");
    if(!Explain("paper font controls",text&&size&&bold&&font))return false;
    text->setText(QStringLiteral("1. パーツを組み合わせる"));
    QMetaObject::invokeMethod(text,"textEdited",Qt::DirectConnection,Q_ARG(QString,text->text()));
    font->setCurrentFont(QFont(QStringLiteral("Arial")));size->setValue(17.5);
    QMetaObject::invokeMethod(size,"editingFinished",Qt::DirectConnection);bold->setChecked(true);Click(view,{20,100});
    const auto item=Items(mode).last().toObject(),savedFont=item["font"].toObject();
    if(!Explain("text preserves content family size bold",item["kind"]=="text"&&item["text"]==text->text()&&
        !savedFont["family"].toString().isEmpty()&&savedFont["sizePt"].toDouble()==17.5&&savedFont["bold"].toBool()))return false;
    const auto before=view->Render({840,1188});size->setValue(23.5);
    QMetaObject::invokeMethod(size,"editingFinished",Qt::DirectConnection);
    return Explain("font edit changes paper rendering",view->Render({840,1188})!=before&&
        Items(mode).last().toObject()["font"].toObject()["sizePt"].toDouble()==23.5);
}
bool AddImages(V2MainWindow& window,V2InstructionMode& mode,V2InstructionSheetView* view,const QTemporaryDir& folder) {
    for(const auto* format:{"png","jpg"}){const auto path=folder.filePath(QStringLiteral("reference.")+format);
        QImage image(80,40,QImage::Format_RGB32);image.fill(format[0]=='p'?Qt::green:Qt::red);
        if(!Explain("reference image written",image.save(path,format)))return false;
        V2InstructionMode::SetPathChooser(window,[path](bool){return path;});mode.Command("instructions.insert_image");
        Click(view,format[0]=='p'?QPointF(125,95):QPointF(125,150));
        const auto item=Items(mode).last().toObject();QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;
        if(!Explain("raster keeps original bytes",item["kind"]=="image"&&
            QByteArray::fromBase64(item["imageData"].toString().toLatin1())==file.readAll()))return false;
        const auto encoded=item["imageFormat"].toString();
        if(!Explain("PNG JPEG format retained",format[0]=='p'?encoded=="png":encoded=="jpg"||encoded=="jpeg"))return false;
    }
    V2InstructionMode::SetPathChooser(window,[](bool){return QString();});return true;
}
bool ExtremeImages(V2MainWindow& window,V2InstructionMode& mode,V2InstructionSheetView* view,const QTemporaryDir& folder) {
    const auto original=mode.Snapshot();const QSize sizes[]={{16000,1},{1,16000}};
    for(int i=0;i<2;++i){const auto path=folder.filePath(QStringLiteral("thin-reference-%1.png").arg(i));
        QImage image(sizes[i],QImage::Format_RGB32);image.fill(Qt::blue);
        if(!Explain("thin reference image written",image.save(path,"PNG")))return false;
        V2InstructionMode::SetPathChooser(window,[path](bool){return path;});mode.Command("instructions.insert_image");Click(view,{30,30});
        const auto placed=mode.Snapshot();const auto rect=Items(mode).last().toObject()["rect"].toArray();
        const auto paper=placed["sheets"].toArray()[placed["currentSheet"].toInt()].toObject();
        if(!Explain("extreme image respects saved size limits",rect[2].toDouble()>=.01&&rect[3].toDouble()>=.01&&
            rect[2].toDouble()<=paper["widthMm"].toDouble()&&rect[3].toDouble()<=paper["heightMm"].toDouble()))return false;
        window.RunCommand("edit.undo");if(!Explain("thin image placement undo",mode.Snapshot()==original))return false;
        window.RunCommand("edit.redo");if(!Explain("thin image placement redo",mode.Snapshot()==placed))return false;
        const auto saved=folder.filePath(QStringLiteral("thin-image-%1.kci").arg(i));
        if(!Explain("thin image save reload exact",mode.Save(saved)&&mode.Load(saved)&&mode.Snapshot()==placed))return false;
        mode.Command("instructions.move");view->SetSelected(Items(mode).size()-1);
        const QPointF corner(rect[0].toDouble()+rect[2].toDouble(),rect[1].toDouble()+rect[3].toDouble());
        const QPointF shrunken(rect[0].toDouble()+.001,rect[1].toDouble()+.001);
        Mouse(view,QEvent::MouseButtonPress,view->ToScreen(corner),Qt::LeftButton,Qt::ShiftModifier);
        Mouse(view,QEvent::MouseMove,view->ToScreen(shrunken),Qt::LeftButton,Qt::ShiftModifier);
        Mouse(view,QEvent::MouseButtonRelease,view->ToScreen(shrunken),Qt::NoButton,Qt::ShiftModifier);
        const auto resized=mode.Snapshot();const auto size=Items(mode).last().toObject()["rect"].toArray();
        if(!Explain("Shift shrink preserves valid thin image dimensions",size[2].toDouble()>=.01&&size[3].toDouble()>=.01&&
            std::abs(size[3].toDouble()/size[2].toDouble()-rect[3].toDouble()/rect[2].toDouble())<1e-9))return false;
        if(!Explain("Shift shrink save reload exact",mode.Save(saved)&&mode.Load(saved)&&mode.Snapshot()==resized))return false;
        if(!mode.Restore(original))return false;
    }
    V2InstructionMode::SetPathChooser(window,[](bool){return QString();});
    return Explain("thin image test leaves proof unchanged",mode.Snapshot()==original);
}
bool NegativeImageBounds(V2InstructionMode& mode,V2InstructionSheetView* view,const QTemporaryDir& folder) {
    const auto original=mode.Snapshot();
    for(const bool shift:{false,true}){auto negative=original;auto sheets=negative["sheets"].toArray();
        const int page=negative["currentSheet"].toInt();auto sheet=sheets[page].toObject();auto items=sheet["items"].toArray();
        auto item=items.last().toObject();item["rect"]=QJsonArray{-100000,-100000,80,40};items[items.size()-1]=item;
        sheet["items"]=items;sheets[page]=sheet;negative["sheets"]=sheets;
        if(!Explain("negative image position is accepted",mode.Restore(negative)))return false;
        mode.Command("instructions.move");view->SetSelected(items.size()-1);
        const auto modifiers=shift?Qt::ShiftModifier:Qt::NoModifier;
        const auto corner=view->ToScreen({-100000+80,-100000+40});const auto outside=view->ToScreen({200000,200000});
        Mouse(view,QEvent::MouseButtonPress,corner,Qt::LeftButton,modifiers);
        Mouse(view,QEvent::MouseMove,outside,Qt::LeftButton,modifiers);
        Mouse(view,QEvent::MouseButtonRelease,outside,Qt::NoButton,modifiers);
        const auto resized=mode.Snapshot();const auto rect=Items(mode).last().toObject()["rect"].toArray();
        if(!Explain("large resize respects saved dimension limits",rect[0].toDouble()==-100000&&rect[1].toDouble()==-100000&&
            rect[2].toDouble()>=.01&&rect[2].toDouble()<=100000&&rect[3].toDouble()>=.01&&rect[3].toDouble()<=100000))return false;
        if(shift&&!Explain("large Shift resize preserves ratio",std::abs(rect[3].toDouble()/rect[2].toDouble()-.5)<1e-9))return false;
        const auto path=folder.filePath(shift?"negative-shift.kci":"negative-resize.kci");
        if(!Explain("large negative frame save reload exact",mode.Save(path)&&mode.Load(path)&&mode.Snapshot()==resized))return false;
        if(!mode.Restore(original))return false;
    }
    return Explain("negative frame test leaves proof unchanged",mode.Snapshot()==original);
}
bool MoveResize(V2MainWindow& window,V2InstructionMode& mode,V2InstructionSheetView* view) {
    mode.Command("instructions.move");view->SetSelected(0);const auto before=mode.Snapshot();
    auto rect=Items(mode)[0].toObject()["rect"].toArray();const QPointF center(rect[0].toDouble()+rect[2].toDouble()/2,
        rect[1].toDouble()+rect[3].toDouble()/2);
    Mouse(view,QEvent::MouseButtonPress,view->ToScreen(center),Qt::LeftButton);
    Mouse(view,QEvent::MouseMove,view->ToScreen(center+QPointF(8,6)),Qt::LeftButton);view->CancelInput();
    Mouse(view,QEvent::MouseButtonRelease,view->ToScreen(center+QPointF(8,6)),Qt::NoButton);
    if(!Explain("cancel paper drag restores placement",mode.Snapshot()==before))return false;
    Mouse(view,QEvent::MouseButtonPress,view->ToScreen(center),Qt::LeftButton);
    Mouse(view,QEvent::MouseMove,view->ToScreen(center+QPointF(8,6)),Qt::LeftButton);
    Mouse(view,QEvent::MouseButtonRelease,view->ToScreen(center+QPointF(8,6)),Qt::NoButton);
    if(!Explain("paper drag moves scene placement",mode.Snapshot()!=before))return false;
    window.RunCommand("edit.undo");if(!Explain("paper drag undo",mode.Snapshot()==before))return false;
    window.RunCommand("edit.redo");rect=Items(mode)[0].toObject()["rect"].toArray();view->SetSelected(0);
    const QPointF corner(rect[0].toDouble()+rect[2].toDouble(),rect[1].toDouble()+rect[3].toDouble());
    Mouse(view,QEvent::MouseButtonPress,view->ToScreen(corner),Qt::LeftButton);
    Mouse(view,QEvent::MouseMove,view->ToScreen(corner+QPointF(12,10)),Qt::LeftButton);
    Mouse(view,QEvent::MouseButtonRelease,view->ToScreen(corner+QPointF(12,10)),Qt::NoButton);
    const auto resized=Items(mode)[0].toObject()["rect"].toArray();
    return Explain("paper corner drag resizes scene",resized[2].toDouble()>rect[2].toDouble()+5&&resized[3].toDouble()>rect[3].toDouble()+5);
}
bool PendingDrag(V2InstructionMode& mode,V2InstructionSheetView* view,const QTemporaryDir& folder) {
    mode.Command("instructions.move");view->SetSelected(0);const auto before=mode.Snapshot();
    const auto rect=Items(mode)[0].toObject()["rect"].toArray();
    const QPointF center(rect[0].toDouble()+rect[2].toDouble()/2,rect[1].toDouble()+rect[3].toDouble()/2);
    const auto begin=[&]{Mouse(view,QEvent::MouseButtonPress,view->ToScreen(center),Qt::LeftButton);
        Mouse(view,QEvent::MouseMove,view->ToScreen(center+QPointF(5,7)),Qt::LeftButton);};
    begin();mode.Command("instructions.scene_editor");mode.Command("instructions.sheet_editor");
    Mouse(view,QEvent::MouseButtonRelease,view->ToScreen(center+QPointF(5,7)),Qt::NoButton);
    if(!Explain("workspace switch cancels pending paper drag",mode.Snapshot()==before))return false;
    view->SetSelected(0);begin();const auto path=folder.filePath("pending-drag.kci");
    if(!Explain("save cancels pending paper drag",mode.Save(path)&&mode.Snapshot()==before))return false;
    Mouse(view,QEvent::MouseButtonRelease,view->ToScreen(center+QPointF(5,7)),Qt::NoButton);
    return Explain("saved drag cannot leak on release",mode.Snapshot()==before&&mode.Load(path)&&mode.Snapshot()==before);
}
bool ShiftResize(V2InstructionMode& mode,V2InstructionSheetView* view) {
    mode.Command("instructions.move");view->SetSelected(0);const auto rect=Items(mode)[0].toObject()["rect"].toArray();
    const QPointF corner(rect[0].toDouble()+rect[2].toDouble(),rect[1].toDouble()+rect[3].toDouble());
    Mouse(view,QEvent::MouseButtonPress,view->ToScreen(corner),Qt::LeftButton,Qt::ShiftModifier);
    Mouse(view,QEvent::MouseMove,view->ToScreen(corner+QPointF(12,20)),Qt::LeftButton,Qt::ShiftModifier);
    Mouse(view,QEvent::MouseButtonRelease,view->ToScreen(corner+QPointF(12,20)),Qt::NoButton,Qt::ShiftModifier);
    const auto resized=Items(mode)[0].toObject()["rect"].toArray();
    return Explain("Shift resize keeps paper item proportions",resized[2].toDouble()>rect[2].toDouble()+5&&
        std::abs(resized[3].toDouble()/resized[2].toDouble()-rect[3].toDouble()/rect[2].toDouble())<1e-9);
}
bool Files(V2InstructionMode& mode,const QTemporaryDir& folder) {
    const auto saved=mode.Snapshot();const auto path=folder.filePath("instructions.kci");
    if(!Explain("v3 save reload exact",mode.Save(path)&&mode.Load(path)&&mode.Snapshot()==saved))return false;
    auto invalid=saved;auto sheets=invalid["sheets"].toArray();auto sheet=sheets[0].toObject();auto items=sheet["items"].toArray();
    auto item=items[0].toObject();item["sceneId"]=QUuid::createUuid().toString();items[0]=item;sheet["items"]=items;sheets[0]=sheet;invalid["sheets"]=sheets;
    if(!Explain("invalid scene reference keeps document",!mode.Restore(invalid)&&mode.Snapshot()==saved))return false;
    auto old=saved;old["version"]=2;old.remove("sheets");old.remove("currentSheet");old.remove("workspace");auto pages=old["pages"].toArray();
    for(int i=0;i<pages.size();++i){auto page=pages[i].toObject();page.remove("id");pages[i]=page;}old["pages"]=pages;
    if(!Explain("old v2 becomes scenes and referenced paper",mode.Restore(old)&&mode.Snapshot()["version"].toInt()==3&&
        mode.Snapshot()["sheets"].toArray().size()==pages.size()))return false;
    if(!mode.Restore(saved))return false;
    for(const auto* ext:{"png","jpg","bmp"}){const auto out=folder.filePath(QStringLiteral("paper.")+ext);
        if(!Explain("paper image output",mode.ExportImage(out,{840,1188})&&QImage(out).size()==QSize(840,1188)))return false;}
    const auto pdf=folder.filePath("instructions.pdf");if(!Explain("mixed paper sizes PDF",mode.ExportPdf(pdf)))return false;
    QFile pdfFile(pdf);if(!pdfFile.open(QIODevice::ReadOnly))return false;
    const auto pdfText=QString::fromLatin1(pdfFile.readAll());
    const QRegularExpression box(QStringLiteral("/MediaBox\\s*\\[\\s*0\\s+0\\s+([0-9.]+)\\s+([0-9.]+)\\s*\\]"));
    auto matches=box.globalMatch(pdfText);bool portrait=false,landscape=false;
    while(matches.hasNext()){const auto match=matches.next();const auto width=match.captured(1).toDouble(),height=match.captured(2).toDouble();
        portrait|=width<height;landscape|=width>height;}
    if(!Explain("PDF preserves both paper orientations",portrait&&landscape))return false;
    return mode.Save(path);
}
bool LiveScene(V2MainWindow& window,V2InstructionMode& mode,V2InstructionSheetView* sheet) {
    const auto before=sheet->Render({840,1188});const auto rect=Items(mode)[0].toObject()["rect"].toArray();
    const auto center=sheet->ToScreen({rect[0].toDouble()+rect[2].toDouble()/2,rect[1].toDouble()+rect[3].toDouble()/2});
    Mouse(sheet,QEvent::MouseButtonDblClick,center,Qt::LeftButton);
    if(!Explain("open referenced 3D scene",mode.Snapshot()["workspace"]=="scene"&&mode.Snapshot()["current"].toInt()==0))return false;
    auto* view=dynamic_cast<V2InstructionScene*>(window.findChild<QWidget*>("instructionCanvas3d"));if(!view)return false;
    QMouseEvent press(QEvent::MouseButtonPress,{100,100},{100,100},Qt::RightButton,Qt::RightButton,Qt::NoModifier);
    QApplication::sendEvent(view,&press);
    QMouseEvent move(QEvent::MouseMove,{200,145},{200,145},Qt::NoButton,Qt::RightButton,Qt::NoModifier);QApplication::sendEvent(view,&move);
    QMouseEvent release(QEvent::MouseButtonRelease,{200,145},{200,145},Qt::RightButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&release);
    mode.Command("instructions.arrow");Mouse(view,QEvent::MouseButtonPress,{250,150},Qt::LeftButton);
    Mouse(view,QEvent::MouseMove,{400,300},Qt::LeftButton);Mouse(view,QEvent::MouseButtonRelease,{400,300},Qt::NoButton);
    mode.Command("instructions.move");
    QApplication::processEvents();const auto output=QDir::tempPath()+"/kachakacha-instruction-proof";QDir().mkpath(output);
    window.grab().save(output+"/v2-instructions-scene-editor.png");mode.Command("instructions.sheet_editor");QApplication::processEvents();
    return Explain("paper renders updated 3D scene",sheet->Render({840,1188})!=before);
}
bool DeferredDimension(V2MainWindow& window,V2InstructionMode& mode) {
    const auto original=mode.Snapshot();auto* pages=Visible<QComboBox>(window,"instructionPages");if(!pages)return false;
    pages->setCurrentIndex(0);mode.Command("instructions.paper");
    auto* width=Visible<QDoubleSpinBox>(window,"instructionPaperWidth");if(!width)return false;
    const auto before=mode.Snapshot();width->setValue(230);
    QMetaObject::invokeMethod(width,"editingFinished",Qt::DirectConnection);
    auto* editor=width->findChild<QLineEdit*>();if(!editor)return false;
    editor->setFocus();editor->setText(QStringLiteral("333.00 mm"));pages->setCurrentIndex(1);QApplication::processEvents();
    if(!Explain("unfinished dimension does not overwrite next paper",mode.Snapshot()["sheets"].toArray()[1].toObject()["widthMm"].toDouble()==297))return false;
    window.RunCommand("edit.undo");if(!Explain("paper restore ignores old focused controls",mode.Snapshot()==before))return false;
    window.RunCommand("edit.redo");if(!Explain("redo retains committed paper dimension",mode.Snapshot()["sheets"].toArray()[0].toObject()["widthMm"].toDouble()==230))return false;
    return mode.Restore(original);
}
bool Sheets(V2MainWindow& window) {
    QTemporaryDir folder;
    bool unexpectedDialog=false;QTimer modalTimer(&window);modalTimer.setInterval(2000);
    QObject::connect(&modalTimer,&QTimer::timeout,&window,[&unexpectedDialog]{if(auto* modal=QApplication::activeModalWidget()){
        std::cerr<<"instruction paper unexpected modal: "<<modal->metaObject()->className()<<" "<<modal->windowTitle().toStdString();
        if(auto* box=qobject_cast<QMessageBox*>(modal))std::cerr<<" "<<box->text().toStdString();std::cerr<<'\n'<<std::flush;
        unexpectedDialog=true;if(auto* dialog=qobject_cast<QDialog*>(modal))dialog->reject();}});modalTimer.start();
    if(!MakeCurvedGuideSurface(window))return false;window.resize(1400,950);window.show();
    const auto revision=window.Session().GetDocument().Revision();window.SetMode(app::UiMode::Instructions);QApplication::processEvents();
    auto* mode=Mode(window);if(!mode)return false;CleanInstructions cleanup{mode,folder.filePath("cleanup.kci")};mode->Command("instructions.capture");
    auto* parts=Visible<QListWidget>(window,"instructionModelParts");if(!parts||parts->count()<1)return false;
    for(int i=0;i<parts->count();++i)parts->item(i)->setCheckState(i==0?Qt::Checked:Qt::Unchecked);mode->AcceptParts();
    const auto sceneId=mode->Snapshot()["pages"].toArray()[0].toObject()["id"].toString();mode->Command("instructions.duplicate");
    if(!Explain("independent scene ids",mode->Snapshot()["pages"].toArray().size()==2&&
        mode->Snapshot()["pages"].toArray()[1].toObject()["id"].toString()!=sceneId))return false;
    mode->Command("instructions.sheet_editor");QApplication::processEvents();
    auto* view=dynamic_cast<V2InstructionSheetView*>(window.findChild<QWidget*>("instructionPaperCanvas"));
    if(!Explain("paper editor replaces scene",view&&view->isVisible()&&mode->Snapshot()["workspace"]=="sheet"))return false;
    mode->Command("instructions.place_scene");auto* source=Visible<QComboBox>(window,"instructionSceneSource");
    if(!Explain("choose independent scene",source&&source->count()==2))return false;source->setCurrentIndex(0);Click(view,{20,20});
    const auto item=Items(*mode)[0].toObject();
    if(!Explain("paper scene uses live 3D UUID not PNG",item["kind"]=="scene"&&item["sceneId"].toString()==sceneId&&
        !item.contains("imageData")&&mode->Snapshot()["assets"].toArray().size()==1))return false;
    source=Visible<QComboBox>(window,"instructionSceneSource");if(!source)return false;source->setCurrentIndex(1);Click(view,{115,20});
    if(!Explain("paper holds two independent 3D scene references",Items(*mode).size()==2&&
        Items(*mode)[0].toObject()["sceneId"]!=Items(*mode)[1].toObject()["sceneId"]))return false;
    if(!AddText(window,*mode,view))return false;
    if(!AddImages(window,*mode,view,folder))return false;
    if(!ExtremeImages(window,*mode,view,folder))return false;
    if(!NegativeImageBounds(*mode,view,folder))return false;
    if(!MoveResize(window,*mode,view)||!ShiftResize(*mode,view)||!PendingDrag(*mode,view,folder))return false;
    mode->Command("instructions.duplicate");mode->Command("instructions.paper");
    auto* width=Visible<QDoubleSpinBox>(window,"instructionPaperWidth"),*height=Visible<QDoubleSpinBox>(window,"instructionPaperHeight");
    if(!Explain("paper size settings",width&&height))return false;width->setValue(297);height->setValue(210);
    QMetaObject::invokeMethod(height,"editingFinished",Qt::DirectConnection);
    if(!Explain("different paper dimensions",mode->Snapshot()["sheets"].toArray()[1].toObject()["widthMm"].toDouble()==297&&
        mode->Snapshot()["sheets"].toArray()[1].toObject()["heightMm"].toDouble()==210))return false;
    if(!Files(*mode,folder))return false;
    if(!DeferredDimension(window,*mode))return false;
    auto* pages=Visible<QComboBox>(window,"instructionPages");if(!pages)return false;pages->setCurrentIndex(0);mode->Command("instructions.move");
    if(!LiveScene(window,*mode,view))return false;
    const auto output=QDir::tempPath()+"/kachakacha-instruction-proof";QDir().mkpath(output);
    window.grab().save(output+"/v2-instructions-layout.png");if(!mode->Save(folder.filePath("final.kci")))return false;
    window.SetMode(app::UiMode::Part);return Explain("paper and scene keep CAD model intact",window.Session().GetDocument().Revision()==revision)&&
        Explain("instruction test does not open unexpected modal",!unexpectedDialog);
}
}
std::vector<SelfTestCase> InstructionSheetCases() {return {{"HP-INSTRUCTIONS-SHEETS scenes live paper raster fonts export",&Sheets}};}
}
