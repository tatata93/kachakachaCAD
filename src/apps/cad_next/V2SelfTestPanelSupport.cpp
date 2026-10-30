// Concave body outlines must trim the native skin, not dent its interior.
// Fixtures use document APIs; tool entry/confirm/history use the real Qt routes.
#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2Ribbon.h"
#include "V2LoopFacesTool.h"
#include "V2LoopFacesDock.h"
#include "V2ThickenDock.h"
#include "V2FabricationDock.h"
#include "V2ParameterDock.h"
#include "V2OutputTool.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/app/FabricationEvaluate.h"
#include "kachakacha/document/Commands.h"
#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QDir>
#include <QPixmap>
#include <algorithm>
#include <iostream>
#ifdef KACHACAD_V2_WITH_OCCT
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#endif

namespace kachakacha::v2::selftest {
namespace {
using geometry::CurveSegment;
using geometry::Vector3;
using base::EntityId;
using domain::EntityKind;
using modeling::ChainRole;

const domain::CreateGuideSurfaceDefinition* Definition(V2MainWindow& w,EntityId id)
{
    const auto* entity=w.Session().GetDocument().FindEntity(id);
    const auto* f=entity?w.Session().GetDocument().FindFeature(entity->createdBy):nullptr;
    return f?std::get_if<domain::CreateGuideSurfaceDefinition>(&f->definition):nullptr;
}

bool OnOriginalSkin(modeling::KernelShapeHandle result,modeling::KernelShapeHandle source)
{
#ifdef KACHACAD_V2_WITH_OCCT
    TopoDS_Shape made,original;
    if(!kernel::LookupShape(result,made)||!kernel::LookupShape(source,original))return false;
    std::vector<occ::handle<Geom_Surface>> originals;
    for(TopExp_Explorer f(original,TopAbs_FACE);f.More();f.Next())originals.push_back(BRep_Tool::Surface(TopoDS::Face(f.Current())));
    double maximum=0;int checked=0;
    for(TopExp_Explorer f(made,TopAbs_FACE);f.More();f.Next()) {
        const auto face=TopoDS::Face(f.Current());const auto surface=BRep_Tool::Surface(face);
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        for(int u=1;u<12;++u)for(int v=1;v<12;++v) {
            const gp_Pnt2d uv(u0+(u1-u0)*u/12.,v0+(v1-v0)*v/12.);
            BRepClass_FaceClassifier classified(face,uv,1e-6);if(classified.State()!=TopAbs_IN)continue;
            double distance=1e99;const auto p=surface->Value(uv.X(),uv.Y());
            for(const auto& originalSurface:originals) {
                GeomAPI_ProjectPointOnSurf projected(p,originalSurface);
                if(projected.NbPoints())distance=std::min(distance,projected.LowerDistance());
            }
            maximum=std::max(maximum,distance);++checked;
        }
    }
    std::cout<<"interior samples="<<checked<<" maximum skin deviation="<<maximum<<" mm"<<std::endl;
    return checked>=20&&maximum<1e-6;
#else
    return false;
#endif
}

bool CheckStoredSkin(V2MainWindow& w,EntityId made,EntityId source)
{
    const auto* definition=Definition(w,made);const auto* original=Definition(w,source);
    if(!definition||!original)return false;
    const auto sourceRole=static_cast<int>(ChainRole::SourceSurface);
    if(!Explain("native skin UUID is saved as an explicit source",std::find(definition->roles.begin(),definition->roles.end(),sourceRole)!=definition->roles.end()))return false;
    auto a=app::GuideTableFromDefinition(w.Session().GetDocument(),w.Session().Scene(),*definition);
    auto b=app::GuideTableFromDefinition(w.Session().GetDocument(),w.Session().Scene(),*original);
    if(!a.HasValue()||!b.HasValue())return false;
    auto result=w.BuildSurfaceFromTable(a.Value(),false);auto support=w.BuildSurfaceFromTable(b.Value(),false);
    if(!result||!support)return false;
    const bool ok=OnOriginalSkin(result->handle,support->handle);
    kernel::ReleaseShape(result->handle);kernel::ReleaseShape(support->handle);
    return Explain("interior geometry stays on original skin including the corner",ok);
}

bool Confirm(V2MainWindow& w,const app::SelectionSet& selected,EntityId& made,bool first)
{
    w.Viewport().SetSelection({});
    if(first) {
        w.RunCommand("surface.from_lines");w.Viewport().SetSelection(selected);
    } else {
        w.Viewport().SetSelection(selected);
        if(!w.Ribbon().ClickCategory(QStringLiteral("面作成"))||!w.Ribbon().ClickTool(QStringLiteral("面にする")))return false;
    }
    QApplication::processEvents();auto& tool=w.LoopFacesTool();
    std::cout<<"plan faces="<<(tool.Plan()?tool.Plan()->faces.size():0)<<" preview="<<w.Viewport().ToolPreviewFaceCount()<<"\n"<<tool.Dock()->SummaryTextJa().toStdString()<<std::endl;
    for(int i=0;i<tool.Dock()->FaceRowCount();++i)std::cout<<tool.Dock()->FaceRowTextJa(i).toStdString()<<std::endl;
    if(!Explain("one native-skinned perimeter is previewed",tool.Plan()&&tool.Plan()->faces.size()==1
        &&w.Viewport().ToolPreviewFaceCount()>0&&tool.Dock()->SummaryTextJa().contains(QStringLiteral("の形を保持"))))return false;
    if(!Explain("boundary deviation is labelled and irrelevant refill style is unavailable",
        tool.Dock()->FaceRowTextJa(0).contains(QStringLiteral("境界の最大ずれ"))&&!tool.Dock()->ChooseStyle(0,1)))return false;
    const int before=CountOfKind(w,EntityKind::GuideSurface);
    if(!tool.Dock()->ClickConfirm()||CountOfKind(w,EntityKind::GuideSurface)!=before+1)return false;
    made=w.Session().GetDocument().Snapshot().entities.back().id;
    return w.RebuildProblems().isEmpty();
}

bool Fixture(V2MainWindow& w,bool hidden,bool first)
{
    w.RunCommand("file.new");
    const auto cross=[](double y){return CurveSegment::MakeCubicBezier({{0,y,0},{0,y,3},{2,y,7},{2,y,10}}).Value();};
    const auto line=[](Vector3 a,Vector3 b){return CurveSegment::MakeLine(a,b).Value();};
    modeling::GuideTable table;table.method=modeling::GuideSurfaceMethod::RuledSections;
    app::SelectionSet selected;
    const auto add=[&](std::vector<CurveSegment> curves,const char* name){
        const auto wire=w.Session().AddWire(curves,false,name);if(!wire.committed)return EntityId{};
        const auto id=wire.createdEntityIds.front();selected.entityIds.push_back(id);return id;
    };
    for(double y:{0.,20.}) {
        const auto curve=cross(y);const auto id=add({curve},"車体の断面");if(id.IsNil())return false;
        modeling::GuideTableRow row;row.role=ChainRole::Section;row.segments={curve};row.sourceWireIds={id};row.sourceLabels={"車体の断面"};table.rows.push_back(row);
    }
    if(!w.SaveAndReopen("skin-fixture-wires.kcd2"))return false;
    const auto built=w.BuildSurfaceFromTable(table,false);if(!built)return false;
    const auto source=w.AdoptGuideSurface(table,*built,selected.entityIds,"元の裾絞り面");if(source.IsNil())return false;
    if(hidden) {
        w.Viewport().SetSelection(app::SelectionSet{{source}});w.RunCommand("view.hide_selected");
        if(!Explain("fixture source is hidden",w.Session().GetDocument().FindEntity(source)->visibility==domain::Visibility::Hidden))return false;
    }
    std::vector<CurveSegment> border{line({0,0,0},{0,20,0}),line({2,0,10},{2,7,10}),
        *cross(7).Split(.55).Value().second,line(cross(7).Evaluate(.55),cross(13).Evaluate(.55)),
        *cross(13).Split(.55).Value().second,line({2,13,10},{2,20,10})};
    if(add(border,"扉の切り欠き付き外周").IsNil()||!w.SaveAndReopen("skin-fixture.kcd2"))return false;
    EntityId made;if(!Confirm(w,selected,made,first)||!CheckStoredSkin(w,made,source))return false;
    const int count=CountOfKind(w,EntityKind::GuideSurface);w.RunCommand("edit.undo");
    if(CountOfKind(w,EntityKind::GuideSurface)!=count-1)return false;
    w.RunCommand("edit.redo");
    return Explain("trimmed skin survives undo redo and native reopen",w.SaveAndReopen("skin-fixture-result.kcd2")
        &&w.RebuildProblems().isEmpty()&&CheckStoredSkin(w,made,source));
}

bool Owner(V2MainWindow& w)
{
    if(!w.OpenDocumentFile(qEnvironmentVariable("KACHACAD_PANEL_OWNER_INPUT")))return false;
    const auto source=*base::EntityId::Parse("00000000-0000-4000-8000-000000000439");
    app::SelectionSet selected;
    for(const char* suffix:{"0070","01a2","0431","0327","1a55","1a58","16d5"})
        for(const auto& entity:w.Session().GetDocument().Snapshot().entities)if(entity.id.ShortSuffix()==suffix)selected.entityIds.push_back(entity.id);
    if(selected.entityIds.size()!=7)return false;
    for(const auto id:selected.entityIds) {
        app::SelectionRef ref;ref.entityId=id;
        if(id.ShortSuffix()!="16d5")selected.ordered.push_back(ref);
        else {
            const auto* entity=w.Session().GetDocument().FindEntity(id);
            const auto* feature=w.Session().GetDocument().FindFeature(entity->createdBy);
            const auto& wire=std::get<domain::CreateWireDefinition>(feature->definition);
            for(const std::size_t at:{0u,4u,5u}) {ref.kind=app::SelectionElementKind::Edge;ref.segmentId=wire.segmentIds[at];selected.ordered.push_back(ref);}
        }
    }
    const auto original=w.Session().Scene().curves;
    w.resize(1600,1000);w.ApplyTheme(UiTheme::Windows95);w.Viewport().SetViewDirection(ViewDirection::Isometric);
    auto settings=w.Viewport().DisplaySettingsNow();settings.gridVisible=false;w.Viewport().SetDisplaySettings(settings);
    w.Viewport().SetSelection(selected);w.RunCommand("view.fit_selection");
    const auto output=qEnvironmentVariable("KACHACAD_PANEL_OWNER_OUTPUT");QDir().mkpath(output);
    EntityId made;if(!Confirm(w,selected,made,true)||!CheckStoredSkin(w,made,source))return false;
    app::SelectionSet face;face.entityIds={made};w.Viewport().SetSelection(face);w.Viewport().IsolateSelection();w.RunCommand("view.fit_selection");
    QApplication::processEvents();w.grab().save(output+"/01-body-skin.png");
    w.SetPathChooser([output](bool){return output+"/body-with-door-notch.kcd2";});w.RunCommand("file.save_as");w.SetPathChooser({});
    for(const auto& curve:original) {
        const auto& now=w.Session().Scene().curves;
        const auto at=std::find_if(now.begin(),now.end(),[&](const auto& c){return c.entityId==curve.entityId&&c.segmentId==curve.segmentId;});
        if(at==now.end()||at->segment.Kind()!=curve.segment.Kind())return false;
        for(double t:{0.,.25,.5,.75,1.})if(geometry::Distance(at->segment.Evaluate(t),curve.segment.Evaluate(t))>1e-12)return false;
    }
    w.RunCommand("edit.undo");w.RunCommand("edit.redo");
    if(!w.SaveAndReopen("owner-body-skin-result.kcd2")||!CheckStoredSkin(w,made,source))return false;
    face.entityIds={made};w.Viewport().SetSelection(face);w.RunCommand("part.thicken");
    auto* amount=w.findChild<QDoubleSpinBox*>("thickenAmount");if(!amount)return false;amount->setValue(.5);
    if(!w.ThickenDock().ClickConfirm()) {Note(w.StatusText().toUtf8().constData());return false;}
    const auto part=w.Session().GetDocument().Snapshot().entities.back().id;
    face.entityIds={part};w.Viewport().SetSelection(face);w.Viewport().IsolateSelection();w.RunCommand("view.fit_selection");
    QApplication::processEvents();w.grab().save(output+"/02-body-solid.png");
    w.SetPathChooser([output](bool){return output+"/body-solid.kcd2";});w.RunCommand("file.save_as");w.SetPathChooser({});
    if(!w.OpenDocumentFile(output+"/body-solid.kcd2")||!w.RebuildProblems().isEmpty())return false;
    face.entityIds={part};w.Viewport().SetSelection(face);
    auto* exporter=V2OutputTool::Open(w,0);
    bool exported=exporter->SaveTo(output+"/body-solid.stl");exporter->ChooseFormat(1);
    exported=exporter->SaveTo(output+"/body-solid.step")&&exported;
    if(!exported)for(const auto* label:exporter->findChildren<QLabel*>())Note(label->text().toUtf8().constData());
    exporter->ChooseFormat(2);exported=exporter->SaveTo(output+"/body-solid-independent.kcd2")&&exported;
    exporter->close();
    if(!Explain("reopened body solid exports native STEP and printable STL",exported))return false;
    if(!w.OpenDocumentFile(output+"/body-with-door-notch.kcd2"))return false;
    w.Viewport().ClearIsolation();face.entityIds={made};w.Viewport().SetSelection(face);w.Viewport().IsolateSelection();
    auto* automatic=w.findChild<QComboBox*>("fabricationAutomaticGeneration");if(!automatic)return false;automatic->setCurrentIndex(0);
    w.RunCommand("fabrication.create");w.Viewport().SetSelection(face);
    auto& dock=w.FabricationDock();dock.SetEqualPartCount(1);dock.SetSplitAxisIndex(2);
    if(!w.ParameterDock().Apply(app::ParameterId::MaxDeviationMm,QStringLiteral("0.1"))||!dock.ClickCandidate(1))return false;
    const auto& previews=w.ApproxEvaluations();
    if(!Explain("body skin with concave door cutout supports fabrication approximation",previews.size()>1&&previews[1]&& !previews[1]->panels.empty()))return false;
    QApplication::processEvents();w.grab().save(output+"/03-approximation.png");
    return true;
}
}

std::vector<SelfTestCase> PanelSupportCases()
{
    std::vector<SelfTestCase> cases{
        {"HP-SKIN-01 隠した裾絞り面の切り欠きを保持",[](V2MainWindow& w){return Fixture(w,true,true);}},
        {"HP-SKIN-02 選択後の帯ボタンでも元面の形を保持",[](V2MainWindow& w){return Fixture(w,false,false);}}
    };
    if(qEnvironmentVariableIsSet("KACHACAD_PANEL_OWNER_INPUT"))cases.push_back({"HP-SKIN-115 添付13辺の車体面・厚み・近似",Owner});
    return cases;
}
}
