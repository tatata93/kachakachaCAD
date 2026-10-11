#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2FabricationDock.h"
#include "kachakacha/app/Selection.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/fabrication/BandFold.h"
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
#include <algorithm>
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
geometry::Vector3 AssembleVertex(const geometry::Vector3& p,const geometry::Vector3& q,
    const geometry::Vector3& previous,const geometry::Vector3& wp,const geometry::Vector3& wq,
    const geometry::Vector3& wv,const geometry::Vector3& wprevious,double ra,double rb)
{
    using namespace geometry;
    const auto axis=Normalized(q-p),worldAxis=Normalized(wq-wp);
    const auto reject=[](const Vector3& v,const Vector3& e){return Normalized(v-e*Dot(v,e));};
    const auto worldPrevious=reject(wprevious-wp,worldAxis),worldNext=reject(wv-wp,worldAxis);
    const double cosine=Dot(worldNext,-worldPrevious);
    const double sine=Dot(worldNext,Cross(worldAxis,worldPrevious));
    const auto side=reject(previous-p,axis);
    const double d=Distance(p,q),along=(ra*ra-rb*rb+d*d)/(2*d);
    const double height=std::sqrt(std::max(0.0,ra*ra-along*along));
    return p+axis*along+(-side*cosine+Cross(axis,side)*sine)*height;
}
bool ReassemblePatterns(const app::FabricationEvaluation& result)
{
    const auto& mesh=*result.bandMesh;double maximum=0,seam=0;
    std::vector<std::vector<geometry::Vector3>> assembled;
    for(int band=0;band<mesh.BandCount();++band) {
        const auto& outline=result.panels[band].outline;const std::size_t n=mesh.columns;
        const auto flat=[&](int side,std::size_t i){const auto& p=outline[side ? 2*n-1-i : i];
            return geometry::Vector3{p.u,p.v,0};};
        const auto length=[&](int a,std::size_t i,int b,std::size_t j){return geometry::Distance(flat(a,i),flat(b,j));};
        const auto& a=mesh.world[band];const auto& b=mesh.world[band+1];
        std::vector<geometry::Vector3> bottom(n),top(n);bottom[0]=a[0];
        top[0]=bottom[0]+geometry::Normalized(b[0]-a[0])*length(0,0,1,0);
        const auto axis=geometry::Normalized(top[0]-bottom[0]);
        const double d=length(0,0,1,0),ra=length(0,0,0,1),rb=length(1,0,0,1);
        const double along=(ra*ra-rb*rb+d*d)/(2*d);
        const auto direction=a[1]-a[0];
        bottom[1]=bottom[0]+axis*along+geometry::Normalized(direction-axis*geometry::Dot(direction,axis))
            *std::sqrt(std::max(0.0,ra*ra-along*along));
        for(std::size_t i=1;i<n;++i) {
            top[i]=AssembleVertex(top[i-1],bottom[i],bottom[i-1],b[i-1],a[i],b[i],a[i-1],
                length(1,i-1,1,i),length(0,i,1,i));
            if(i+1<n)bottom[i+1]=AssembleVertex(bottom[i],top[i],top[i-1],a[i],b[i],a[i+1],b[i-1],
                length(0,i,0,i+1),length(1,i,0,i+1));
        }
        for(std::size_t i=0;i<n;++i) {
            maximum=std::max({maximum,geometry::Distance(bottom[i],a[i]),geometry::Distance(top[i],b[i])});
            if(band)seam=std::max(seam,geometry::Distance(assembled.back()[i],bottom[i]));
        }
        assembled.push_back(top);
    }
    Note(QStringLiteral("型紙の寸法と曲げ角から再組立: 位置差%1mm 継ぎ目%2mm")
        .arg(maximum,0,'g',8).arg(seam,0,'g',8).toUtf8().constData());
    return Explain("型紙を伸縮させずに再組立",maximum<1e-5 && seam<1e-5);
}
bool CheckPanelLengths(const app::FabricationEvaluation& result)
{
    const auto& mesh=*result.bandMesh;double maximum=0,warp=0;
    std::vector<std::vector<std::vector<geometry::Vector3>>> states;
    for(double progress:{0.0,.5,.999999}) {
        const auto state=fabrication::BuildBandFoldRails(mesh,std::vector<double>(mesh.CreaseCount(),1),
            std::vector<double>(mesh.BandCount(),progress),0);
        if(!state.HasValue())return false;states.push_back(state.Value());
    }
    for(int band=0;band<mesh.BandCount();++band) {
        const auto& outline=result.panels[band].outline;
        const std::size_t n=mesh.columns;if(outline.size()!=2*n)return false;
        if(!Explain("型紙の輪郭が自己交差しない",!geometry::HasSelfIntersection(outline,1e-9)))return false;
        const auto flat=[&](int side,std::size_t i){const auto& p=outline[side ? 2*n-1-i : i];
            return geometry::Vector3{p.u,p.v,0};};
        const auto edge=[&](int a,std::size_t i,int b,std::size_t j){
            const double length=geometry::Distance(flat(a,i),flat(b,j));
            maximum=std::max(maximum,std::abs(length-geometry::Distance(mesh.world[band+a][i],mesh.world[band+b][j])));
            for(const auto& state:states)maximum=std::max(maximum,std::abs(length
                -geometry::Distance(state[2*band+a][i],state[2*band+b][j])));};
        for(std::size_t i=0;i<n;++i) {
            edge(0,i,1,i);
            if(i) {edge(0,i-1,0,i);edge(1,i-1,1,i);edge(1,i-1,0,i);
                warp=std::max(warp,std::abs(geometry::Distance(flat(0,i-1),flat(1,i))
                    -geometry::Distance(mesh.world[band][i-1],mesh.world[band+1][i])));}
        }
    }
    Note(QStringLiteral("出力型紙の三角形辺長差: 最大%1mm").arg(maximum,0,'g',8).toUtf8().constData());
    Note(QStringLiteral("滑らかな四辺形と三角形展開の対角線差: 最大%1mm")
        .arg(warp,0,'g',8).toUtf8().constData());
    return Explain("型紙が辺長を保持する",maximum<1e-7) && ReassemblePatterns(result);
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
        if(!CheckPanelLengths(result.Value()))return false;
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
    window.Viewport().SetSelection(app::SelectAllOfKind(window.Session().GetDocument().Snapshot(),
        domain::EntityKind::FabricationModel));
    window.RunCommand("fabrication.create_pattern");QApplication::processEvents();
    if(!Explain("実際の展開操作で12枚の原寸型紙を作れる",window.PatternDock().PageCount()>0
        && window.StatusText().contains(QStringLiteral("12枚"))
        && window.StatusText().contains(QStringLiteral("原寸"))))return false;
    if(!output.isEmpty())window.grab().save(output+"-pattern.png");
    return checked==10;
}
}
namespace {
bool CheckPlanarPattern(V2MainWindow& window,const app::FabricationEvaluation& result,std::size_t index)
{
    if(index>=result.panelOrigins.size())return false;
    const auto& origin=result.panelOrigins[index];
    const auto& document=window.Session().GetDocument();
    const auto* entity=document.FindEntity(origin.entityId);
    const auto* feature=entity?document.FindFeature(entity->createdBy):nullptr;
    const auto* definition=feature?std::get_if<domain::CreateGuideSurfaceDefinition>(&feature->definition):nullptr;
    if(!definition)return Explain("planar practical source is an editable guide",false);
    const geometry::GeometryTolerance tolerance;
    const auto table=app::GuideTableFromDefinition(document,window.Session().Scene(),*definition);
    if(!table.HasValue())return false;
    const auto request=modeling::ToGuideSurfaceRequest(table.Value(),tolerance);
    if(!request.HasValue())return false;
    const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request.Value(),tolerance);
    if(!analysis.HasValue())return false;
    const auto built=kernel::BuildGuideSurface(request.Value(),analysis.Value(),tolerance);
    if(!built.HasValue())return false;
    const auto boundary=kernel::FaceBoundaryOf(built.Value().handle,origin.faceIndex.value_or(0),tolerance);
    kernel::ReleaseShape(built.Value().handle);
    if(!boundary.HasValue())return false;
    const auto& panel=result.panels[index];
    if(!Explain("all planar openings retained",panel.openings.size()==boundary.Value().holeLoops.size()))return false;
    std::vector<std::vector<geometry::CurveSegment>> curves{boundary.Value().outerLoop};
    curves.insert(curves.end(),boundary.Value().holeLoops.begin(),boundary.Value().holeLoops.end());
    std::vector<std::vector<geometry::Point2>> loops{panel.outline};
    loops.insert(loops.end(),panel.openings.begin(),panel.openings.end());
    std::vector<geometry::Vector3> world,flat;
    double area=0,perimeter=0;
    for(std::size_t loop=0;loop<loops.size();++loop) {
        std::vector<geometry::Point2> clean;
        for(const auto& point:loops[loop]) {
            if(clean.empty()||std::hypot(point.u-clean.back().u,point.v-clean.back().v)>1e-9)clean.push_back(point);
            flat.push_back({point.u,point.v,0});
        }
        if(clean.size()>1&&std::hypot(clean.front().u-clean.back().u,clean.front().v-clean.back().v)<1e-9)clean.pop_back();
        if(clean.size()<3||geometry::HasSelfIntersection(clean,1e-9))return false;
        area+=(loop? -1:1)*std::abs(geometry::SignedArea(clean));
        for(std::size_t i=0;i<clean.size();++i) {
            const auto& a=clean[i];const auto& b=clean[(i+1)%clean.size()];perimeter+=std::hypot(a.u-b.u,a.v-b.v);
        }
        for(const auto& curve:curves[loop])for(const auto& sample:geometry::SampleCurve(curve,.01))world.push_back(sample.position);
    }
    if(world.size()!=flat.size())return Explain("planar source and pattern correspondence",false);
    double maximum=0;
    for(std::size_t anchor=0;anchor<world.size();anchor+=std::max<std::size_t>(1,world.size()/24))
        for(std::size_t i=0;i<world.size();++i)maximum=std::max(maximum,std::abs(
            geometry::Distance(world[anchor],world[i])-geometry::Distance(flat[anchor],flat[i])));
    Note(QStringLiteral("Planar pattern: openings=%1 area=%2 native area=%3 rigid-distance error=%4mm")
        .arg(panel.openings.size()).arg(area,0,'g',12).arg(boundary.Value().areaMm2,0,'g',12)
        .arg(maximum,0,'g',8).toUtf8().constData());
    return Explain("planar dimensions and aperture positions preserved",maximum<1e-7)
        &&Explain("planar net area agrees within curve chord tolerance",area>0&&std::abs(area-boundary.Value().areaMm2)<perimeter*.02);
}
}
bool CheckPracticalUnfold(V2MainWindow& window,const app::FabricationEvaluation& result)
{
    std::size_t offset=0;
    if(result.panels.empty())return Explain("geometry available for independent unfolding check",false);
    for(const auto& face:result.bandFaces) {
        const auto count=static_cast<std::size_t>(face.mesh.BandCount());
        if(offset+count>result.panels.size())return false;
        app::FabricationEvaluation single;
        single.bandMesh=face.mesh;
        single.panels.assign(result.panels.begin()+offset,result.panels.begin()+offset+count);
        if(!CheckPanelLengths(single))return false;
        double flatArea=0,worldArea=0;
        for(std::size_t band=0;band<count;++band) {
            const auto& outline=single.panels[band].outline;
            const std::size_t n=face.mesh.columns;
            const auto flat=[&](int side,std::size_t i){const auto& p=outline[side?2*n-1-i:i];return geometry::Vector3{p.u,p.v,0};};
            const auto area=[](auto a,auto b,auto c){return geometry::Cross(b-a,c-a).Length()*.5;};
            for(std::size_t i=1;i<n;++i) {
                flatArea+=area(flat(0,i-1),flat(1,i-1),flat(0,i))+area(flat(1,i-1),flat(1,i),flat(0,i));
                const auto& a=face.mesh.world[band];const auto& b=face.mesh.world[band+1];
                worldArea+=area(a[i-1],b[i-1],a[i])+area(b[i-1],b[i],a[i]);
            }
        }
        Note(QStringLiteral("展開三角形面積: flat=%1mm2 assembled=%2mm2 difference=%3mm2")
            .arg(flatArea,0,'g',12).arg(worldArea,0,'g',12).arg(std::abs(flatArea-worldArea),0,'g',8).toUtf8().constData());
        if(!Explain("triangulated sheet area preserved",std::abs(flatArea-worldArea)<1e-7*std::max(1.0,worldArea)))return false;
        offset+=count;
    }
    for(;offset<result.panels.size();++offset)if(!CheckPlanarPattern(window,result,offset))return false;
    return Explain("every banded and planar face checked",offset==result.panels.size());
}
std::vector<SelfTestCase> ApproxOutlineCases()
{
    std::vector<SelfTestCase> cases{{"HP-AP-QUICK 日常設定詳細折畳みと実面下見とGPT製作入口廃止",QuickSettings}};
    if(qEnvironmentVariableIsSet("KACHACAD_SURFACE_TEST_FILE"))
        cases.push_back({"HP-AP-OUTLINE-115 元の実曲面を5方向と等間隔自動間隔で近似生成",OwnerOutline});
    return cases;
}
}
