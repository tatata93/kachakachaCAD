#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2DrawingDock.h"
#include "V2FabricationDock.h"
#include "V2ParameterDock.h"
#include "kachakacha/app/DirectWireEntry.h"
#include "kachakacha/app/Selection.h"
#include <QPushButton>
#include <QCheckBox>
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
bool GenerationSelectedSurface(V2MainWindow& window);
bool GenerationSeparateDocument(V2MainWindow& window);
bool GenerationGptAutomatic(V2MainWindow& window);
bool GenerationBendStates(V2MainWindow& window);
bool MakeCurvedGuideSurface(V2MainWindow& window);
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
bool RestartAdaptivePreview(V2MainWindow& window)
{
    if (!MakeCurvedGuideSurface(window)) { return false; }
    auto& dock=window.FabricationDock();
    auto* adaptive=window.findChild<QCheckBox*>(QStringLiteral("fabricationAdaptiveSpacing"));
    QPushButton* action=nullptr;
    for (auto* button:window.findChildren<QPushButton*>(QStringLiteral("panelConfirm"))) {
        if (button->text().contains(QStringLiteral("始める"))) { action=button; break; }
    }
    if (!Explain("find adaptive controls",adaptive && action)) { return false; } adaptive->setChecked(true);
    const auto revision=window.Session().GetDocument().Revision();
    window.RunCommand("fabrication.create");
    if (!Explain("cancel approximation",dock.ClickCancelApprox())) { return false; }
    if (!Explain("cancel returns a usable start button without changing document",
        action->isEnabled() && action->text().contains(QStringLiteral("始める"))
        && window.Viewport().ToolPreview().empty() && window.Session().GetDocument().Revision()==revision)) { return false; }
    action->click();
    if (!Explain("same source restarts from the panel button",!window.Viewport().ToolPreview().empty())) { return false; }
    dock.SetEqualPartCount(3);
    if (!Explain("count change recomputes preview",window.ApproxOutcomes()[1].partCount==3)) { return false; }
    dock.SetManualBoundariesText(QStringLiteral("0.2, abc"));
    if (!Explain("invalid settings cannot confirm stale geometry",!action->isEnabled()
        && window.Viewport().ToolPreview().empty() && dock.MessageText().contains(QStringLiteral("UI-F001")))) { return false; }
    window.RunCommand("fabrication.preview_update");
    if (!Explain("explicit refresh also rejects invalid settings",!action->isEnabled()
        && window.Viewport().ToolPreview().empty())) { return false; }
    dock.SetManualBoundariesText(QString());
    if (!Explain("correcting settings restores preview",action->isEnabled() && !window.Viewport().ToolPreview().empty())) { return false; }
    dock.SetEqualPartCount(9);
    if (!window.ParameterDock().Apply(app::ParameterId::MaxDeviationMm,QStringLiteral("5"))) { return false; }
    if (!Explain("loose tolerance is reflected immediately",window.ApproxOutcomes()[1].reachedTolerance)) { return false; }
    if (!window.ParameterDock().Apply(app::ParameterId::MaxDeviationMm,QStringLiteral("0.001"))) { return false; }
    if (!Explain("tight tolerance is reflected immediately",!window.ApproxOutcomes()[1].reachedTolerance)) { return false; }
    if (!Explain("cancel approximation",dock.ClickCancelApprox())) { return false; }
    action->click();
    if (!Explain("second restart retains changed count",window.ApproxOutcomes().size()==3
        && window.ApproxOutcomes()[1].partCount==9
        && !window.Viewport().ToolPreview().empty())) { return false; }
    action->click();
    return Explain("restarted preview confirms once",window.FabricationModelCount()==1);
}
bool SmallAdaptivePreview(V2MainWindow& window)
{
    if (!MakeGptHeadFixture(window)) { return false; }
    window.Viewport().SetSelection(app::SelectAllOfKind(window.Session().GetDocument().Snapshot(),domain::EntityKind::GuideSurface));
    auto& dock=window.FabricationDock();
    auto choice=dock.Choice(); choice.equalPartCount=9; choice.minimumPartWidthMm=4;
    choice.maximumPartCount=12; choice.splitAtCorners=true; choice.adaptiveSpacing=false;
    dock.SetChoice(choice);
    auto* checkbox=window.findChild<QCheckBox*>(QStringLiteral("fabricationAdaptiveSpacing"));
    if (!checkbox) { return false; } checkbox->setChecked(true);
    window.RunCommand("fabrication.create");
    const auto& outcomes=window.ApproxOutcomes();
    if (!Explain("small head with 9 parts and 4 mm minimum remains available",
        outcomes.size()==3 && outcomes[1].available && outcomes[1].partCount==9
        && outcomes[2].available && outcomes[2].partCount==1)) { return false; }
    if (!dock.ClickCandidate(1)) { return false; }
    if (!Explain("adaptive preview is visible",!window.Viewport().ToolPreview().empty())) { return false; }
    if (!Explain("minimum-width conflict is visible before confirmation",
        dock.MessageText().contains(QStringLiteral("最小幅")) && dock.MessageText().contains(QStringLiteral("指定枚数を優先")))) { return false; }
    QApplication::processEvents();
    window.grab().save(QDir::tempPath()+QStringLiteral("/kachakacha-small-adaptive-preview.png"));
    window.RunCommand("fabrication.create");
    if (!Explain("small adaptive model can be confirmed",window.FabricationModelCount()==1)) { return false; }
    if (!window.SaveAndReopen(QStringLiteral("small_adaptive.kcd2"))) { return false; }
    window.RunCommand("fabrication.create_pattern");
    return Explain("small adaptive model reopens and produces pattern",
        window.FabricationModelCount()==1 && window.ShelfShown(app::Shelf::Pattern));
}
bool ClassicAdaptive(V2MainWindow& window)
{
    if (!MakeCurvedGuideSurface(window)) { return false; }
    auto* checkbox=window.findChild<QCheckBox*>(QStringLiteral("fabricationAdaptiveSpacing"));
    if (!checkbox) { return false; }
    checkbox->setChecked(true);
    window.RunCommand("fabrication.create"); window.RunCommand("fabrication.create");
    if (!Explain("classic adaptive model created",window.FabricationModelCount()==1)) { return false; }
    if (!window.SaveAndReopen(QStringLiteral("classic_adaptive.kcd2"))) { return false; }
    bool found=false;
    for (const auto& feature:window.Session().GetDocument().Snapshot().features) {
        const auto* definition=std::get_if<domain::CreateFabricationModelDefinition>(&feature.definition);
        if (definition) { found=definition->adaptiveSpacing; }
    }
    if (!Explain("classic adaptive setting saved and rebuilt",found && window.FabricationModelCount()==1)) { return false; }
    window.RunCommand("fabrication.create_pattern");
    return Explain("adaptive classic pattern available",window.ShelfShown(app::Shelf::Pattern));
}
bool Fabrication(V2MainWindow& window);
bool AdaptiveControls(V2MainWindow& window)
{
    auto* classic=window.findChild<QCheckBox*>(QStringLiteral("fabricationAdaptiveSpacing"));
    auto* gpt=window.findChild<QCheckBox*>(QStringLiteral("gptFabricationAdaptiveSpacing"));
    if (!classic || !gpt || classic->isChecked() || gpt->isChecked()) { return false; }
    classic->setChecked(true);
    const auto choice=window.FabricationDock().Choice();
    if (!Explain("classic adaptive spacing is a real model option",choice.adaptiveSpacing)) { return false; }
    window.FabricationDock().SetChoice(choice);
    if (!classic->isChecked()) { return false; }
    classic->setChecked(false);
    gpt->setChecked(true);
    return Fabrication(window);
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
    auto* adaptive=window.findChild<QCheckBox*>(QStringLiteral("gptFabricationAdaptiveSpacing"));
    const bool spacing=adaptive->isChecked(); adaptive->setChecked(!spacing);
    auto* confirm=window.findChild<QPushButton*>(QStringLiteral("gptFabricationConfirm"));
    if (!Explain("spacing changes invalidate old preview",confirm && !confirm->isEnabled())) { return false; }
    adaptive->setChecked(spacing);
    if (!ClickGpt(window,"gptFabricationPreview")) { return false; }
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
    for (const auto& feature:window.Session().GetDocument().Snapshot().features) {
        const auto* definition=std::get_if<domain::CreateFabricationModelDefinition>(&feature.definition);
        if (definition && !Explain("adaptive spacing survives save/rebuild",definition->adaptiveSpacing==spacing)) { return false; }
    }
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
        {"HP-GPT-F02 前頭部のGPT近似・番号表示・型紙",HeadFabrication},
        {"HP-GPT-F03 自動間隔・両方の棚・保存・型紙",AdaptiveControls},
        {"HP-GPT-F04 従来帯近似の自動間隔・保存・型紙",ClassicAdaptive},
        {"HP-GPT-F05 小さい前頭部9枚・最小幅4mmでも下見と確定",SmallAdaptivePreview},
        {"HP-GPT-F06 取消と設定変更から近似をやり直す",RestartAdaptivePreview},
        {"HP-GEN-01 自動生成・選択部材・短辺・近似面・保存",GenerationSelectedSurface},
        {"HP-GEN-02 選択部材の別文書生成・保存取消",GenerationSeparateDocument},
        {"HP-GEN-03 GPT近似確定時のワイヤーと面生成",GenerationGptAutomatic},
        {"HP-GEN-04 選択部材の0・37・100%生成形状",GenerationBendStates}};
}
}
