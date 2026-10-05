#include <QComboBox>
#include <QUuid>
#include <QJsonObject>
#include <QString>
#include <QJsonDocument>
#include <QPoint>
#include <QSize>
#include <QWidget>
#include "V2SelfTest.h"
#include "V2InstructionMode.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include <QApplication>
#include <QJsonArray>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QPixmap>
#include <QImage>
#include <QEvent>
#include <QIODevice>
#include <QPointF>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
namespace kachakacha::v2::selftest {
bool MakeCurvedGuideSurface(V2MainWindow&);
namespace {
void Mouse(QWidget* view,QEvent::Type type,QPointF at,Qt::MouseButton button,Qt::MouseButtons held){QMouseEvent event(type,at,at,button,held,Qt::NoModifier);QApplication::sendEvent(view,&event);QApplication::processEvents();}
bool HiddenLines(){
    auto asset=std::make_shared<InstructionAsset>();modeling::MeshTriangle a,b;
    a.points={geometry::Vector3{-1,-1,1},geometry::Vector3{1,-1,1},geometry::Vector3{1,1,1}};
    b.points={geometry::Vector3{-1,-1,1},geometry::Vector3{1,1,1},geometry::Vector3{-1,1,1}};
    asset->mesh.triangles={a,b};asset->mesh.edges={{{-.5,0,0},{.5,0,0}}};
    InstructionPage page;page.pitch=1.5707963267948966;page.yaw=0;page.span=4;page.parts.push_back({QUuid::createUuid(),asset});
    V2InstructionScene view(nullptr);view.SetPage(&page);const auto hidden=view.Render({300,300});
    if(!Explain("hidden edge removed",qGray(hidden.pixel(150,150))==255))return false;
    asset->mesh.edges={{{-.5,0,2},{.5,0,2}}};const auto shown=view.Render({300,300});
    return Explain("front edge remains",qGray(shown.pixel(150,150))<200);
}
bool Instructions(V2MainWindow& window){
    if(!HiddenLines())return false;
    if(!MakeCurvedGuideSurface(window))return false;window.resize(1400,900);QTemporaryDir folder;
    const auto modelPath=folder.filePath("source.kcd2");window.SetPathChooser([modelPath](bool){return modelPath;});window.RunCommand("file.save_as");window.SetPathChooser([](bool){return QString();});
    if(!Explain("source model saved",QFile::exists(modelPath)))return false;
    const auto revision=window.Session().GetDocument().Revision();window.SetMode(app::UiMode::Instructions);QApplication::processEvents();
    V2InstructionMode* mode=nullptr;for(auto* child:window.centralWidget()->children())if(auto* m=dynamic_cast<V2InstructionMode*>(child))mode=m;
    if(!Explain("instruction workspace replaces viewport",mode&&mode->isVisible()&&!window.Viewport().isVisible()))return false;
    window.RunCommand("instructions.capture");auto* list=window.findChild<QListWidget*>("instructionModelParts");
    if(!Explain("model then part selection",list&&list->count()>0&&mode->Snapshot()["pages"].toArray()[0].toObject()["parts"].toArray().isEmpty()))return false;
    for(int i=0;i<list->count();++i)list->item(i)->setCheckState(Qt::Unchecked);mode->AcceptParts();
    if(!Explain("empty selection refused",mode->Snapshot()["pages"].toArray()[0].toObject()["parts"].toArray().isEmpty()))return false;
    if(!Explain("choose another model file",mode->ReadModel(modelPath)))return false;list=window.findChild<QListWidget*>("instructionModelParts");
    list->item(0)->setCheckState(Qt::Checked);mode->AcceptParts();QApplication::processEvents();
    if(!Explain("only chosen part added",mode->Snapshot()["pages"].toArray()[0].toObject()["parts"].toArray().size()==1))return false;
    auto* view=dynamic_cast<V2InstructionScene*>(mode->findChild<QWidget*>("instructionCanvas3d"));if(!view)return false;
    const auto original=mode->Snapshot();const auto image=view->Render(view->size());QPoint point;
    bool found=false;for(int y=10;y<image.height()-10&&!found;y++)for(int x=10;x<image.width()-10;x++)if(qGray(image.pixel(x,y))<100){point={x,y};found=true;break;}
    if(!Explain("outline is visible",found))return false;int colored=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixel(x,y);if(qRed(c)!=qGreen(c)||qGreen(c)!=qBlue(c))++colored;}
    if(!Explain("default export is monochrome",colored==0))return false;
    Mouse(view,QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton);Mouse(view,QEvent::MouseMove,point+QPoint(35,20),Qt::NoButton,Qt::LeftButton);Mouse(view,QEvent::MouseButtonRelease,point+QPoint(35,20),Qt::LeftButton,Qt::NoButton);
    if(!Explain("3D drag changes part placement",mode->Snapshot()!=original))return false;
    window.RunCommand("edit.undo");if(!Explain("undo restores placement",mode->Snapshot()==original))return false;window.RunCommand("edit.redo");
    const auto beforeOrbit=mode->Snapshot();Mouse(view,QEvent::MouseButtonPress,{100,100},Qt::RightButton,Qt::RightButton);Mouse(view,QEvent::MouseMove,{140,115},Qt::NoButton,Qt::RightButton);Mouse(view,QEvent::MouseButtonRelease,{140,115},Qt::RightButton,Qt::NoButton);
    if(!Explain("3D orbit changes camera",mode->Snapshot()!=beforeOrbit))return false;
    window.RunCommand("instructions.arrow");
    const auto beforeArrow=mode->Snapshot();
    Mouse(view,QEvent::MouseButtonPress,{100,100},Qt::LeftButton,Qt::LeftButton);
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);
    if(!Explain("Esc cancels pending arrow without editing page",mode->Snapshot()==beforeArrow))return false;
    for(const auto at:{QPointF(200,200),QPointF(400,300)}){Mouse(view,QEvent::MouseButtonPress,at,Qt::LeftButton,Qt::LeftButton);Mouse(view,QEvent::MouseButtonRelease,at,Qt::LeftButton,Qt::NoButton);}
    window.RunCommand("instructions.text");Mouse(view,QEvent::MouseButtonPress,{180,140},Qt::LeftButton,Qt::LeftButton);Mouse(view,QEvent::MouseButtonRelease,{180,140},Qt::LeftButton,Qt::NoButton);
    if(!Explain("world anchored arrow and text",mode->Snapshot()["pages"].toArray()[0].toObject()["marks"].toArray().size()==2))return false;
    window.RunCommand("tools.search");window.RunCommand("instructions.text");if(!Explain("settings return from search",window.findChild<QLineEdit*>("instructionText")!=nullptr))return false;
    window.RunCommand("instructions.move");QApplication::processEvents();QComboBox* placed=nullptr;for(auto* combo:window.findChildren<QComboBox*>("instructionPlacedPart"))if(combo->isVisible())placed=combo;
    if(!Explain("part selector available",placed&&placed->count()==2))return false;placed->setCurrentIndex(1);if(view->Selected()!=0)return false;
    window.RunCommand("instructions.duplicate");if(!Explain("duplicate page",mode->Snapshot()["pages"].toArray().size()==2))return false;
    const auto file=folder.filePath("manual.kci"),pdf=folder.filePath("manual.pdf");
    const auto saved=mode->Snapshot();if(!Explain("save",mode->Save(file))||!Explain("load",mode->Load(file))||!Explain("roundtrip",mode->Snapshot()==saved)||!Explain("pdf",mode->ExportPdf(pdf)))return false;
    for(const auto* ext:{"png","jpg","bmp"}){const auto output=folder.filePath(QStringLiteral("step.")+ext);if(!Explain(ext,mode->ExportImage(output,{640,480})&&QImage(output).size()==QSize(640,480)))return false;}
    if(!Explain("unsupported format refused",!mode->ExportImage(folder.filePath("step.unknown"),{640,480})))return false;
    QFile bad(folder.filePath("bad.kci"));bad.open(QIODevice::WriteOnly);bad.write("{}");bad.close();if(!Explain("bad file leaves page intact",!mode->Load(bad.fileName())&&mode->Snapshot()==saved))return false;
    const QJsonObject old{{"format","kachakacha-instructions"},{"version",1},{"current",0},{"pages",QJsonArray{QJsonObject{{"title","old"},{"items",QJsonArray{QJsonObject{{"kind","text"},{"text","legacy"},{"x",20},{"y",20},{"scale",1}}}}}}}};
    QFile legacy(folder.filePath("old.kci"));legacy.open(QIODevice::WriteOnly);legacy.write(QJsonDocument(old).toJson());legacy.close();
    if(!Explain("legacy diagram preserved",mode->Load(legacy.fileName())&&!mode->Snapshot()["pages"].toArray()[0].toObject()["legacy"].toString().isEmpty()))return false;
    if(!mode->Restore(saved)||!mode->Save(file))return false;
    window.grab().save(QDir::tempPath()+"/kachakacha-instructions-3d.png");window.SetMode(app::UiMode::Part);QApplication::processEvents();
    if(!Explain("original model unchanged",window.Session().GetDocument().Revision()==revision&&window.Viewport().isVisible()))return false;
    window.SetMode(app::UiMode::Instructions);return Explain("mode keeps state",mode->Snapshot()==saved);
}
}
std::vector<SelfTestCase> InstructionCases(){return {{"HP-INSTRUCTIONS 3D model choice outlines arrows image export",&Instructions}};}
}
#include <QKeyEvent>
