#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2FabricationDock.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctFaceQuery.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPixmap>
#include <QPushButton>
#include <QString>
#include <QWidget>
#include <cmath>

namespace kachakacha::v2::selftest {
bool MakeCurvedGuideSurface(V2MainWindow& window);
namespace {
bool QuickSettings(V2MainWindow& window)
{
    if(!MakeCurvedGuideSurface(window))return false;
    window.RunCommand("fabrication.create");
    auto* details=window.findChild<QPushButton*>(QStringLiteral("fabricationDetailsToggle"));
    auto* method=window.findChild<QComboBox*>(QStringLiteral("fabricationMethod"));
    if(!details || !method || !Explain("方式は通常隠れている",!details->isChecked() && !method->isVisible()))return false;
    for(const char* id:{"fabricationPartCount","fabricationTolerance","fabricationDirection","fabricationAdaptiveSpacing"}) {
        auto* field=window.findChild<QWidget*>(QString::fromLatin1(id));
        if(!Explain(id,field && field->isVisible()))return false;
    }
    if(!Explain("GPT製作近似の新規入口を廃止",!app::FindCommand("fabrication.gpt_create")
        && !app::FindCommand("fabrication.gpt_from_face") && app::FindCommand("surface.gpt_create")))return false;
    window.FabricationDock().SetEqualPartCount(7);
    auto* adaptive=window.findChild<QCheckBox*>(QStringLiteral("fabricationAdaptiveSpacing"));
    adaptive->setChecked(true);
    if(!Explain("枚数と自動間隔で下見が更新",window.ApproxOutcomes()[1].partCount==7
        && window.Viewport().ToolPreviewFaceCount()>0))return false;
    details->click();if(!Explain("詳細を開けば方式を選べる",method->isVisible()))return false;
    details->click();
    return Explain("詳細を閉じても下見を維持",!method->isVisible() && window.Viewport().ToolPreviewFaceCount()>0);
}
bool GeneratedMatchesRails(V2MainWindow& window)
{
    const geometry::GeometryTolerance tolerance;
    double maximum=0;int count=0;
    for(const auto& feature:window.Session().GetDocument().Snapshot().features) {
        const auto* guide=std::get_if<domain::CreateGuideSurfaceDefinition>(&feature.definition);
        if(!guide || !guide->lockSectionOrder || guide->chains.size()!=2)continue;
        const auto table=app::GuideTableFromDefinition(window.Session().GetDocument(),window.Session().Scene(),*guide);
        if(!table.HasValue())return false;
        const auto request=modeling::ToGuideSurfaceRequest(table.Value(),tolerance);if(!request.HasValue())return false;
        const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request.Value(),tolerance);if(!analysis.HasValue())return false;
        const auto built=kernel::BuildGuideSurface(request.Value(),analysis.Value(),tolerance);if(!built.HasValue())return false;
        const auto value=kernel::FabricationEvaluatorOf(built.Value().handle);if(!value.HasValue())return false;
        const auto& a=request.Value().chains[0].segments;const auto& b=request.Value().chains[1].segments;
        if(a.size()!=b.size())return false;
        double gap=0;
        for(std::size_t i=1;i<a.size();++i)gap=std::max({gap,
            geometry::Distance(a[i-1].EndPoint(),a[i].StartPoint()),
            geometry::Distance(b[i-1].EndPoint(),b[i].StartPoint())});
        Note(QStringLiteral("生成レール: 方式%1 順序固定%2 点数%3 接続差%4mm")
            .arg(static_cast<int>(request.Value().method)).arg(request.Value().keepSectionOrder)
            .arg(a.size()).arg(gap,0,'g',8).toUtf8().constData());
        for(std::size_t i=0;i<a.size();++i)for(double along:{0.0,.5,1.0})for(double across:{0.0,.5,1.0}) {
            const auto p=a[i].StartPoint()*(1-along)+a[i].EndPoint()*along;
            const auto q=b[i].StartPoint()*(1-along)+b[i].EndPoint()*along;
            maximum=std::max(maximum,geometry::Distance(value.Value()(across,(i+along)/a.size()),p*(1-across)+q*across));
        }
        kernel::ReleaseShape(built.Value().handle);++count;
    }
    Note(QStringLiteral("生成面とレールの点対応: %1面 / 最大差%2mm").arg(count).arg(maximum,0,'g',8).toUtf8().constData());
    return Explain("生成面の外周短辺内部は下見と同じ対応",count==12 && maximum<1e-7);
}
bool MeasureOriginalDeviation(const app::FabricationSource& source,
    const domain::CreateFabricationModelDefinition& definition,const app::FabricationEvaluation& result)
{
    const fabrication::SampledSurface original(*source.samples,source.evaluate);
    const auto axis=fabrication::ResolveSplitAxis(original,app::SplitDirectionOf(definition));
    const auto& face=result.bandFaces.front();double maximum=0,squares=0;int count=0;
    for(int band=0;band<face.mesh.BandCount();++band)for(int k=0;k+1<face.mesh.columns;++k)
        for(double along:{.25,.5,.75})for(double across:{.1,.3,.5,.7,.9}) {
            const auto a=face.mesh.world[band][k]*(1-along)+face.mesh.world[band][k+1]*along;
            const auto b=face.mesh.world[band+1][k]*(1-along)+face.mesh.world[band+1][k+1]*along;
            const double t=face.bands.railParameters[band]*(1-across)+face.bands.railParameters[band+1]*across;
            const double s=face.mesh.longitudinalParameters[k]*(1-along)+face.mesh.longitudinalParameters[k+1]*along;
            const auto p=original.EvaluateSplit(axis,t,s);
            const auto chord=b-a;
            const double fraction=chord.LengthSquared()>1e-18
                ? std::clamp(geometry::Dot(p-a,chord)/chord.LengthSquared(),0.0,1.0) : 0;
            const double distance=geometry::Distance(p,a+chord*fraction);
            maximum=std::max(maximum,distance);squares+=distance*distance;++count;
        }
    Note(QStringLiteral("元の実曲面との独立標本検査: %1点 最大%2mm RMS%3mm / 表示偏差%4mm")
        .arg(count).arg(maximum,0,'g',8).arg(std::sqrt(squares/count),0,'g',8)
        .arg(result.maximumDeviationMm,0,'g',8).toUtf8().constData());
    return Explain("表示する誤差と実際の生成形状が整合",count>1000
        && maximum<=result.maximumDeviationMm*1.15+.001);
}
bool OwnerOutline(V2MainWindow& window)
{
    if(!window.OpenDocumentFile(qEnvironmentVariable("KACHACAD_SURFACE_TEST_FILE")))return false;
    window.resize(1600,1000);window.Viewport().SetViewDirection(ViewDirection::Isometric);
    window.Viewport().FitToDocument();
    const auto original=window.Session().GetDocument().Snapshot();
    const domain::CreateGuideSurfaceDefinition* guide=nullptr;
    for(const auto& feature:original.features)
        if(const auto* definition=std::get_if<domain::CreateGuideSurfaceDefinition>(&feature.definition))guide=definition;
    if(!guide)return false;
    const geometry::GeometryTolerance tolerance;
    const auto table=app::GuideTableFromDefinition(window.Session().GetDocument(),window.Session().Scene(),*guide);
    if(!table.HasValue())return false;
    const auto request=modeling::ToGuideSurfaceRequest(table.Value(),tolerance);
    if(!request.HasValue())return false;
    const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request.Value(),tolerance);
    if(!analysis.HasValue())return false;
    const auto built=kernel::BuildGuideSurface(request.Value(),analysis.Value(),tolerance);
    if(!built.HasValue())return false;
    const auto native=kernel::FabricationEvaluatorOf(built.Value().handle);
    if(!native.HasValue()){Note(native.FirstMessageJa().c_str());return false;}
    app::FabricationSource source;source.name="115屋根";source.samples=built.Value().samples;source.evaluate=native.Value();
    int checked=0;
    for(int axis=0;axis<5;++axis)for(bool adaptive:{false,true}) {
        domain::CreateFabricationModelDefinition definition;definition.method=1;definition.splitAxis=axis;
        definition.equalPartCount=12;definition.maximumPartCount=12;definition.minimumPartWidthMm=4;
        definition.adaptiveSpacing=adaptive;definition.splitAtCorners=true;definition.targetMaxDeviation.value=.1;
        const auto result=app::EvaluateFabrication(definition,{source},{},.01);
        if(!result.HasValue()){Note(result.FirstMessageJa().c_str());return false;}
        if(!Explain("12枚と完成形レールを保持",result.Value().panels.size()==12 && result.Value().bandMesh.has_value()))return false;
        if(!MeasureOriginalDeviation(source,definition,result.Value()))return false;
        const auto& mesh=*result.Value().bandMesh;
        const auto folded=app::FoldedRailsOf(definition,result.Value(),0);
        if(folded.size()!=24)return false;
        double error=0;
        for(int band=0;band<mesh.BandCount();++band)for(int side=0;side<2;++side)
            for(std::size_t k=0;k<folded[2*band+side].size();++k)
                error=std::max(error,geometry::Distance(folded[2*band+side][k],mesh.world[band+side][k]));
        Note(QStringLiteral("115方向%1 自動間隔%2: %3枚 最大推定誤差%4mm 完成形レール差%5mm")
            .arg(axis).arg(adaptive).arg(result.Value().panels.size()).arg(result.Value().maximumDeviationMm,0,'g',8)
            .arg(error,0,'g',8).toUtf8().constData());
        if(!Explain("曲げ100%でレールの位置を変えない",error<1e-7))return false;
        ++checked;
    }
    kernel::ReleaseShape(built.Value().handle);
    auto& dock=window.FabricationDock();auto choice=dock.Choice();choice.equalPartCount=12;
    choice.maximumPartCount=12;choice.adaptiveSpacing=false;choice.splitAxis=3;dock.SetChoice(choice);
    auto* adaptive=window.findChild<QCheckBox*>(QStringLiteral("fabricationAdaptiveSpacing"));
    if(!adaptive)return false;adaptive->setChecked(true);
    auto* deviation=window.findChild<QDoubleSpinBox*>(QStringLiteral("fabricationTolerance"));
    if(!deviation)return false;deviation->setValue(.1);
    window.Viewport().SetSelection({});window.RunCommand("fabrication.create");
    window.Viewport().SetSelection(app::SelectAllOfKind(original,domain::EntityKind::GuideSurface));
    QApplication::processEvents();
    Note(dock.MessageText().toUtf8().constData());
    if(!Explain("候補比較から設定した帯を選べる",dock.ClickCandidate(1)))return false;
    if(auto* compare=window.findChild<QPushButton*>(QStringLiteral("fabricationCompareToggle")))compare->setChecked(false);
    if(!Explain("ツール先行でも12枚の実面下見",window.ApproxOutcomes()[1].partCount==12
        && window.Viewport().ToolPreviewFaceCount()>0))return false;
    const auto output=qEnvironmentVariable("KACHACAD_SURFACE_TEST_OUTPUT");
    if(!output.isEmpty())window.grab().save(output+"-adaptive-preview.png");
    window.HandleToolKey(Qt::Key_Return,nullptr);
    if(!Explain("近似を確定",window.FabricationModelCount()==1))return false;
    dock.SetPartNumbersText(QString());dock.TypeAssemblyPercent(100);dock.PressApplyAssembly();
    dock.SetPartNumbersText(QStringLiteral("すべて"));
    auto* format=window.findChild<QComboBox*>(QStringLiteral("fabricationGenerationOutput"));
    if(!format)return false;format->setCurrentIndex(3);window.RunCommand("fabrication.freeze_state");
    int surfaces=0,wires=0;
    for(const auto& entity:window.Session().GetDocument().Snapshot().entities)if(entity.generatedFrom) {
        surfaces+=entity.kind==domain::EntityKind::GuideSurface;wires+=entity.kind==domain::EntityKind::Wire;
    }
    if(!Explain("全12部材に4辺と面を生成",surfaces==12 && wires==48 && window.RebuildProblems().isEmpty())
        || !GeneratedMatchesRails(window))return false;
    window.Viewport().SetSelection({});QApplication::processEvents();
    if(!output.isEmpty()) {
        window.grab().save(output+"-adaptive-result.png");
        if(!window.SaveAndReopen(output+"-adaptive-result.kcd2") || !window.RebuildProblems().isEmpty()
            || !GeneratedMatchesRails(window))return false;
    }
    return checked==10;
}
}
std::vector<SelfTestCase> ApproxOutlineCases()
{
    std::vector<SelfTestCase> cases{{"HP-AP-QUICK 日常設定詳細折畳みと実面下見とGPT製作入口廃止",QuickSettings}};
    if(qEnvironmentVariableIsSet("KACHACAD_SURFACE_TEST_FILE"))
        cases.push_back({"HP-AP-OUTLINE-115 元の実曲面を5方向と等間隔自動間隔で近似生成",OwnerOutline});
    return cases;
}
}
