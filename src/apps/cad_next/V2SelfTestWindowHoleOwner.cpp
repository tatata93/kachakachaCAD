#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2ProjectionTool.h"
#include "V2FabricationDock.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/app/GuideTableBuild.h"
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
#include <QString>
#include <iostream>
#include <cmath>

using namespace kachakacha::v2;
class V2WindowHoleExercise {
public:
    static bool Run(V2MainWindow& w,bool curved);
    static bool PlanarGeneration(V2MainWindow& w);
};
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
    std::vector<base::EntityId> hidden;for(const auto& entity:w.session_->GetDocument().Snapshot().entities)
        if(entity.id!=made)hidden.push_back(entity.id);
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
namespace kachakacha::v2::selftest {
std::vector<SelfTestCase> WindowHoleOwnerCases(){
    std::vector<SelfTestCase> cases={{"PROJ planar generated curves preserve holes and rigid poses",V2WindowHoleExercise::PlanarGeneration}};
    if(!qEnvironmentVariable("KACHACAD_WINDOW_OWNER_INPUT").isEmpty()) {
        cases.push_back({"PROJ OWNER selected outer and planar window hole",[](auto& w){return V2WindowHoleExercise::Run(w,false);}});
        cases.push_back({"PROJ OWNER selected outer and curved window hole",[](auto& w){return V2WindowHoleExercise::Run(w,true);}});
    }
    return cases;
}
}
