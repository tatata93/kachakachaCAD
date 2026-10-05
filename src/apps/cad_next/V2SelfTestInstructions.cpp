#include "V2SelfTest.h"
#include "V2InstructionMode.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QJsonArray>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QPixmap>
#include <QEvent>
#include <QIODevice>
#include <QPointF>
#include <QLineEdit>
namespace kachakacha::v2::selftest {
bool MakeCurvedGuideSurface(V2MainWindow&);
namespace {
bool Instructions(V2MainWindow& window){
    if(!MakeCurvedGuideSurface(window))return false;
    window.resize(1400,900);window.Viewport().SetSelection({});window.Viewport().SetViewDirection(ViewDirection::Isometric);window.Viewport().FitToDocument();
    const auto revision=window.Session().GetDocument().Revision();window.SetMode(app::UiMode::Instructions);QApplication::processEvents();
    V2InstructionMode* mode=nullptr;for(auto* child:window.centralWidget()->children())if(auto* found=dynamic_cast<V2InstructionMode*>(child))mode=found;
    if(!Explain("instruction workspace replaces viewport",mode&&mode->isVisible()&&!window.Viewport().isVisible()))return false;
    window.RunCommand("instructions.capture");auto* view=mode->findChild<QGraphicsView*>(QStringLiteral("instructionCanvas"));
    if(!Explain("capture creates movable parts",view&&!view->scene()->items().empty()))return false;
    auto* part=view->scene()->items().front();const auto position=part->pos();part->setPos(position+QPointF(50,25));
    window.RunCommand("instructions.arrow");
    auto click=[&](const QPointF& point){const QPointF at=view->mapFromScene(point);QMouseEvent event(QEvent::MouseButtonPress,at,at,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(view->viewport(),&event);};
    click({300,300});click({500,400});window.RunCommand("instructions.text");
    auto data=mode->Snapshot();auto items=data["pages"].toArray()[0].toObject()["items"].toArray();
    bool arrow=false,text=false;for(const auto& item:items){arrow|=item.toObject()["kind"]=="arrow";text|=item.toObject()["kind"]=="text";}
    if(!Explain("arrow and numbered text exist",arrow&&text))return false;
    window.RunCommand("edit.undo");window.RunCommand("edit.redo");
    if(!Explain("instruction undo redo",mode->Snapshot()==data))return false;
    window.RunCommand("tools.search");QApplication::processEvents();window.RunCommand("instructions.move");
    if(!Explain("return from search restores instruction settings",window.findChild<QLineEdit*>(QStringLiteral("instructionText"))!=nullptr))return false;
    window.RunCommand("instructions.save");window.RunCommand("instructions.open");window.RunCommand("instructions.pdf");
    if(!Explain("cancelled file dialogs preserve pages",mode->Snapshot()==data))return false;
    window.RunCommand("instructions.duplicate");if(!Explain("duplicate keeps all items",mode->Snapshot()["pages"].toArray().size()==2))return false;
    QTemporaryDir folder;const auto file=folder.filePath(QStringLiteral("manual.kci")),pdf=folder.filePath(QStringLiteral("manual.pdf"));
    if(!mode->Save(file)||!mode->Load(file)||!mode->ExportPdf(pdf))return false;
    QFile output(pdf);if(!output.open(QIODevice::ReadOnly)||!output.read(4).startsWith("%PDF"))return false;
    const auto saved=mode->Snapshot();QFile bad(folder.filePath(QStringLiteral("bad.kci")));bad.open(QIODevice::WriteOnly);bad.write("{}");bad.close();
    if(!Explain("invalid document preserves pages",!mode->Load(bad.fileName())&&mode->Snapshot()==saved))return false;
    QApplication::processEvents();window.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-instructions.png"));
    window.SetMode(app::UiMode::Part);QApplication::processEvents();
    if(!Explain("return to unchanged model",window.Viewport().isVisible()&&window.Session().GetDocument().Revision()==revision))return false;
    window.SetMode(app::UiMode::Instructions);return Explain("mode switch retains pages",mode->Snapshot()==saved);
}
}
std::vector<SelfTestCase> InstructionCases(){return {{"HP-INSTRUCTIONS pages arrows save PDF and model isolation",&Instructions}};}
}
