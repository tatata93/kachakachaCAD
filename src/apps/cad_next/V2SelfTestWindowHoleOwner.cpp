#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2ProjectionTool.h"
#include "V2FabricationDock.h"
#include "V2FabricationMaterial.h"
#include "V2ParameterDock.h"
#include "V2Ribbon.h"
#include "V2PlanarCurves.h"
#include "kachakacha/fabrication/PlanarPanel.h"
#include "V2ThickenDock.h"
#include "V2ThickenPreviewState.h"
#include "V2LoopFacesTool.h"
#include "V2LoopFacesDock.h"
#include "V2ToolSelection.h"
#include "V2GeometryJob.h"
#include "V2EntityTree.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/kernel/OcctSurfaceRegion.h"
#include <QFile>
#include <QIODevice>
#include <QPointF>
#include "kachakacha/geometry/WireChain.h"
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QListWidget>
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/ProfileRegion.h"
#include "kachakacha/app/SurfaceNetwork.h"
#include "kachakacha/app/ProfileNetwork.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/modeling/MeshPick.h"
#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/app/FabricationEvaluate.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QPushButton>
#include <QPixmap>
#include <QMouseEvent>
#include <QEvent>
#include <QTreeWidgetItem>
#include <QString>
#include <iostream>
#include <cmath>
#include <algorithm>

using namespace kachakacha::v2;
class V2WindowHoleExercise {
public:
    static bool Run(V2MainWindow& w,bool curved);
    static bool ApproximateRegion(V2MainWindow& w,base::EntityId source,const QString& output);
    static bool ReopenMaterial(V2MainWindow& w,const QString& saved,base::EntityId model,base::EntityId part,std::size_t holeCount);
    static bool RegionFixture(V2MainWindow& w,bool notch);
    static bool PlanarGeneration(V2MainWindow& w);
    static bool Panorama(V2MainWindow& w);
    static bool PreparePanorama(V2MainWindow& w,std::vector<base::EntityId>& input,base::EntityId& perimeter,std::vector<geometry::CurveSegment>& outer,bool left=false,bool makeSources=true);
    static bool ShowOnly(V2MainWindow& w,base::EntityId keep);
    static bool Existing(V2MainWindow& w,bool curved,bool left=false);
    static bool PreviewMeshReuse(V2MainWindow& w);
    static bool RawSelection(V2MainWindow& w,bool extra=false);
    static bool MultipleHoles(V2MainWindow& w,bool notch,bool island=false);
};
bool V2WindowHoleExercise::RawSelection(V2MainWindow& w,bool extra) {
    const auto path=qEnvironmentVariable("KACHACAD_WINDOW_OWNER_INPUT");
    if(!w.OpenDocumentFile(path))return false;
    const auto id=[](const char* s){return *base::EntityId::Parse(std::string("00000000-0000-4000-8000-00000000")+s);};
    app::SelectionSet selected;
    std::vector<const char*> suffixes={"169a","16a5","03a7","03ba","03d3","032e","001f"};
    if(extra)suffixes.push_back("16cd");
    for(const auto* suffix:suffixes) {
        const auto which=id(suffix);auto chosen=app::GuideSelectionOf(w.Session().GetDocument(),w.Session().Scene(),which);
        if(!chosen)return false;
        selected.entityIds.push_back(which);
        for(std::size_t i=0;i<chosen->segments.size();++i) {
            const auto& c=chosen->segments[i];const auto a=c.StartPoint(),b=c.EndPoint();
            std::cout<<suffix<<"["<<i<<"] kind="<<int(c.Kind())<<" "<<a.x<<","<<a.y<<","<<a.z<<" -> "<<b.x<<","<<b.y<<","<<b.z<<std::endl;
        }
        app::SelectionRef ref;ref.entityId=which;
        if(std::string(suffix)=="03ba"&&!qEnvironmentVariableIsSet("KACHACAD_WINDOW_OWNER_WHOLE")) {
            ref.kind=app::SelectionElementKind::Edge;
            for(const auto& c:w.Session().Scene().curves)if(c.entityId==which)ref.segmentId=c.segmentId;
        }
        selected.ordered.push_back(ref);
    }
    w.resize(1600,1000);w.Viewport().SetViewDirection(ViewDirection::Isometric);w.Viewport().FitToDocument();
    modeling::SnapScene raw;
    for(const auto& ref:selected.ordered)for(const auto& c:w.Session().Scene().curves)
        if(c.entityId==ref.entityId&&(!ref.segmentId||*ref.segmentId==c.segmentId))raw.curves.push_back(c);
    auto tolerance=w.Session().GetDocument().Snapshot().settings.tolerance;tolerance.interactiveJoinMm=tolerance.modelLinearMm;
    auto split=app::detail::SplitProfileNetwork(raw.curves,tolerance);
    std::vector<app::RoleWire> unique;
    for(const auto& c:split) {
        bool dup=false;for(const auto& p:unique){bool same=true,reverse=true;
            for(double t:{0.,.25,.5,.75,1.}){same=same&&geometry::Distance(c.segment.Evaluate(t),p.segments.front().Evaluate(t))<1e-6;
                reverse=reverse&&geometry::Distance(c.segment.Evaluate(t),p.segments.front().Evaluate(1-t))<1e-6;}dup=dup||same||reverse;}
        if(!dup)unique.push_back({c.entityId,{c.segment}});
    }
    std::cout<<"raw="<<raw.curves.size()<<" split="<<split.size()<<" unique="<<unique.size()<<" tables="<<app::PlanSurfaceNetwork(unique,tolerance).tables.size()<<std::endl;
    w.Viewport().SetSelection(selected);w.RunCommand("surface.from_lines");QApplication::processEvents();
    const auto output=qEnvironmentVariable("KACHACAD_WINDOW_OWNER_OUTPUT")+(extra?"/raw-multiple":"/raw-single");QDir().mkpath(output);
    w.grab().save(output+"/raw-selection.png");
    std::cout<<w.LoopFacesTool().Dock()->SummaryTextJa().toStdString()<<std::endl;
    for(int i=0;i<w.LoopFacesTool().Dock()->FaceRowCount();++i)std::cout<<w.LoopFacesTool().Dock()->FaceRowTextJa(i).toStdString()<<std::endl;
    const auto originals=w.Session().Scene().curves;
    const int before=selftest::CountOfKind(w,domain::EntityKind::GuideSurface);
    if(!selftest::Explain("raw selected original wires produce and commit one region",w.LoopFacesTool().Plan()
        &&w.LoopFacesTool().Plan()->faces.size()==1&&w.HandleToolKey(Qt::Key_Return,nullptr)
        &&selftest::CountOfKind(w,domain::EntityKind::GuideSurface)==before+1))return false;
    const auto made=w.Session().GetDocument().Snapshot().entities.back().id;
    const auto material=[&](base::EntityId which,geometry::Vector3 p,geometry::Vector3 direction=geometry::Vector3{0,-1,0}){
        for(const auto& shape:w.Viewport().ShapeViews())if(shape.entityId==which)for(const auto& t:shape.mesh.triangles)
            if(modeling::RayHitsTriangle(p,direction,t))return true;return false;
    };
    if(!selftest::Explain("the panoramic opening is empty and the lower body remains",!material(made,{10,0,21.5})&&material(made,{10,0,16})))return false;
    if(extra&&!selftest::Explain("the separate side window is also empty",!material(made,{30,-17,21.5},{-1,0,0})&&material(made,{30,-17,16},{-1,0,0})))return false;
    app::SelectionSet face;face.entityIds={made};w.Viewport().SetSelection(face);w.Viewport().IsolateSelection();w.RunCommand("view.fit_selection");
    auto display=w.Viewport().DisplaySettingsNow();display.gridVisible=false;w.Viewport().SetDisplaySettings(display);
    QApplication::processEvents();w.grab().save(output+"/raw-notch-face.png");
    for(const auto& original:originals){
        const auto* source=w.Session().GetDocument().FindEntity(original.entityId);
        if(!source||source->kind!=domain::EntityKind::Wire)continue;
        const auto& now=w.Session().Scene().curves;
        const auto found=std::find_if(now.begin(),now.end(),[&](const auto& c){return c.entityId==original.entityId&&c.segmentId==original.segmentId;});
        if(found==now.end()||found->segment.Kind()!=original.segment.Kind()){std::cout<<"original missing "<<original.entityId.ToString()<<std::endl;return false;}
        for(const double t:{0.,.25,.5,.75,1.})if(geometry::Distance(found->segment.Evaluate(t),original.segment.Evaluate(t))>1e-12){std::cout<<"source changed "<<original.entityId.ToString()<<std::endl;return false;}
    }
    if(qEnvironmentVariableIsSet("KACHACAD_WINDOW_OWNER_APPROX")){
        if(!ApproximateRegion(w,made,output+"/approximation"))return false;
        const auto part=w.Session().GetDocument().Snapshot().entities.back().id;
        return selftest::Explain("saved owner solid keeps the panoramic notch and side window empty with surrounding body material",
            !material(part,{10,0,21.5})&&material(part,{10,0,16})
            &&(!extra||(!material(part,{30,-17,21.5},{-1,0,0})&&material(part,{30,-17,16},{-1,0,0}))));
    }
    w.RunCommand("part.thicken");auto* amount=w.findChild<QDoubleSpinBox*>("thickenAmount");if(!amount)return false;amount->setValue(.5);
    if(!w.ThickenDock().ClickConfirm()){std::cout<<"raw thicken failed: "<<w.StatusText().toStdString()<<std::endl;return false;}
    const auto part=w.Session().GetDocument().Snapshot().entities.back().id;
    const auto check=kernel::CheckSolidForExport(w.partShapes_.at(part.ToString()),.001);
    if(!selftest::Explain("raw notch makes a closed positive-volume solid without filling the window",check.HasValue()&&check.Value().Ok()
        &&check.Value().volumeMm3>0&&!material(part,{10,0,21.5})&&material(part,{10,0,16})))return false;
    if(extra&&(!material(part,{30,-17,16},{-1,0,0})||material(part,{30,-17,21.5},{-1,0,0})))return false;
    face.entityIds={part};w.Viewport().SetSelection(face);w.Viewport().IsolateSelection();w.RunCommand("view.fit_selection");
    QApplication::processEvents();w.grab().save(output+"/raw-notch-solid.png");
    const int count=selftest::CountOfKind(w,domain::EntityKind::Part);w.RunCommand("edit.undo");
    if(selftest::CountOfKind(w,domain::EntityKind::Part)!=count-1)return false;
    w.RunCommand("edit.redo");if(selftest::CountOfKind(w,domain::EntityKind::Part)!=count)return false;
    if(!ShowOnly(w,part))return false;
    const auto saved=output+"/115-original-wire-notch-solid.kcd2";
    w.SetPathChooser([&](bool){return saved;});w.RunCommand("file.save_as");w.SetPathChooser({});
    return selftest::Explain("raw notch survives save and reopen",w.OpenDocumentFile(saved)&&w.RebuildProblems().isEmpty()
        &&!material(part,{10,0,21.5})&&material(part,{10,0,16}));
}


