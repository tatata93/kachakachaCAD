#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2DrawingDock.h"
#include "kachakacha/app/DirectWireEntry.h"
#include "kachakacha/app/Selection.h"
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QApplication>
#include <QDir>
#include <QPixmap>
#include <QString>
#include <QFile>
#include <QIODevice>
namespace kachakacha::v2::selftest {
bool MakeGptHeadFixture(V2MainWindow& window);
namespace {
bool ClickGpt(V2MainWindow& window,const char* name)
{
    auto* button=window.findChild<QPushButton*>(QString::fromLatin1(name));
    if (!button || !button->isEnabled()) { return Explain(name,false); }
    button->click(); QApplication::processEvents(); return true;
}
bool MakeSource(V2MainWindow& window)
{
    const std::vector<geometry::Vector3> points{{0,0,0},{30,0,0},{24,20,0},{0,15,0}};
    for (std::size_t i=0;i<points.size();++i) {
        app::DirectWireRequest wire; wire.kind=app::DirectWireKind::SpatialLine;
        wire.points={points[i],points[(i+1)%points.size()]};
        window.DrawingDock().SetDirectWire(wire,QStringLiteral("外周")); window.DrawingDock().PressCreateWire();
    }
    window.Viewport().SetSelection(app::SelectAllOfKind(window.Session().GetDocument().Snapshot(),domain::EntityKind::Wire));
    window.RunCommand("surface.gpt_create");
    if (!ClickGpt(window,"gptSurfacePreview") || !ClickGpt(window,"gptSurfaceConfirm")) { return false; }
    window.Viewport().SetSelection(app::SelectAllOfKind(window.Session().GetDocument().Snapshot(),domain::EntityKind::GuideSurface));
    return true;
}
bool Fabrication(V2MainWindow& window)
{
    if (!MakeSource(window)) { return false; }
    const auto before=window.Session().GetDocument().Revision();
    window.RunCommand("fabrication.gpt_create");
    if (!Explain("dedicated GPT fabrication panel",window.ShelfShown(app::Shelf::GptFabrication))) { return false; }
    if (!ClickGpt(window,"gptFabricationPreview")) { return false; }
    const auto* status=window.findChild<QLabel*>(QStringLiteral("gptFabricationStatus"));
    if (status) { Note(status->text().toStdString().c_str()); }
    if (!Explain("filled preview without mutating document",window.Viewport().ToolPreviewFaceCount()>0
        && window.Session().GetDocument().Revision()==before)) { return false; }
    auto* assembly=window.findChild<QDoubleSpinBox*>(QStringLiteral("gptFabricationAssembly"));
    if (!assembly) { return false; } assembly->setValue(30);
    if (!ClickGpt(window,"gptFabricationCancel") || !Explain("cancel removes preview",window.Viewport().ToolPreviewFaceCount()==0
        && window.Session().GetDocument().Revision()==before)) { return false; }
    window.RunCommand("fabrication.gpt_create");
    if (!ClickGpt(window,"gptFabricationPreview") || !ClickGpt(window,"gptFabricationConfirm")) { return false; }
    if (!Explain("one separate fabrication model",window.FabricationModelCount()==1)) { return false; }
    window.RunCommand("edit.undo");
    if (!Explain("single undo preserves original surface",CountOfKind(window,domain::EntityKind::FabricationModel)==0
        && CountOfKind(window,domain::EntityKind::GuideSurface)==1)) { return false; }
    window.RunCommand("edit.redo");
    if (!Explain("save/reopen independent method",window.SaveAndReopen(QStringLiteral("gpt_fabrication.kcd2"))
        && window.FabricationModelCount()==1)) { return false; }
    window.RunCommand("fabrication.create_pattern");
    QApplication::processEvents(); window.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-gpt-fabrication-pattern.png"));
    if (!Explain("pattern view available",window.ShelfShown(app::Shelf::Pattern))) { return false; }
    auto& output=window.ExportDock();
    if (!output.ChooseTarget(app::ExportTarget::CurrentPattern)) { return false; }
    for (const auto format:{app::ExportFormat::Svg,app::ExportFormat::Dxf}) {
        if (!output.ChooseFormat(format)) { return false; }
        const auto path=QDir::tempPath()+QStringLiteral("/kachakacha-gpt-pattern")
            +(format==app::ExportFormat::Svg ? QStringLiteral(".svg") : QStringLiteral(".dxf"));
        QFile::remove(path); output.ChoosePath(path);
        if (!Explain("write actual pattern",output.CanRun() && output.RunNow())) { return false; }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size()<100) { return false; }
    }
    return true;
}
bool HeadFabrication(V2MainWindow& window)
{
    if (!MakeGptHeadFixture(window)) { return false; }
    window.Viewport().SetSelection(app::SelectAllOfKind(window.Session().GetDocument().Snapshot(),domain::EntityKind::GuideSurface));
    window.RunCommand("fabrication.gpt_create");
    auto* width=window.findChild<QDoubleSpinBox*>(QStringLiteral("gptFabricationWidth"));
    if (!width) { return false; } width->setValue(.2);
    if (!ClickGpt(window,"gptFabricationPreview")) { return false; }
    auto* status=window.findChild<QLabel*>(QStringLiteral("gptFabricationStatus"));
    if (status) { Note(status->text().toStdString().c_str()); }
    QApplication::processEvents();
    window.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-gpt-fabrication-head-preview.png"));
    if (!ClickGpt(window,"gptFabricationConfirm")) { return false; }
    QApplication::processEvents();
    window.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-gpt-fabrication-head.png"));
    if (!Explain("head approximation and original retained",window.FabricationModelCount()==1
        && CountOfKind(window,domain::EntityKind::GuideSurface)==1)) { return false; }
    window.RunCommand("fabrication.create_pattern"); QApplication::processEvents();
    window.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-gpt-fabrication-head-pattern.png"));
    return Explain("head pattern available",window.ShelfShown(app::Shelf::Pattern));
}

}
std::vector<SelfTestCase> GptFabricationCases()
{
    return {{"HP-GPT-F01 GPT近似・取消・確定・Undo・保存再読込・型紙",Fabrication},
        {"HP-GPT-F02 前頭部のGPT近似・番号表示・型紙",HeadFabrication}};
}
}
