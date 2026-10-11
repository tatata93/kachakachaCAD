// User-requested practical fixtures: native document -> visible tools -> print solids.
#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2FabricationDock.h"
#include "V2ThickenDock.h"
#include "V2PatternDock.h"
#include "V2OutputTool.h"
#include "V2ExportDock.h"
#include "V2BooleanDock.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/io/AtomicFile.h"
#include <QApplication>
#include <QChar>
#include <QString>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPixmap>
#include <QTextStream>
#include <QXmlStreamReader>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <QStringList>

namespace kachakacha::v2::selftest {
bool CheckPracticalUnfold(V2MainWindow& window,const app::FabricationEvaluation& result);
bool CheckPracticalFreezeSweep(V2MainWindow& window,const QString& input,const QString& directory);
namespace {
void Stage(const QString& text)
{
    std::cout << "PRACTICAL STAGE " << text.toUtf8().constData() << std::endl;
}
app::SelectionSet ManufacturingGuides(V2MainWindow& w)
{
    app::SelectionSet result;
    for(const auto& e:w.Session().GetDocument().Snapshot().entities)
        if(e.kind==domain::EntityKind::GuideSurface&&e.visibility==domain::Visibility::Visible&&!e.construction)result.entityIds.push_back(e.id);
    return result;
}
bool SaveNative(V2MainWindow& w,const QString& path)
{
    Stage("save native: " + path);
    w.SetPathChooser([path](bool){return path;});w.RunCommand("file.save_as");
    if(!QFile::exists(path))return Explain("persistent native file exists",false);
    w.RunCommand("file.new");
    Stage("reopen native: " + path);
    if(!w.OpenDocumentFile(path)){Note(w.StatusText().toUtf8().constData());return false;}
    if(!w.RebuildProblems().isEmpty()){Note(w.RebuildProblems().toUtf8().constData());return false;}
    Stage("reopen complete: " + path);
    return true;
}
void Shot(V2MainWindow& w,const QString& path)
{
    w.Viewport().SetViewDirection(ViewDirection::Isometric);
    w.Viewport().FitToDocument();
    QApplication::processEvents();w.grab().save(path);
}
bool ExportParts(V2MainWindow& w,const QString& dir,const QString& stem)
{
    const auto selected=app::SelectAllOfKind(w.Session().GetDocument().Snapshot(),domain::EntityKind::Part);
    if(!Explain("print stage has solids",!selected.entityIds.empty()))return false;
    const auto revision=w.Session().GetDocument().Revision();
    w.Viewport().SetSelection(selected);auto* output=V2OutputTool::Open(w,0);
    if(!Explain("all solids exported through output dialog",output->SaveTo(dir+"/"+stem+".stl"))) {
        for(const auto* label:output->findChildren<QLabel*>())Note(label->text().toUtf8().constData());
        output->grab().save(dir+"/failed-output.png");output->close();return false;
    }
    output->ChooseFormat(1);const bool step=output->SaveTo(dir+"/"+stem+".step");output->close();
    if(!step)return Explain("STEP export",false);
    int index=0;
    for(const auto& id:selected.entityIds) {
        app::SelectionSet one;one.entityIds.push_back(id);w.Viewport().SetSelection(one);
        auto* single=V2OutputTool::Open(w,0);
        const auto name=dir+"/"+stem+QString("-part-%1").arg(++index,3,10,QChar('0'));
        bool ok=single->SaveTo(name+".stl");single->ChooseFormat(2);ok=single->SaveTo(name+".kcd2")&&ok;
        single->close();if(!ok)return Explain("separate component output",false);
    }
    return Explain("export has no source mutation",w.Session().GetDocument().Revision()==revision);
}
bool ThickenGroup(V2MainWindow& w,const app::SelectionSet& guides,fabrication::ThicknessPlacement placement)
{
    const auto before=CountOfKind(w,domain::EntityKind::Part);
    w.SetMode(app::UiMode::Part);w.Viewport().SetSelection(guides);w.RunCommand("part.thicken");
    QApplication::processEvents();
    auto* amount=w.findChild<QDoubleSpinBox*>("thickenAmount");
    if(!amount||!amount->isVisible()) {
        Note(w.StatusText().toUtf8().constData());
        return Explain("visible thickness input",false);
    }
    const auto thickness=qEnvironmentVariable("KACHACAD_PRACTICAL_THICKNESS").toDouble();
    amount->setValue(thickness>0?thickness:.8);
    if(!w.ThickenDock().ClickPlacementCard(placement))return false;
    if(!w.ThickenDock().ClickConfirm()) {
        Note(w.ThickenDock().StatusTextJa().toUtf8().constData());
        Note(w.StatusText().toUtf8().constData());
        Shot(w,qEnvironmentVariable("KACHACAD_PRACTICAL_OUTPUT")+"/failed-thickness.png");return false;
    }
    Note(w.StatusText().toUtf8().constData());
    return Explain("each guide produces a closed solid",CountOfKind(w,domain::EntityKind::Part)==before+guides.entityIds.size());
}
bool ThickenGuides(V2MainWindow& w,const app::SelectionSet& guides)
{
    const auto point=qEnvironmentVariable("KACHACAD_PRACTICAL_INTERIOR_POINT").split(',');
    if(point.size()!=3)return ThickenGroup(w,guides,fabrication::ThicknessPlacement::Inside);
    // Explicit reference-study interior point: choose the visible direction card
    // for planar sheets. Plane fitting uses a canonical normal, not wire winding.
    const geometry::Vector3 interior{point[0].toDouble(),point[1].toDouble(),point[2].toDouble()};
    app::SelectionSet inside,outside;
    for(const auto id:guides.entityIds) {
        bool found=false,useOutside=false;
        for(const auto& view:w.Viewport().ShapeViews()) {
            if(view.entityId!=id||view.mesh.triangles.empty())continue;
            found=true;
            const auto& first=view.mesh.triangles.front();
            bool planar=true;
            for(const auto& triangle:view.mesh.triangles)for(const auto p:triangle.points)
                if(std::abs(geometry::Dot(first.normal,p-first.points[0]))>1e-6)planar=false;
            useOutside=planar&&geometry::Dot(first.normal,interior-first.points[0])>0;
            break;
        }
        if(!Explain("guide direction visible for thickness choice",found))return false;
        (useOutside?outside:inside).entityIds.push_back(id);
    }
    Stage(QString("thickness direction groups: inside=%1 outside=%2").arg(inside.entityIds.size()).arg(outside.entityIds.size()));
    return (outside.entityIds.empty()||ThickenGroup(w,outside,fabrication::ThicknessPlacement::Outside))
        &&(inside.entityIds.empty()||ThickenGroup(w,inside,fabrication::ThicknessPlacement::Inside));
}
bool ExportAssembly(V2MainWindow& w,const QString& dir)
{
    Stage("assembly start: " + dir);
    QDir().mkpath(dir);
    const auto before=app::SelectAllOfKind(w.Session().GetDocument().Snapshot(),domain::EntityKind::Part);
    app::SelectionSet original;
    for(const auto& entity:w.Session().GetDocument().Snapshot().entities)
        if(entity.kind==domain::EntityKind::Part&&entity.visibility==domain::Visibility::Visible
            &&entity.displayName.find("[separate]")==std::string::npos)original.entityIds.push_back(entity.id);
    if(original.entityIds.size()<2)return true;
    Stage(QString("union preview: %1 parts").arg(original.entityIds.size()));
    w.SetMode(app::UiMode::Part);w.Viewport().SetSelection(original);w.RunCommand("part.boolean_add");
    QApplication::processEvents();
    Stage("union preview complete; confirm/rebuild");
    if(!w.BooleanDock().ClickConfirm()){
        Note(w.BooleanDock().StatusTextJa().toUtf8().constData());
        Note(w.StatusText().toUtf8().constData());return false;
    }
    Stage("union confirm/rebuild complete");
    const auto now=app::SelectAllOfKind(w.Session().GetDocument().Snapshot(),domain::EntityKind::Part);
    app::SelectionSet assembly;
    for(const auto& id:now.entityIds)
        if(std::find(before.entityIds.begin(),before.entityIds.end(),id)==before.entityIds.end())assembly.entityIds.push_back(id);
    if(!Explain("union creates one assembly entity",assembly.entityIds.size()==1))return false;
    w.Viewport().SetSelection(assembly);Shot(w,dir+"/07-print-assembly.png");
    Stage("assembly STL export");
    auto* output=V2OutputTool::Open(w,0);bool ok=output->SaveTo(dir+"/assembly.stl");
    Stage("assembly STEP export");
    output->ChooseFormat(1);ok=output->SaveTo(dir+"/assembly.step")&&ok;
    if(!ok){for(const auto* label:output->findChildren<QLabel*>())Note(label->text().toUtf8().constData());output->grab().save(dir+"/failed-step.png");}
    output->close();
    if(!ok){Note(w.StatusText().toUtf8().constData());return false;}
    return SaveNative(w,dir+"/assembly.kcd2");
}
bool ExportPatterns(V2MainWindow& w,const QString& dir)
{
    const auto scale=qEnvironmentVariable("KACHACAD_PRACTICAL_REFERENCE_SCALE");
    if(!scale.isEmpty()&&!w.ParameterDock().Apply(app::ParameterId::ScaleDenominator,scale))return false;
    const auto models=app::SelectAllOfKind(w.Session().GetDocument().Snapshot(),domain::EntityKind::FabricationModel);
    int index=0;
    for(const auto& id:models.entityIds) {
        w.Viewport().SetSelection({});w.RunCommand("fabrication.create_pattern");
        w.Viewport().SetSelection(app::SelectionSet{{id}});
        QApplication::processEvents();Note(w.StatusText().toUtf8().constData());
        if(!Explain("pattern pages produced for each model",w.PatternDock().PageCount()>0)) {
            Note(w.PatternDock().SummaryText().toUtf8().constData());Shot(w,dir+"/failed-pattern.png");return false;
        }
        const auto stem=dir+QString("/pattern-%1").arg(++index,3,10,QChar('0'));
        w.grab().save(stem+".png");w.RefreshExportCounts();auto& output=w.ExportDock();
        for(const auto format:{app::ExportFormat::Svg,app::ExportFormat::Dxf,app::ExportFormat::Pdf}) {
            if(!output.ChooseTarget(app::ExportTarget::CurrentPattern)||!output.ChooseFormat(format))return false;
            output.ChoosePath(stem+QString::fromUtf8(app::ExportFormatExtension(format).data()));output.SetOverwrite(true);
            if(!output.RunNow()){Note(output.LastMessage().toUtf8().constData());return false;}
            if(format==app::ExportFormat::Svg) {
                QFile file(stem+".svg");if(!file.open(QIODevice::ReadOnly))return false;
                QXmlStreamReader xml(&file);while(!xml.atEnd())xml.readNext();
                if(!Explain("exported SVG reparses as XML",!xml.hasError())) {
                    Note(xml.errorString().toUtf8().constData());return false;
                }
            }
        }
        // Exercise the same page navigation used by the visible preview.
        // Page 1 retains the existing file name; subsequent sheets are explicit.
        for(int page=1;page<w.PatternDock().PageCount();++page) {
            w.PatternDock().ShowPage(page);QApplication::processEvents();
            const auto pageStem=stem+QString("-page-%1").arg(page+1,3,10,QChar('0'));
            w.grab().save(pageStem+".png");
            for(const auto format:{app::ExportFormat::Svg,app::ExportFormat::Dxf}) {
                if(!output.ChooseTarget(app::ExportTarget::CurrentPattern)||!output.ChooseFormat(format))return false;
                const auto path=pageStem+QString::fromUtf8(app::ExportFormatExtension(format).data());
                output.ChoosePath(path);output.SetOverwrite(true);
                if(!output.RunNow())return false;
                if(format==app::ExportFormat::Svg) {
                    QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;
                    QXmlStreamReader xml(&file);while(!xml.atEnd())xml.readNext();
                    if(!Explain("subsequent SVG sheet reparses as XML",!xml.hasError()))return false;
                }
            }
        }
    }
    return index>0;
}
bool Practical(V2MainWindow& w)
{
    const auto input=qEnvironmentVariable("KACHACAD_PRACTICAL_FILE");
    const auto dir=qEnvironmentVariable("KACHACAD_PRACTICAL_OUTPUT");
    if(input.isEmpty()||dir.isEmpty())return false;QDir().mkpath(dir);
    const auto original=io::ReadWholeFile(input.toStdString());if(!original.HasValue())return false;
    Stage("open input: "+input);
    if(!w.OpenDocumentFile(input)) {Note(w.StatusText().toUtf8().constData());return false;}
    Stage("input open complete");
    if(!Explain("input rebuilt without errors",w.RebuildProblems().isEmpty())) {
        Note(w.RebuildProblems().toUtf8().constData());return false;
    }
    w.resize(1500,980);w.Viewport().FitToDocument();QApplication::processEvents();
    if(qEnvironmentVariable("KACHACAD_PRACTICAL_STAGE")=="freeze-sweep")
        return CheckPracticalFreezeSweep(w,input,dir);
    if(qEnvironmentVariable("KACHACAD_PRACTICAL_STAGE")=="pattern") {
        return ExportPatterns(w,dir);
    }
    if(qEnvironmentVariableIsSet("KACHACAD_PRACTICAL_PART_VIEW")) {
        app::SelectionSet hidden;
        for(const auto& e:w.Session().GetDocument().Snapshot().entities)
            if(e.kind!=domain::EntityKind::Part)hidden.entityIds.push_back(e.id);
        w.Viewport().SetSelection(hidden);w.RunCommand("view.hide_selected");w.Viewport().SetSelection({});
        auto settings=w.Viewport().DisplaySettingsNow();settings.gridVisible=false;settings.workPlaneVisible=false;
        w.Viewport().SetDisplaySettings(settings);
    }
    if(qEnvironmentVariable("KACHACAD_PRACTICAL_STAGE")=="assembly")return ExportAssembly(w,dir);
    if(qEnvironmentVariable("KACHACAD_PRACTICAL_STAGE")=="export") {
        app::SelectionSet selected;
        for(const auto& entity:w.Session().GetDocument().Snapshot().entities)
            if(entity.kind==domain::EntityKind::Part&&entity.visibility==domain::Visibility::Visible
                &&entity.displayName.find("[separate]")==std::string::npos)selected.entityIds.push_back(entity.id);
        w.Viewport().SetSelection(selected);auto* output=V2OutputTool::Open(w,0);
        bool ok=output->SaveTo(dir+"/assembly.stl");output->ChooseFormat(1);
        ok=output->SaveTo(dir+"/assembly.step")&&ok;output->ChooseFormat(2);
        ok=output->SaveTo(dir+"/assembly-independent.kcd2")&&ok;output->close();
        if(!ok)return false;
        w.RunCommand("file.new");
        return w.OpenDocumentFile(dir+"/assembly-independent.kcd2")&&w.RebuildProblems().isEmpty();
    }
    Shot(w,dir+"/01-ideal-surfaces.png");
    if(qEnvironmentVariable("KACHACAD_PRACTICAL_STAGE")=="inspect")return true;
    auto guides=ManufacturingGuides(w);
    if(qEnvironmentVariable("KACHACAD_PRACTICAL_STAGE")!="approximation") {
    if(!ThickenGuides(w,guides))return false;
    Shot(w,dir+"/02-ideal-solids.png");
    if(!ExportParts(w,dir,"ideal")||!SaveNative(w,dir+"/ideal.kcd2"))return false;
    if(qEnvironmentVariable("KACHACAD_PRACTICAL_STAGE")=="ideal")return true;
    if(!ExportAssembly(w,dir+"/ideal-assembly"))return false;
    }
    // Re-open original: approximation must not depend on the thickening result.
    if(!w.OpenDocumentFile(input))return false;
    auto* automatic=w.findChild<QComboBox*>("fabricationAutomaticGeneration");
    if(!automatic)return false;automatic->setCurrentIndex(0);
    auto& dock=w.FabricationDock();dock.SetGenerationDestination(0);
    w.Viewport().SetSelection({});w.RunCommand("fabrication.create");
    w.Viewport().SetSelection(ManufacturingGuides(w));
    QApplication::processEvents();
    if(!dock.ClickCandidate(1))return Explain("select band candidate",false);
    auto* materialThickness=w.findChild<QDoubleSpinBox*>("fabricationThickness");
    if(!materialThickness)return false;
    const double requestedThickness=qEnvironmentVariable("KACHACAD_PRACTICAL_THICKNESS").toDouble();
    materialThickness->setValue(requestedThickness>0?requestedThickness:.8);
    const double requestedDeviation=qEnvironmentVariable("KACHACAD_PRACTICAL_DEVIATION").toDouble();
    if(requestedDeviation>0) {
        auto* deviation=w.findChild<QDoubleSpinBox*>("fabricationTolerance");
        if(!deviation)return false;deviation->setValue(requestedDeviation);
    }
    const int requestedCount=qEnvironmentVariableIntValue("KACHACAD_PRACTICAL_PARTS");
    dock.SetEqualPartCount(requestedCount>0?requestedCount:12);dock.SetSplitAxisIndex(2);
    const auto& preview=w.ApproxEvaluations();
    if(preview.size()<2||!preview[1]||!CheckPracticalUnfold(w,*preview[1]))return false;
    Note(QStringLiteral("Approximation max deviation=%1mm requested tolerance reached=%2")
        .arg(preview[1]->maximumDeviationMm,0,'g',12).arg(preview[1]->reachedTolerance).toUtf8().constData());
    if(requestedDeviation>0&&!Explain("requested approximation tolerance reached",preview[1]->reachedTolerance))return false;
    Shot(w,dir+"/03-approximation-preview.png");
    w.Viewport().SetSelection(ManufacturingGuides(w));
    w.HandleToolKey(Qt::Key_Return,nullptr);
    if(!Explain("one approximation per source guide confirmed",w.FabricationModelCount()==guides.entityIds.size())){Note(w.StatusText().toUtf8().constData());return false;}
    if(!SaveNative(w,dir+"/approximation.kcd2"))return false;
    auto model=app::SelectAllOfKind(w.Session().GetDocument().Snapshot(),domain::EntityKind::FabricationModel);
    if(!ExportPatterns(w,dir))return false;
    w.Viewport().SetSelection(model);
    dock.SetPartNumbersText(QStringLiteral("すべて"));dock.TypeAssemblyPercent(100);dock.PressApplyAssembly();
    auto* format=w.findChild<QComboBox*>("fabricationGenerationOutput");if(!format)return false;
    format->setCurrentIndex(3);w.RunCommand("fabrication.freeze_state");
    app::SelectionSet generated;
    for(const auto& e:w.Session().GetDocument().Snapshot().entities)
        if(e.generatedFrom&&e.kind==domain::EntityKind::GuideSurface)generated.entityIds.push_back(e.id);
    if(!Explain("approximation panel faces generated",!generated.entityIds.empty())) {
        Note(w.StatusText().toUtf8().constData());Shot(w,dir+"/failed-generation.png");return false;
    }
    Shot(w,dir+"/05-assembled-panels.png");
    if(!ThickenGuides(w,generated))return false;
    Shot(w,dir+"/06-print-panels.png");
    if(!ExportParts(w,dir,"approximation")||!SaveNative(w,dir+"/print-panels.kcd2"))return false;
    if(!ExportAssembly(w,dir))return false;
    const auto after=io::ReadWholeFile(input.toStdString());
    return Explain("input file unchanged",after.HasValue()&&after.Value()==original.Value());
}
}
std::vector<SelfTestCase> PracticalCases()
{
    if(!qEnvironmentVariableIsSet("KACHACAD_PRACTICAL_FILE"))return {};
    return {{"HP-PRACTICAL native fixture through ideal and approximate print solids",Practical}};
}
}