bool V2WindowHoleExercise::ApproximateRegion(V2MainWindow& w,base::EntityId source,const QString& output) {
    QDir().mkpath(output);
    const auto capture=[&](const char* name){QApplication::processEvents();return w.grab().save(output+"/"+name);};
    w.RunCommand("selection.activate");w.Viewport().ClearIsolation();w.Viewport().SetSelection({});
    if(!ShowOnly(w,source))return false;
    auto* automatic=w.findChild<QComboBox*>("fabricationAutomaticGeneration");if(!automatic)return false;automatic->setCurrentIndex(0);
    auto& dock=w.FabricationDock();dock.SetGenerationDestination(0);
    auto choice=dock.Choice();choice.equalPartCount=choice.maximumPartCount=1;choice.splitAxis=2;
    choice.splitAtCorners=false;choice.adaptiveSpacing=false;choice.rangeUMin=choice.rangeVMin=0;choice.rangeUMax=choice.rangeVMax=1;dock.SetChoice(choice);
    if(!w.ParameterDock().Apply(app::ParameterId::MaxDeviationMm,QStringLiteral("0.1"))
        ||!w.ParameterDock().Apply(app::ParameterId::ExtrudeDistance,QStringLiteral("0.5")))return false;
    w.SetMode(app::UiMode::Fabrication);
    if(!w.Ribbon().ClickCategory(QStringLiteral("近似"))||!w.Ribbon().ClickTool(QStringLiteral("近似")))return false;
    auto* one=w.findChild<QPushButton*>("fabricationOnePiece");if(!one||!one->isVisible()||!one->isEnabled())return false;one->click();
    app::SelectionSet selected;selected.entityIds={source};w.Viewport().SetSelection(selected);
    if(!dock.ClickCandidate(1))return false;
    for(int i=0;i<3;++i)std::cout<<"material candidate "<<i<<": "<<dock.CandidateTextJa(i).toStdString()<<std::endl;
    if(!selftest::Explain("curved source holes are accepted through ribbon and visible one-sheet button",w.ApproxEvaluations().size()>1
        &&w.ApproxEvaluations()[1]&&w.ApproxEvaluations()[1]->panels.size()==1&&w.Viewport().ToolPreviewFaceCount()>0))return false;
    const auto before=w.Session().GetDocument().Revision();
    (void)w.HandleToolKey(Qt::Key_Escape,nullptr);
    if(w.Session().GetDocument().Revision()!=before)return false;
    w.Viewport().SetSelection({});w.Viewport().SetSelection(selected);if(!dock.ClickCandidate(1))return false;
    const auto initial=*w.ApproxEvaluations()[1];
    if(initial.materialPanels.empty()||initial.panels[0].openings.empty())return false;
    const auto holeCount=initial.panels[0].openings.size();
    std::cout<<"material maxDeviation="<<initial.maximumDeviationMm<<" rms="<<initial.rmsDeviationMm
        <<" panels="<<initial.panels.size()<<" openings="<<holeCount<<std::endl;
    if(!capture("01-one-sheet-preview.png")||!w.HandleToolKey(Qt::Key_Return,nullptr)||w.FabricationModelCount()!=1)return false;
    const auto model=w.CurrentFabricationModelId();
    w.RunCommand("edit.undo");if(w.FabricationModelCount()!=0)return false;
    w.RunCommand("edit.redo");if(w.FabricationModelCount()!=1)return false;
    if(!ShowOnly(w,model))return false;
    selected.entityIds={model};w.Viewport().SetSelection(selected);w.RunCommand("fabrication.set_assembly");
    const auto evaluated=w.fabricationModels_.at(model.ToString());
    std::vector<double> lengths;
    for(int percent:{0,35,100}){
        dock.SetPartNumbersText(QStringLiteral("すべて"));dock.TypeAssemblyPercent(percent);dock.PressApplyAssembly();
        const auto* definition=w.CurrentFabricationDefinition();if(!definition)return false;
        if(!selftest::Explain("visible assembly control changes the saved model percent",std::abs(definition->masterPercent-percent)<1e-9))return false;
        const auto rails=app::FoldedRailsOf(*definition,evaluated,0);
        const auto loops=V2MaterialLoops(evaluated.materialPanels.front(),rails);
        double length=0;for(const auto& loop:loops)for(std::size_t i=0;i<loop.size();++i)length+=geometry::Distance(loop[i],loop[(i+1)%loop.size()]);
        lengths.push_back(length);
        if(percent==100){
            QFile data(output+"/mapped-loops.csv");if(data.open(QIODevice::WriteOnly)){
                for(std::size_t l=0;l<loops.size();++l)for(auto p:loops[l])data.write((std::to_string(l)+","+std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z)+"\n").c_str());
            }
            for(const auto& loop:loops){base::DeterministicIdGenerator ids;std::vector<geometry::ChainInput> inputs;
                for(const auto& c:V2MaterialCurves(loop,true))inputs.push_back({{},ids.NextTyped<base::IdKind::Segment>(),c});
                auto exact=w.Session().GetDocument().Snapshot().settings.tolerance;exact.interactiveJoinMm=exact.modelLinearMm;
                const auto chain=geometry::AnalyzeChain(inputs,exact);
                std::cout<<"boundary chain "<<loop.size()<<" vertices / "<<inputs.size()<<" curves: "<<(chain.HasValue()?"passed":chain.FirstMessageJa())<<std::endl;
            }
        }
        w.Viewport().FitToDocument();
        const auto visibleRails=app::FoldedRailsOf(*definition,evaluated,8.0);
        for(const auto& rail:visibleRails)for(const auto& p:rail){const auto screen=w.Viewport().Mapping().Project(p);
            if(!selftest::Explain("fit includes the complete current manufacturing pose",screen&&screen->x>=0&&screen->y>=0&&screen->x<=w.Viewport().width()&&screen->y<=w.Viewport().height()))return false;}
        if(!capture(("02-bend-"+std::to_string(percent)+".png").c_str()))return false;
        if(percent==0){
            std::vector<geometry::Vector3> flat;for(const auto& rail:rails)flat.insert(flat.end(),rail.begin(),rail.end());
            const auto plane=geometry::FitPlane(flat);
            if(!selftest::Explain("zero percent is an actually flat sheet",plane.valid&&plane.maximumDeviationMm<1e-6))return false;
            w.RunCommand("fabrication.create_pattern");
            if(!selftest::Explain("one pattern sheet retains every source hole",w.patternPages_.size()>0&&w.fabricationPanels_.size()==1
                &&w.fabricationPanels_[0].openings.size()==holeCount))return false;
        }
    }
    for(double length:lengths)if(!selftest::Explain("cut path length is preserved at 0,35,100 percent",std::abs(length-lengths.front())<1e-6*std::max(1.,length)))return false;
    auto* outputType=w.findChild<QComboBox*>("fabricationGenerationOutput");if(!outputType)return false;
    const auto generate=[&](int kind){
        outputType->setCurrentIndex(kind);w.ActivateCommand("fabrication.freeze_state");
        const bool clicked=w.FabricationDock().ClickGenerateConfirm();
        std::cout<<"material generation: "<<w.StatusText().toStdString()<<std::endl;
        return clicked;
    };
    // The ordinary tool opens first; its visible confirmation performs the generation.
    const int guides=selftest::CountOfKind(w,domain::EntityKind::GuideSurface);
    if(!generate(3)||selftest::CountOfKind(w,domain::EntityKind::GuideSurface)!=guides+1)return false;
    if(!capture("03-generated-hole-surface.png"))return false;
    w.RunCommand("edit.undo");if(selftest::CountOfKind(w,domain::EntityKind::GuideSurface)!=guides)return false;
    w.RunCommand("edit.redo");if(selftest::CountOfKind(w,domain::EntityKind::GuideSurface)!=guides+1)return false;
    const auto surfaceCopy=output+"/generated-surface.kcd2";
    w.SetPathChooser([&](bool){return surfaceCopy;});w.RunCommand("file.save_as");w.SetPathChooser({});
    const int parts=selftest::CountOfKind(w,domain::EntityKind::Part);
    if(!generate(2)||selftest::CountOfKind(w,domain::EntityKind::Part)!=parts+1)return false;
    const auto part=w.Session().GetDocument().Snapshot().entities.back().id;
    const auto native=w.partShapes_.find(part.ToString());if(native==w.partShapes_.end())return false;
    const auto check=kernel::CheckSolidForExport(native->second,.001);
    if(!selftest::Explain("generated perforated sheet thickens to a valid positive-volume solid",check.HasValue()&&check.Value().Ok()&&check.Value().volumeMm3>0))return false;
    selected.entityIds={part};w.Viewport().SetSelection(selected);w.Viewport().IsolateSelection();w.RunCommand("view.fit_selection");w.Viewport().SetSelection({});
    if(!capture("04-generated-hole-solid.png"))return false;
    const auto saved=output+"/one-sheet-windows.kcd2";w.SetPathChooser([&](bool){return saved;});w.RunCommand("file.save_as");w.SetPathChooser({});
    return ReopenMaterial(w,saved,model,part,holeCount);
}
bool V2WindowHoleExercise::ReopenMaterial(V2MainWindow& w,const QString& saved,base::EntityId model,base::EntityId part,std::size_t holeCount) {
    if(!w.OpenDocumentFile(saved)||!w.RebuildProblems().isEmpty())return false;
    const auto after=w.partShapes_.find(part.ToString());if(after==w.partShapes_.end())return false;
    const auto restored=kernel::CheckSolidForExport(after->second,.001);
    return selftest::Explain("saved one-sheet approximation and solid rebuild without losing holes",restored.HasValue()&&restored.Value().Ok()
        &&w.fabricationModels_.at(model.ToString()).panels.size()==1
        &&w.fabricationModels_.at(model.ToString()).panels[0].openings.size()==holeCount);
}
bool V2WindowHoleExercise::RegionFixture(V2MainWindow& w,bool notch) {
    w.RunCommand("selection.activate");w.RunCommand("file.new");
    using geometry::CurveSegment;using geometry::Vector3;using P=geometry::Point2;
    constexpr double pi=3.14159265358979323846;
    const auto point=[&](P p){return Vector3{p.u*20,30*std::cos(p.v*pi/2),30*std::sin(p.v*pi/2)};};
    const auto edge=[&](P a,P b){return std::abs(a.u-b.u)<1e-12
        ?CurveSegment::MakeCircularArc({a.u*20,0,0},{1,0,0},{0,1,0},30,a.v*pi/2,(b.v-a.v)*pi/2).Value()
        :CurveSegment::MakeLine(point(a),point(b)).Value();};
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::RuledSections;request.keepSectionOrder=true;
    for(int row=0;row<2;++row){modeling::GuideChain chain;chain.role=modeling::ChainRole::Section;chain.index=row+1;
        chain.segments={edge({double(row),0},{double(row),1})};request.chains.push_back(chain);}
    const auto tolerance=w.Session().GetDocument().Snapshot().settings.tolerance;
    const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,tolerance);if(!analysis.HasValue())return false;
    const auto support=kernel::BuildGuideSurface(request,analysis.Value(),tolerance);if(!support.HasValue())return false;
    std::vector<std::vector<P>> uv;
    if(notch)uv={{{0,0},{1,0},{1,1},{0,1},{0,.8},{.65,.8},{.65,.2},{0,.2}},
        {{.76,.28},{.94,.28},{.94,.44},{.76,.44}},{{.76,.55},{.94,.55},{.94,.72},{.76,.72}}};
    else {uv={{{0,0},{1,0},{1,1},{0,1}}};for(int i=0;i<6;++i){double u=.1+(i%3)*.27,v=.1+(i/3)*.48;uv.push_back({{u,v},{u+.15,v},{u+.15,v+.25},{u,v+.25}});}}
    std::vector<std::vector<CurveSegment>> loops;
    for(const auto& path:uv){std::vector<CurveSegment> curves;for(std::size_t i=0;i<path.size();++i)curves.push_back(edge(path[i],path[(i+1)%path.size()]));loops.push_back(curves);}
    const auto trimmed=kernel::TrimSurfaceRegion({support.Value().handle},loops,tolerance,.001);kernel::ReleaseShape(support.Value().handle);
    if(!trimmed.HasValue())return selftest::Explain(trimmed.FirstMessageJa().c_str(),false);
    const auto bytes=kernel::CaptureOutputShape(trimmed.Value());kernel::ReleaseShape(trimmed.Value());if(!bytes.HasValue())return false;
    domain::FreezeDerivedDefinition def;def.frozenBrep=bytes.Value();domain::Feature feature;
    feature.id=w.ids_->NextTyped<base::IdKind::Feature>();feature.type=domain::FeatureType::FreezeDerived;feature.definition=def;
    domain::Entity entity;entity.id=w.ids_->NextTyped<base::IdKind::Entity>();entity.createdBy=feature.id;entity.kind=domain::EntityKind::GuideSurface;
    entity.displayName=notch?"切り欠きと2窓の曲面":"6窓の曲面";feature.displayName=entity.displayName;feature.outputs.push_back({"source",entity.id,entity.kind});
    if(!w.Session().GetDocument().Run(document::AddFeatureCommand(feature,{entity},"穴付き曲面の回帰入力")).committed)return false;
    w.AdoptCurrentDocument();w.RebuildKernelShapes();w.resize(1600,1000);w.Viewport().SetViewDirection(ViewDirection::Isometric);w.Viewport().FitToDocument();
    auto output=qEnvironmentVariable("KACHACAD_MATERIAL_UI_OUTPUT");if(output.isEmpty())output=QDir::currentPath()+"/material-region-fixture";
    if(!ApproximateRegion(w,entity.id,output+(notch?"/notch":"/six-holes")))return false;
    const auto part=w.Session().GetDocument().Snapshot().entities.back().id;
    const auto hit=[&](P at){const auto on=point(at);for(const auto& view:w.Viewport().ShapeViews())if(view.entityId==part)for(const auto& tri:view.mesh.triangles)
        if(modeling::RayHitsTriangle({on.x,100,on.z},{0,-1,0},tri))return true;return false;};
    return selftest::Explain("native generated solid has empty window rays and surrounding material",!hit(notch?P{.85,.36}:P{.175,.225})
        &&hit(notch?P{.85,.85}:P{.05,.5})&&(!notch||!hit({.3,.5})));
}
bool V2WindowHoleExercise::MultipleHoles(V2MainWindow& w,bool notch,bool island){
    w.RunCommand("selection.activate");w.RunCommand("file.new");
    using geometry::CurveSegment;using geometry::Vector3;
    const auto line=[](Vector3 a,Vector3 b){return CurveSegment::MakeLine(a,b).Value();};
    const auto rectangle=[&](double x,double y,double a,double b){return std::vector<CurveSegment>{
        line({x,y,0},{x+a,y,0}),line({x+a,y,0},{x+a,y+b,0}),line({x+a,y+b,0},{x,y+b,0}),line({x,y+b,0},{x,y,0})};};
    app::SelectionSet selected;
    const auto add=[&](const auto& curves){const auto made=w.Session().AddWire(curves,false,"窓と開口の試験");
        if(!made.committed)return false;selected.entityIds.push_back(made.createdEntityIds.front());return true;};
    if(!add(rectangle(-20,-20,40,40)))return false;
    std::vector<Vector3> holes;
    for(int i=0;i<(notch?2:6);++i){const double x=-9.+(i%3)*8.,y=-8.+(i/3)*13.;
        if(!add(rectangle(x,y,4,5)))return false;holes.push_back({x+2,y+2.5,10});}
    if(notch&&!add(std::vector<CurveSegment>{CurveSegment::MakeCircle({-20,6,0},{0,0,1},{1,0,0},3).Value()}))return false;
    if(island&&!add(rectangle(-8.5,-7.5,1,1)))return false;
    w.AdoptCurrentDocument();w.Viewport().SetSelection(selected);w.RunCommand("surface.from_lines");
    const std::size_t regions=island?2:1;
    if(!w.LoopFacesTool().Plan()||w.LoopFacesTool().Plan()->faces.size()!=regions||!w.HandleToolKey(Qt::Key_Return,nullptr)
        ||selftest::CountOfKind(w,domain::EntityKind::GuideSurface)!=regions){std::cout<<w.LoopFacesTool().Dock()->SummaryTextJa().toStdString()<<std::endl;return false;}
    const auto hit=[&](domain::EntityKind kind,Vector3 p){for(const auto& shape:w.Viewport().ShapeViews())
        if(const auto* e=w.Session().GetDocument().FindEntity(shape.entityId);e&&e->kind==kind)
        for(const auto& t:shape.mesh.triangles)if(modeling::RayHitsTriangle(p,{0,0,-1},t))return true;return false;};
    const auto openings=[&](domain::EntityKind kind){
        if(!hit(kind,{15,15,10}))return false;for(const auto p:holes)if(hit(kind,p))return false;
        if(island&&!hit(kind,{-8,-7,10}))return false;
        return !notch||(!hit(kind,{-19,6,10})&&hit(kind,{-16,6,10})&&hit(kind,{-19,10,10}));
    };
    if(!selftest::Explain("each hole and notch remains empty, while islands remain material",openings(domain::EntityKind::GuideSurface)))return false;
    selected={};for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.kind==domain::EntityKind::GuideSurface)selected.entityIds.push_back(e.id);
    w.Viewport().SetSelection(selected);w.RunCommand("part.thicken");
    auto* amount=w.findChild<QDoubleSpinBox*>("thickenAmount");if(!amount)return false;amount->setValue(.5);
    if(!w.ThickenDock().ClickConfirm())return false;
    const auto part=w.Session().GetDocument().Snapshot().entities.back().id;const auto quality=kernel::CheckSolidForExport(w.partShapes_.at(part.ToString()),.001);
    return selftest::Explain("all openings survive thickness and solid validity checks",quality.HasValue()&&quality.Value().Ok()
        &&quality.Value().volumeMm3>0&&openings(domain::EntityKind::Part)
        &&selftest::CountOfKind(w,domain::EntityKind::Part)==regions&&selftest::CountOfKind(w,domain::EntityKind::Wire)==static_cast<int>(notch?(island?5:4):7));
}
bool V2WindowHoleExercise::Existing(V2MainWindow& w,bool curved,bool left) {
    const auto path=qEnvironmentVariable("KACHACAD_WINDOW_OWNER_INPUT");
    const auto original=io::ReadWholeFile(path.toStdString());if(!original.HasValue()||!w.OpenDocumentFile(path))return false;
    const auto id=[](const char* suffix){return *base::EntityId::Parse(std::string("00000000-0000-4000-8000-00000000")+suffix);};
    std::vector<base::EntityId> selected;
    if(curved){
        std::vector<base::EntityId> unused;base::EntityId perimeter;std::vector<geometry::CurveSegment> outer;
        if(!PreparePanorama(w,unused,perimeter,outer,left,false))return false;
        selected={perimeter,id(left?"16ae":"169a"),id(left?"16b7":"16a5")};
    }else selected={id("172a"),id("173e")};
    w.AdoptCurrentDocument();w.resize(1600,1000);w.Viewport().SetViewDirection(ViewDirection::Isometric);
    if(curved){
        std::vector<app::RoleWire> wires;modeling::SnapScene scene;
        for(const auto which:selected){
            auto chosen=app::GuideSelectionOf(w.Session().GetDocument(),w.Session().Scene(),which);if(!chosen)return false;
            wires.push_back({which,chosen->segments});
            for(const auto& curve:chosen->segments){scene.curves.push_back({which,{},curve});
                const auto a=curve.StartPoint(),b=curve.EndPoint();
                std::cout<<which.ToString().substr(32)<<" edge "<<int(curve.Kind())<<" "<<a.x<<","<<a.y<<","<<a.z<<" -> "<<b.x<<","<<b.y<<","<<b.z<<std::endl;
            }
        }
        const auto tolerance=w.Session().GetDocument().Snapshot().settings.tolerance;
        for(int i:{0,6}){
            const auto& a=wires[1].segments[i];const auto& b=wires[2].segments[6-i];double forward=0,backward=0;
            for(int j=0;j<=32;++j){forward=std::max(forward,a.ClosestPoint(b.Evaluate(j/32.)).distance);backward=std::max(backward,b.ClosestPoint(a.Evaluate(j/32.)).distance);}
            std::cout<<"overlap "<<i<<" contained="<<forward<<" reverse="<<backward<<std::endl;
        }
        std::cout<<"existing spatial boundaries="<<app::DetectSpatialBoundaries(scene,tolerance).size()<<" network tables="<<app::PlanSurfaceNetwork(wires,tolerance).tables.size()<<std::endl;
    }
    w.Viewport().FitToDocument();w.Viewport().SetSelection({});w.RunCommand("surface.from_lines");QApplication::processEvents();
    for(std::size_t i=0;i<selected.size();++i){
        QTreeWidgetItem* item=nullptr;for(const auto& row:w.entityItems_)if(row.second==selected[i])item=row.first;
        if(!item)return false;w.entityTree_->scrollToItem(item);QApplication::processEvents();
        const auto p=QPointF(w.entityTree_->visualItemRect(item).center());
        const auto modifiers=i?Qt::ControlModifier:Qt::NoModifier;
        for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
            QMouseEvent event(type,p,QPointF(w.entityTree_->viewport()->mapToGlobal(p.toPoint())),Qt::LeftButton,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,modifiers);
            QApplication::sendEvent(w.entityTree_->viewport(),&event);
        }
    }
    const auto output=qEnvironmentVariable("KACHACAD_WINDOW_OWNER_OUTPUT")+(curved?(left?"/existing-panorama-left":"/existing-panorama"):"/existing-planar");QDir().mkpath(output);
    const auto capture=[&](const char* file){QApplication::processEvents();w.grab().save(output+"/"+file);};
    capture("01-selected-preview.png");
    std::cout<<w.LoopFacesTool().Dock()->SummaryTextJa().toStdString()<<std::endl;
    if(!selftest::Explain("original projected wires reach one face row through Ctrl tree clicks",!V2ToolSelection::Active(w.Viewport())
        &&w.LoopFacesTool().Plan()&&w.LoopFacesTool().Plan()->faces.size()==1&&w.LoopFacesTool().Dock()->FaceRowCount()==1))return false;
    const int before=selftest::CountOfKind(w,domain::EntityKind::GuideSurface);
    if(!w.HandleToolKey(Qt::Key_Return,nullptr)||selftest::CountOfKind(w,domain::EntityKind::GuideSurface)!=before+1){std::cout<<w.StatusText().toStdString()<<std::endl;return false;}
    const auto made=w.Session().GetDocument().Snapshot().entities.back().id;
    std::cout<<"OWNER stage: face committed"<<std::endl;
    const auto hasMaterial=[&](base::EntityId which,geometry::Vector3 origin){
        for(const auto& shape:w.Viewport().ShapeViews())if(shape.entityId==which)for(const auto& t:shape.mesh.triangles)
            if(modeling::RayHitsTriangle(origin,{0,-1,0},t))return true;return false;
    };
    const double probeX=curved?(left?-10.:10.):0.;
    const geometry::Vector3 opening{probeX,0,21.5},beside{probeX,0,curved?16.:8.};
    if(!selftest::Explain("original window is a hole with material around it",!hasMaterial(made,opening)&&hasMaterial(made,beside)))return false;
    app::SelectionSet face;face.entityIds={made};w.Viewport().SetSelection(face);w.Viewport().IsolateSelection();w.RunCommand("view.fit_selection");capture("02-hole-face.png");
    if(!ShowOnly(w,made))return false;
    const auto savedFace=output+"/hole-face.kcd2";w.SetPathChooser([&](bool){return savedFace;});w.RunCommand("file.save_as");
    std::cout<<"OWNER stage: reopening saved face"<<std::endl;
    if(!selftest::Explain("saved face retains the empty window and surrounding material",w.OpenDocumentFile(savedFace)
        &&w.RebuildProblems().isEmpty()&&!hasMaterial(made,opening)&&hasMaterial(made,beside)))return false;
    w.Viewport().SetSelection(face);
    std::cout<<"OWNER stage: thicken entry"<<std::endl;
    w.RunCommand("part.thicken");auto* amount=w.findChild<QDoubleSpinBox*>("thickenAmount");if(!amount)return false;amount->setValue(.5);
    std::cout<<"OWNER stage: thicken confirm"<<std::endl;
    if(w.thicken_->built.size()!=1)return false;
    const auto previewHandle=w.thicken_->built.front().value;
    const auto mesh=w.thicken_->builtMeshes.find(previewHandle);
    if(mesh==w.thicken_->builtMeshes.end()||mesh->second.triangles.empty())return false;
    const auto* previewTriangles=mesh->second.triangles.data();
    if(!w.ThickenDock().ClickConfirm())return false;
    if(!selftest::Explain("thickness confirmation adopts the preview mesh without rebuilding",w.shapeMeshes_.contains(previewHandle)
        &&w.shapeMeshes_.at(previewHandle).triangles.data()==previewTriangles))return false;
    std::cout<<"OWNER stage: solid quality check"<<std::endl;
    const auto part=w.Session().GetDocument().Snapshot().entities.back().id;
    const auto checked=kernel::CheckSolidForExport(w.partShapes_.at(part.ToString()),.001);
    if(!selftest::Explain("original projected window thickens to a closed hollowed solid",checked.HasValue()&&checked.Value().Ok()
        &&checked.Value().volumeMm3>0&&!hasMaterial(part,opening)&&hasMaterial(part,beside)))return false;
    face.entityIds={part};w.Viewport().SetSelection(face);w.Viewport().IsolateSelection();w.RunCommand("view.fit_selection");capture("03-hole-solid.png");
    std::cout<<"OWNER stage: solid undo redo"<<std::endl;
    const int count=selftest::CountOfKind(w,domain::EntityKind::Part);w.RunCommand("edit.undo");if(selftest::CountOfKind(w,domain::EntityKind::Part)!=count-1)return false;
    w.RunCommand("edit.redo");if(selftest::CountOfKind(w,domain::EntityKind::Part)!=count)return false;
    if(!ShowOnly(w,part))return false;
    const auto saved=output+"/hole-solid.kcd2";w.SetPathChooser([&](bool){return saved;});w.RunCommand("file.save_as");w.SetPathChooser({});
    std::cout<<"OWNER stage: reopening solid"<<std::endl;
    if(!w.OpenDocumentFile(saved)||!w.RebuildProblems().isEmpty())return false;
    const auto native=w.partShapes_.find(part.ToString());if(native==w.partShapes_.end())return false;
    const auto restored=kernel::CheckSolidForExport(native->second,.001);
    return selftest::Explain("original unchanged; reopened solid retains hole, wall, closedness and volume",restored.HasValue()&&restored.Value().Ok()
        &&std::abs(restored.Value().volumeMm3-checked.Value().volumeMm3)<=std::max(1e-6,checked.Value().volumeMm3*1e-7)
        &&!hasMaterial(part,opening)&&hasMaterial(part,beside)&&io::ReadWholeFile(path.toStdString()).Value()==original.Value());
}
bool V2WindowHoleExercise::Panorama(V2MainWindow& w) {
    const auto path=qEnvironmentVariable("KACHACAD_WINDOW_OWNER_INPUT");
    const auto original=io::ReadWholeFile(path.toStdString());if(!original.HasValue()||!w.OpenDocumentFile(path))return false;
    const auto id=[](const char* suffix){return *base::EntityId::Parse(std::string("00000000-0000-4000-8000-000000000")+suffix);};
    const auto support=id("433");
    std::vector<base::EntityId> input;base::EntityId perimeter;
    std::vector<geometry::CurveSegment> outer;
    if(!PreparePanorama(w,input,perimeter,outer))return false;
    w.AdoptCurrentDocument();w.resize(1500,1000);w.Viewport().SetViewDirection(ViewDirection::Isometric);
    const auto output=qEnvironmentVariable("KACHACAD_WINDOW_OWNER_OUTPUT");QDir().mkpath(output);
    const auto capture=[&](const char* file){QApplication::processEvents();w.grab().save(output+"/"+file);};
    w.RunCommand("wire.project_surface");auto* projection=V2ProjectionTool::Open(w);
    for(const auto source:input){app::SelectionRef ref;ref.entityId=source;if(!projection->AddWire(ref))return false;}
    auto* rows=projection->findChild<QListWidget*>("projectionWireRows");if(!rows)return false;
    const auto button=[&](const QString& name){for(auto* b:projection->findChildren<QPushButton*>())if(b->text()==name){b->click();return true;}return false;};
    rows->setCurrentRow(0);if(!button(QStringLiteral("Y方向"))||!button(QStringLiteral("方向を反転")))return false;
    rows->setCurrentRow(1);if(!button(QStringLiteral("X方向"))||!button(QStringLiteral("方向を反転")))return false;
    app::SelectionRef target;target.entityId=support;if(!projection->AddTarget(target))return false;
    projection->findChild<QComboBox*>("projectionWireForm")->setCurrentIndex(1);
    if(!projection->Preview()||!projection->Commit()){std::cout<<w.StatusText().toStdString()<<std::endl;return false;}
    const auto projected=w.Session().GetDocument().Snapshot().entities.back().id;
    app::SelectionSet selected;selected.entityIds={projected};
    const auto curves=app::SelectedCurves(selected,w.Session().Scene());
    std::cout<<"OWNER PANORAMA segments="<<curves.size()<<std::endl;
    if(!selftest::Explain("owner panorama is a closed spatial wire",geometry::SegmentsFormClosedLoop(curves,{})))return false;
    {
        const std::vector<modeling::KernelShapeHandle> supports={w.guideShapes_.at(support.ToString()),w.guideShapes_.at(id("44b").ToString())};
        const auto probe=kernel::TrimSurfaceRegion(supports,{outer,curves},{},.01);
        QFile log(output+"/native-region.txt");if(log.open(QIODevice::WriteOnly)) {
            log.write(probe.HasValue()?"native region passed\n":"native region failed\n");
            for(const auto& d:probe.Diagnostics())log.write((d.code+" "+d.summaryJa+" "+d.detailsJa+"\n").c_str());
        }
        if(probe.HasValue())kernel::ReleaseShape(probe.Value());
        QFile more(output+"/native-isolation.txt");if(more.open(QIODevice::WriteOnly))for(const auto& loop:{outer,curves}) {
            const auto one=kernel::TrimSurfaceRegion(supports,{loop},{},.01);
            more.write(one.HasValue()?"single loop passed\n":"single loop failed\n");
            for(const auto& d:one.Diagnostics())more.write((d.summaryJa+" "+d.detailsJa+"\n").c_str());
            if(one.HasValue())kernel::ReleaseShape(one.Value());
        }
    }
    w.RunCommand("selection.activate");w.Viewport().SetSelection(selected);w.Viewport().IsolateSelection();
    w.RunCommand("view.fit_selection");auto display=w.Viewport().DisplaySettingsNow();display.gridVisible=false;w.Viewport().SetDisplaySettings(display);
    capture("panorama-01-projected.png");
    // Normal human picking: every generated curved interval must be independently
    // selectable without selecting either adjacent straight segment.
    int curved=0;
    for(const auto& c:curves)if(c.Kind()!=geometry::CurveKind::Line) {
        ++curved;w.Viewport().SetSelection({});const auto p=w.Viewport().Mapping().Project(c.Evaluate(.37));if(!p)return false;
        w.Viewport().SelectAt(QPointF(p->x,p->y),Qt::NoModifier);
        const auto picked=app::SelectedCurves(w.Viewport().Selection(),w.Session().Scene());
        if(!selftest::Explain("one curved interval selected without adjacent straight edges",picked.size()==1&&picked.front().Kind()!=geometry::CurveKind::Line))return false;
    }
    if(curved<4)return false;capture("panorama-02-curve-only.png");
    w.Viewport().SetSelection({});w.Viewport().ClearIsolation();
    selected.entityIds={perimeter,projected};w.Viewport().SetSelection(selected);
    const int before=selftest::CountOfKind(w,domain::EntityKind::GuideSurface);
    w.RunCommand("surface.from_lines");capture("panorama-03-hole-preview.png");
    if(!w.HandleToolKey(Qt::Key_Return,nullptr)||selftest::CountOfKind(w,domain::EntityKind::GuideSurface)!=before+1){std::cout<<w.StatusText().toStdString()<<std::endl;return false;}
    const auto made=w.Session().GetDocument().Snapshot().entities.back().id;
    const auto nativeFace=kernel::CaptureOutputShape(w.guideShapes_.at(made.ToString()));
    if(nativeFace.HasValue()) {QFile file(output+"/panorama-hole-face.brep");
        if(file.open(QIODevice::WriteOnly))file.write(nativeFace.Value().data(),nativeFace.Value().size());}
    const auto hasMaterial=[&](base::EntityId which,geometry::Vector3 origin) {
        bool hit=false;for(const auto& shape:w.Viewport().ShapeViews())if(shape.entityId==which)
            for(const auto& t:shape.mesh.triangles)hit=hit||modeling::RayHitsTriangle(origin,{0,-1,0},t).has_value();return hit;
    };
    if(!selftest::Explain("owner panorama hole is empty and original surrounding surface remains",!hasMaterial(made,{10,0,21.5})&&hasMaterial(made,{10,0,16})))return false;
    selected.entityIds={made};w.Viewport().SetSelection(selected);w.Viewport().IsolateSelection();w.RunCommand("view.fit_selection");capture("panorama-04-hole-face.png");
    const auto savedFace=output+"/115-panorama-hole-face.kcd2";
    if(!ShowOnly(w,made))return false;
    w.SetPathChooser([&](bool){return savedFace;});w.RunCommand("file.save_as");w.SetPathChooser({});
    w.RunCommand("part.thicken");
    auto* amount=w.findChild<QDoubleSpinBox*>("thickenAmount");if(!amount)return false;amount->setValue(.5);
    capture("panorama-05-thickness-preview.png");
    if(!w.ThickenDock().ClickConfirm()){std::cout<<w.StatusText().toStdString()<<std::endl;return false;}
    const auto part=w.Session().GetDocument().Snapshot().entities.back().id;
    const auto native=w.partShapes_.find(part.ToString());if(native==w.partShapes_.end())return false;
    const auto solid=kernel::CheckSolidForExport(native->second,.001);
    if(!solid.HasValue()||!solid.Value().Ok()||solid.Value().volumeMm3<=0||hasMaterial(part,{10,0,21.5})||!hasMaterial(part,{10,0,16}))return false;
    selected.entityIds={part};w.Viewport().SetSelection(selected);w.Viewport().IsolateSelection();w.RunCommand("view.fit_selection");capture("panorama-06-hole-solid.png");
    const int count=selftest::CountOfKind(w,domain::EntityKind::Part);w.RunCommand("edit.undo");
    if(selftest::CountOfKind(w,domain::EntityKind::Part)!=count-1)return false;
    w.RunCommand("edit.redo");if(selftest::CountOfKind(w,domain::EntityKind::Part)!=count)return false;
    const auto saved=output+"/115-panorama-hole-solid.kcd2";
    if(!ShowOnly(w,part))return false;
    w.SetPathChooser([&](bool){return saved;});w.RunCommand("file.save_as");w.SetPathChooser({});
    if(!w.OpenDocumentFile(saved)||!w.RebuildProblems().isEmpty())return false;
    return selftest::Explain("owner original preserved; window and closed solid survive save undo redo",io::ReadWholeFile(path.toStdString()).Value()==original.Value());
}
bool V2WindowHoleExercise::ShowOnly(V2MainWindow& w,base::EntityId keep) {
    std::vector<base::EntityId> hidden;
    for(const auto& e:w.Session().GetDocument().Snapshot().entities)if(e.id!=keep)hidden.push_back(e.id);
    if(!w.Session().GetDocument().Run(document::SetVisibilityCommand(hidden,domain::Visibility::Hidden)).committed)return false;
    w.AdoptCurrentDocument();return true;
}

bool V2WindowHoleExercise::PreparePanorama(V2MainWindow& w,std::vector<base::EntityId>& input,base::EntityId& perimeter,std::vector<geometry::CurveSegment>& outer,bool left,bool makeSources) {
    const auto id=[](const char* suffix){return *base::EntityId::Parse(std::string("00000000-0000-4000-8000-000000000")+suffix);};
    const auto support=id(left?"43f":"433");const auto* e=w.Session().GetDocument().FindEntity(support);
    const auto* feature=e?w.Session().GetDocument().FindFeature(e->createdBy):nullptr;
    const auto* definition=feature?std::get_if<domain::CreateGuideSurfaceDefinition>(&feature->definition):nullptr;
    if(!definition)return selftest::Explain("owner current 115 surface fixture is available",false);
    const auto table=app::GuideTableFromDefinition(w.Session().GetDocument(),w.Session().Scene(),*definition);
    if(!table.HasValue())return false;
    for(const auto& row:table.Value().rows)if(row.role==modeling::ChainRole::OuterBoundary||row.role==modeling::ChainRole::BoundarySide)
        outer.insert(outer.end(),row.segments.begin(),row.segments.end());
    std::vector<geometry::CurveSegment> expanded;
    for(const auto& c:outer) {
        const auto a=c.StartPoint(),b=c.EndPoint();
        const double x=left?-6.:6.;
        if(c.Kind()==geometry::CurveKind::Line&&std::abs(a.x-x)<1e-7&&std::abs(b.x-x)<1e-7
            &&std::abs(a.y+27)<1e-7&&std::abs(b.y+27)<1e-7&&std::abs(a.z-b.z)>19.9) {
            const auto aa=a+geometry::Vector3{left?1.:-1.,0,0},bb=b+geometry::Vector3{left?1.:-1.,0,0};
            expanded.push_back(geometry::CurveSegment::MakeLine(a,aa).Value());
            expanded.push_back(geometry::CurveSegment::MakeLine(aa,bb).Value());
            expanded.push_back(geometry::CurveSegment::MakeLine(bb,b).Value());
        } else expanded.push_back(c);
    }
    if(expanded.size()!=outer.size()+2)return selftest::Explain("owner perimeter includes 1mm of the adjacent central face",false);
    outer=std::move(expanded);
    if(!makeSources){
        const auto boundary=w.Session().AddWire(outer,false,"穴付き面の外周");if(!boundary.committed)return false;
        perimeter=boundary.createdEntityIds.front();
        return w.Session().GetDocument().Run(document::SetVisibilityCommand({support},domain::Visibility::Visible)).committed;
    }
    std::vector<std::vector<geometry::CurveSegment>> sources;
    for(int side=0;side<2;++side) {
        const auto chosen=app::GuideSelectionOf(w.Session().GetDocument(),w.Session().Scene(),id(side?"4a2":"057"));
        if(!chosen||chosen->segments.size()!=5)return false;
        auto curves=chosen->segments;
        // Split the two original drawings at X=15 on the front planar region.
        // The complete drawings overlap; double-projecting those overlaps makes
        // branches rather than a unique closed window. Preserve the four R1s.
        const double first=side?.19041885388641175:3.0/11,last=side?.8079077378311976:8.0/11;
        curves.front()=geometry::CurveSegment::MakeLine(curves.front().Evaluate(first),curves.front().EndPoint()).Value();
        curves.back()=geometry::CurveSegment::MakeLine(curves.back().StartPoint(),curves.back().Evaluate(last)).Value();
        sources.push_back(std::move(curves));
    }
    // User-authorized replacement is exercised on this in-memory copy only.
    for(const auto suffix:{"51f","539"}) {
        const auto* old=w.Session().GetDocument().FindEntity(id(suffix));
        if(old&&!w.Session().GetDocument().Run(document::RemoveFeatureCommand(old->createdBy,document::RemovePolicy::Cascade,"投影試験の旧線を置換")).committed)return false;
    }
    for(const auto& curves:sources) {
        const auto added=w.Session().AddWire(curves,false,"窓投影・分担位置を合わせた入力");
        if(!added.committed)return false;input.push_back(added.createdEntityIds.front());
    }
    const auto boundary=w.Session().AddWire(outer,false,"穴付き面の外周");if(!boundary.committed)return false;
    perimeter=boundary.createdEntityIds.front();
    if(!w.Session().GetDocument().Run(document::SetVisibilityCommand({support},domain::Visibility::Visible)).committed)return false;
    return true;
}
bool V2WindowHoleExercise::Run(V2MainWindow& w,bool curved) {
    const auto path=qEnvironmentVariable("KACHACAD_WINDOW_OWNER_INPUT");
    const auto original=io::ReadWholeFile(path.toStdString());if(!original.HasValue()||!w.OpenDocumentFile(path))return false;
    const auto id=[](const char* suffix){return *base::EntityId::Parse(std::string("00000000-0000-4000-8000-000000")+suffix);};
    const auto support=id(curved?"0069ab":"0021ab"),source=id(curved?"000115":"0000db");
    const auto native=w.guideShapes_.find(support.ToString());if(native==w.guideShapes_.end())return false;
    const auto* entity=w.session_->GetDocument().FindEntity(support);
    const auto* feature=entity?w.session_->GetDocument().FindFeature(entity->createdBy):nullptr;
    const auto* definition=feature?std::get_if<domain::CreateGuideSurfaceDefinition>(&feature->definition):nullptr;
    if(!definition)return false;
    const auto table=app::GuideTableFromDefinition(w.session_->GetDocument(),w.session_->Scene(),*definition);
    if(!table.HasValue())return false;
    std::vector<geometry::CurveSegment> outer;
    for(const auto& row:table.Value().rows)if(row.role==modeling::ChainRole::OuterBoundary||row.role==modeling::ChainRole::BoundarySide)
        outer.insert(outer.end(),row.segments.begin(),row.segments.end());
    std::cout<<"OWNER original outer segments="<<outer.size()<<std::endl;
    if(!w.session_->GetDocument().Run(document::SetVisibilityCommand({support},domain::Visibility::Visible)).committed)return false;
    const auto boundary=w.session_->AddWire(outer,false,"試験用・車体の外周");
    if(!boundary.committed)return false;w.AdoptCurrentDocument();
    auto* tool=V2ProjectionTool::Open(w);app::SelectionRef wire,face;wire.entityId=source;face.entityId=support;
    if(!tool->AddWire(wire)||!tool->AddTarget(face))return false;
    for(auto* button:tool->findChildren<QPushButton*>())if(button->text()==QStringLiteral("Y方向"))button->click();
    for(auto* button:tool->findChildren<QPushButton*>())if(button->text()==QStringLiteral("方向を反転"))button->click();
    if(!tool->Preview()||!tool->Commit())return false;
    const auto projected=w.session_->GetDocument().Snapshot().entities.back().id;
    w.RunCommand("selection.activate");app::SelectionSet selected;selected.entityIds={boundary.createdEntityIds.front(),projected};
    w.Viewport().SetSelection(selected);const auto before=selftest::CountOfKind(w,domain::EntityKind::GuideSurface);
    w.RunCommand("surface.from_lines");
    if(!w.HandleToolKey(Qt::Key_Return,nullptr)||selftest::CountOfKind(w,domain::EntityKind::GuideSurface)!=before+1)return false;
    const auto made=w.session_->GetDocument().Snapshot().entities.back().id;
    bool hole=true,material=false;
    const geometry::Vector3 center=curved?geometry::Vector3{-13,0,13.5}:geometry::Vector3{0,0,21};
    const auto beside=center+geometry::Vector3{0,0,curved?3.0:5.0};
    for(const auto& shape:w.Viewport().ShapeViews())if(shape.entityId==made)for(const auto& triangle:shape.mesh.triangles){
        hole=hole&&!modeling::RayHitsTriangle(center,{0,-1,0},triangle).has_value();
        material=material||modeling::RayHitsTriangle(beside,{0,-1,0},triangle).has_value();
    }
    if(!selftest::Explain("owner window center is empty and surrounding body remains",hole&&material))return false;
    const auto output=qEnvironmentVariable("KACHACAD_WINDOW_OWNER_OUTPUT");QDir().mkpath(output);
    selected.entityIds={made};w.Viewport().SetSelection(selected);w.Viewport().IsolateSelection();
    w.resize(1500,950);QApplication::processEvents();w.Viewport().SetViewDirection(ViewDirection::Isometric);
    w.RunCommand("view.fit_selection");w.Viewport().SetSelection({});
    auto display=w.Viewport().DisplaySettingsNow();display.gridVisible=false;w.Viewport().SetDisplaySettings(display);
    QApplication::processEvents();const auto name=curved?QStringLiteral("curved"):QStringLiteral("planar");w.grab().save(output+"/"+name+"-window-hole.png");
    w.RunCommand("edit.undo");
    if(!selftest::Explain("owner hole generation is one undo",selftest::CountOfKind(w,domain::EntityKind::GuideSurface)==before))return false;
    w.RunCommand("edit.redo");
    std::vector<base::EntityId> hidden;for(const auto& row:w.session_->GetDocument().Snapshot().entities)
        if(row.id!=made)hidden.push_back(row.id);
    if(!w.session_->GetDocument().Run(document::SetVisibilityCommand(hidden,domain::Visibility::Hidden)).committed)return false;
    w.AdoptCurrentDocument();
    const auto saved=output+"/115-"+name+"-window-hole.kcd2";
    w.SetPathChooser([saved](bool){return saved;});w.RunCommand("file.save_as");w.SetPathChooser({});
    if(!selftest::Explain("owner result saved",w.StatusText().contains(QStringLiteral("保存しました"))))return false;
    const bool reopened=w.OpenDocumentFile(saved);
    std::cout<<"OWNER reopen="<<reopened<<" problems="<<w.RebuildProblems().toStdString()<<std::endl;
    return selftest::Explain("owner result reopens without rebuild errors",reopened&&w.RebuildProblems().isEmpty())&&io::ReadWholeFile(path.toStdString()).Value()==original.Value();
}
bool V2WindowHoleExercise::PlanarGeneration(V2MainWindow& w) {
    using geometry::CurveSegment;using geometry::Vector3;
    for(const double tilt:{.9,2.8}) {
        w.RunCommand("file.new");
        std::vector<CurveSegment> outer={CurveSegment::MakeLine({-10,-10,0},{10,-10,0}).Value(),
            CurveSegment::MakeCubicBezier({{10,-10,0},{15,-10,0},{15,10,0},{10,10,0}}).Value(),
            CurveSegment::MakeLine({10,10,0},{-10,10,0}).Value(),CurveSegment::MakeLine({-10,10,0},{-10,-10,0}).Value()};
        std::vector<CurveSegment> hole={CurveSegment::MakeCircle({},{0,0,1},{1,0,0},2).Value()};
        for(auto* curves:{&outer,&hole})for(auto& curve:*curves){curve=geometry::RotateCurve(curve,{},geometry::Normalized(Vector3{1,1,0}),tilt).Value();
            curve=geometry::RotateCurve(curve,{},{0,0,1},.73).Value();curve=geometry::TranslateCurve(curve,{7,-5,11});}
        std::vector<base::EntityId> inputs;for(const auto& curves:{outer,hole}){const auto wire=w.session_->AddWire(curves,false,"曲線を保持する試験");if(!wire.committed)return false;inputs.push_back(wire.createdEntityIds.front());}
        w.AdoptCurrentDocument();modeling::GuideTable table;table.method=modeling::GuideSurfaceMethod::PlanarBoundary;
        for(std::size_t i=0;i<inputs.size();++i){const auto selected=app::GuideSelectionOf(w.session_->GetDocument(),w.session_->Scene(),inputs[i]);if(!selected)return false;
            const auto added=modeling::AddSelectionAsNewRow(table,i?modeling::ChainRole::HoleBoundary:modeling::ChainRole::OuterBoundary,*selected);if(!added.HasValue())return false;table=added.Value();}
        const auto built=w.BuildSurfaceFromTable(table,true);if(!built)return false;
        const auto guide=w.AdoptGuideSurface(table,*built,inputs,"傾いた曲線付き面");if(guide.IsNil())return false;w.AdoptCurrentDocument();
        domain::CreateFabricationModelDefinition definition;definition.parts={guide};definition.targetMaxDeviation.value=.1;
        const auto sources=w.FabricationSourcesFor(definition.parts,false);
        const auto evaluated=app::EvaluateFabrication(definition,sources,{},.01);if(!evaluated.HasValue()||sources.size()!=1||!sources.front().flatBoundary||sources.front().flatOpenings.empty())return false;
        for(const double tolerance:{.002,.025}){
            fabrication::PlanarPanelRequest request;request.panelId="native";request.boundary=*sources.front().flatBoundary;
            request.openings=sources.front().flatOpenings;
            const auto panel=fabrication::BuildPlanarPanel(request,tolerance);if(!panel.HasValue())return false;
            const auto frame=V2PlanarCurves::SourceFrame(panel.Value(),sources.front(),tolerance);if(!frame)return false;
            fabrication::PatternPlacement placement;placement.rotationRad=.47;placement.translationMm={25,40};
            const auto paths=V2PlanarCurves::Paths(panel.Value(),sources.front(),{},*frame,tolerance);
            const auto placed=V2PlanarCurves::Place(panel.Value(),placement,sources.front(),{},*frame,tolerance);
            if(!placed.HasValue()||paths.size()!=2)return false;
            std::size_t count=0;for(const auto& path:paths)for(const auto& curve:path.curves){
                if(placed.Value()[count].segment.Kind()!=curve.Kind()||std::abs(placed.Value()[count].segment.Evaluate(.4).z)>1e-7)return false;
                if(std::abs(placed.Value()[count].segment.TotalLength(1e-8)-curve.TotalLength(1e-8))>1e-7)return false;++count;
            }
            if(count!=request.boundary.size()+request.openings.front().size())return false;
        }
        const std::vector<std::vector<CurveSegment>> original={*sources.front().flatBoundary,sources.front().flatOpenings.front()};
        w.freezeOutput_=fabrication::FreezeOutput::WiresOnly;w.Viewport().SetSelection({});w.FabricationDock().SetPartNumbersText({});
        for(const double percent:{0.0,37.0,100.0}) {
            definition.masterPercent=percent;const auto count=w.session_->GetDocument().Snapshot().features.size();int wires=0,surfaces=0,parts=0;
            if(!w.FreezePlanarPanels(definition,evaluated.Value(),wires,surfaces,parts)||wires!=2)return false;
            const auto& snapshot=w.session_->GetDocument().Snapshot();
            std::vector<Vector3> before,after;
            for(std::size_t loop=0;loop<2;++loop){const auto* wire=std::get_if<domain::CreateWireDefinition>(&snapshot.features[count+loop].definition);
                if(!wire||wire->segments.size()!=original[loop].size())return selftest::Explain("generated planar curves retain their kinds and edge count",false);
                for(std::size_t i=0;i<wire->segments.size();++i){if(wire->segments[i].Kind()!=original[loop][i].Kind())return false;
                    for(const double t:{0.0,.37,1.0}){const auto p=wire->segments[i].Evaluate(t),q=original[loop][i].Evaluate(t);before.push_back(q);after.push_back(p);
                        if(percent==100&&geometry::Distance(p,q)>1e-7)return false;if(percent==0&&std::abs(p.z)>1e-7)return false;}}
            }
            for(std::size_t i=0;i<before.size();++i)for(std::size_t j=0;j<before.size();++j)
                if(std::abs(geometry::Distance(before[i],before[j])-geometry::Distance(after[i],after[j]))>1e-7)return false;
        }
        w.AdoptCurrentDocument();if(!w.SaveAndReopen("generated-planar-curves.kcd2"))return false;
    }
    return selftest::Explain("tilted and reversed planar outlines preserve curves, holes and lengths at 0 37 100 percent",true);
}
bool V2WindowHoleExercise::PreviewMeshReuse(V2MainWindow& w){
    using geometry::CurveSegment;
    for(const bool independent:{false,true}){
        w.RunCommand("file.new");
        const std::vector<CurveSegment> curves={CurveSegment::MakeLine({-10,-5,0},{10,-5,0}).Value(),
            CurveSegment::MakeLine({10,-5,0},{10,5,0}).Value(),CurveSegment::MakeLine({10,5,0},{-10,5,0}).Value(),
            CurveSegment::MakeLine({-10,5,0},{-10,-5,0}).Value()};
        const auto wire=w.Session().AddWire(curves,false,"mesh転送の回帰");if(!wire.committed)return false;
        app::SelectionSet selected;selected.entityIds=wire.createdEntityIds;w.Viewport().SetSelection(selected);
        w.RunCommand("surface.from_lines");if(!w.HandleToolKey(Qt::Key_Return,nullptr))return false;
        const auto surface=w.Session().GetDocument().Snapshot().entities.back().id;
        selected.entityIds={surface};w.Viewport().SetSelection(selected);w.RunCommand("part.thicken");
        auto* amount=w.findChild<QDoubleSpinBox*>("thickenAmount");if(!amount)return false;amount->setValue(.5);
        auto* output=w.findChild<QComboBox*>("thickenOutput");auto* separate=w.findChild<QCheckBox*>("thickenIndependent");
        if(!output||!separate||w.thicken_->built.size()!=1)return false;output->setCurrentIndex(independent?0:2);separate->setChecked(independent);
        const auto handle=w.thicken_->built.front().value;
        const auto preview=w.thicken_->builtMeshes.find(handle);if(preview==w.thicken_->builtMeshes.end()||preview->second.triangles.empty())return false;
        const auto* triangles=preview->second.triangles.data();
        const auto groups=w.thicken_->builtWires.find(handle);if(groups==w.thicken_->builtWires.end()||groups->second.empty())return false;
        const auto wireCount=groups->second.size();
        if(!w.ThickenDock().ClickConfirm()||!w.shapeMeshes_.contains(handle)||w.shapeMeshes_.at(handle).triangles.data()!=triangles)return false;
        if(selftest::CountOfKind(w,domain::EntityKind::Wire)!=static_cast<int>(1+(independent?0:wireCount)))return false;
        const auto tolerance=w.Session().GetDocument().Snapshot().settings.tolerance;modeling::ShapeMesh restoredMesh;
        const auto cached=V2GeometryJob::Thicken(&w,w.guideShapes_.at(surface.ToString()),.5,
            w.thicknessPlacement_,std::nullopt,tolerance,false,false,&restoredMesh,true);
        if(!cached.HasValue()||restoredMesh.Empty())return false;kernel::ReleaseShape(cached.Value().handle);
        const auto different=V2GeometryJob::Thicken(&w,w.guideShapes_.at(surface.ToString()),.617,
            w.thicknessPlacement_,std::nullopt,tolerance,false,false,&restoredMesh,true);
        if(different.HasValue()){kernel::ReleaseShape(different.Value().handle);return false;}
        w.RunCommand("edit.undo");if(selftest::CountOfKind(w,domain::EntityKind::Part)!=0)return false;
        w.RunCommand("edit.redo");if(selftest::CountOfKind(w,domain::EntityKind::Part)!=1||!w.RebuildProblems().isEmpty())return false;
        separate->setChecked(false);
    }
    return selftest::Explain("worker display transfers to dependent and independent parts; redo cache rejects changed settings",true);
}
namespace kachakacha::v2::selftest {
std::vector<SelfTestCase> WindowHoleOwnerCases(){
    std::vector<SelfTestCase> cases={{"HP-FAB-REGION six curved windows one pattern and thick solid",[](auto& w){return V2WindowHoleExercise::RegionFixture(w,false);}},
        {"HP-FAB-REGION C notch two windows one pattern and thick solid",[](auto& w){return V2WindowHoleExercise::RegionFixture(w,true);}},
        {"PROJ planar generated curves preserve holes and rigid poses",V2WindowHoleExercise::PlanarGeneration},
        {"HP-TH-MESH worker preview mesh moves to confirmed part",V2WindowHoleExercise::PreviewMeshReuse},
        {"HP-LF-HOLES six holes from original wires face and solid",[](auto& w){return V2WindowHoleExercise::MultipleHoles(w,false);}},
        {"HP-LF-HOLES semicircular notch and two holes face and solid",[](auto& w){return V2WindowHoleExercise::MultipleHoles(w,true);}},
        {"HP-LF-HOLES notch and nested island retain material",[](auto& w){return V2WindowHoleExercise::MultipleHoles(w,true,true);}}};
    if(!qEnvironmentVariable("KACHACAD_WINDOW_OWNER_INPUT").isEmpty()) {
        if(qEnvironmentVariableIsSet("KACHACAD_WINDOW_OWNER_RAW"))return {
            {"PROJ OWNER raw selected original boundaries",[](auto& w){return V2WindowHoleExercise::RawSelection(w);}},
            {"PROJ OWNER raw selected original boundaries and side window",[](auto& w){return V2WindowHoleExercise::RawSelection(w,true);}}};
        if(qEnvironmentVariableIsSet("KACHACAD_WINDOW_OWNER_EXISTING"))return {
            {"PROJ OWNER existing projected planar window face and solid",[](auto& w){return V2WindowHoleExercise::Existing(w,false);}},
            {"PROJ OWNER existing projected panorama face and solid",[](auto& w){return V2WindowHoleExercise::Existing(w,true);}},
            {"PROJ OWNER existing projected left panorama face and solid",[](auto& w){return V2WindowHoleExercise::Existing(w,true,true);}}};
        if(qEnvironmentVariableIsSet("KACHACAD_WINDOW_OWNER_PANORAMA"))return {{"PROJ OWNER panorama curves hole face and solid",V2WindowHoleExercise::Panorama}};
        cases.push_back({"PROJ OWNER selected outer and planar window hole",[](auto& w){return V2WindowHoleExercise::Run(w,false);}});
        cases.push_back({"PROJ OWNER selected outer and curved window hole",[](auto& w){return V2WindowHoleExercise::Run(w,true);}});
    }
    return cases;
}
}
