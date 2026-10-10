#include "V2SelfTest.h"
#include "V2MainWindow.h"
#include "V2Viewport.h"
#include "V2LoopFacesTool.h"
#include "kachakacha/app/GuideTableBuild.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <QApplication>
#include <QPixmap>
#include <QString>
#include <algorithm>
#include <cmath>
#include <optional>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <GeomAPI_IntCS.hxx>
#include <Geom_Line.hxx>
#include <Geom_Surface.hxx>
#endif

namespace kachakacha::v2::selftest {
namespace {
bool ComparePhysicalSections(modeling::KernelShapeHandle dome, modeling::KernelShapeHandle standard)
{
#ifdef KACHACAD_V2_WITH_OCCT
    const auto surface=[](modeling::KernelShapeHandle handle) -> occ::handle<Geom_Surface> {
        TopoDS_Shape shape; if(!kernel::LookupShape(handle,shape))return {};
        TopExp_Explorer faces(shape,TopAbs_FACE);
        return faces.More() ? BRep_Tool::Surface(TopoDS::Face(faces.Current())) : occ::handle<Geom_Surface>{};
    };
    const auto a=surface(dome),b=surface(standard); if(a.IsNull() || b.IsNull())return false;
    const auto height=[](const occ::handle<Geom_Surface>& face,double x,double y) -> std::optional<double> {
        const occ::handle<Geom_Curve> line=new Geom_Line(gp_Lin(gp_Pnt(x,y,0),gp_Dir(0,0,1)));
        GeomAPI_IntCS hit(line,face);
        if(!hit.IsDone() || hit.NbPoints()!=1)return std::nullopt;
        return hit.Point(1).Z();
    };
    int middle=0,outer=0; double maximum=0;
    for(double x:{6.0,12.0,15.0})for(double y:{-5.0,-10.0,-15.0,-20.0}) {
        const auto z=height(a,x,y),old=height(b,x,y);
        if(!z || !old)return false;
        const double lift=*z-*old; maximum=std::max(maximum,lift);
        if(lift>0.05 && x>=6)++middle;
        if(lift>0.05 && x>=12)++outer;
    }
    const auto text=QStringLiteral("115同じ位置の断面: 最大持上げ %1 mm / 中央外側 %2点 / 外周寄り %3点").arg(maximum,0,'g',6).arg(middle).arg(outer);
    Note(text.toUtf8().constData());
    return middle>=6 && outer>=4;
#else
    (void)dome;(void)standard;return false;
#endif
}

bool CheckRightShape(V2MainWindow& window)
{
    const auto snapshot=window.Session().GetDocument().Snapshot();
    for(const auto& feature:snapshot.features) {
        const auto* definition=std::get_if<domain::CreateGuideSurfaceDefinition>(&feature.definition);
        if(!definition || definition->fourEdgeStyle!=3) continue;
        const auto table=app::GuideTableFromDefinition(window.Session().GetDocument(),window.Session().Scene(),*definition);
        if(!table.HasValue()) return false;
        const geometry::GeometryTolerance tolerance;
        const auto request=modeling::ToGuideSurfaceRequest(table.Value(),tolerance);
        if(!request.HasValue()) return false;
        const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request.Value(),tolerance);
        if(!analysis.HasValue()) return false;
        const auto built=kernel::BuildGuideSurface(request.Value(),analysis.Value(),tolerance);
        if(!built.HasValue()) return false;
        const bool bounded=std::all_of(built.Value().samples.points.begin(),built.Value().samples.points.end(),[](const auto& point){
            return point.x >= -0.01 && point.x <= 18.01 && point.z >= 26.99 && point.z <= 32.51;
        });
        auto standard=request.Value(); standard.fourEdgeStyle=modeling::FourEdgeStyle::Coons;
        const auto old=kernel::BuildGuideSurface(standard,analysis.Value(),tolerance);
        const bool spread=old.HasValue() && ComparePhysicalSections(built.Value().handle,old.Value().handle);
        if(old.HasValue()) kernel::ReleaseShape(old.Value().handle);
        kernel::ReleaseShape(built.Value().handle);
        return Explain("右の選択区画だけで外周の範囲に収まる",bounded)
            && Explain("中央断面から離れた内部にも丸みを伝える",spread);
    }
    return false;
}

#ifdef KACHACAD_V2_WITH_OCCT
std::optional<gp_Pnt> RoofPoint(const occ::handle<Geom_Surface>& s,double x,double y)
{
    double u0,u1,v0,v1;s->Bounds(u0,u1,v0,v1);
    const bool exchange=std::abs(s->Value(u1,(v0+v1)/2).Y()-s->Value(u0,(v0+v1)/2).Y())
        >std::abs(s->Value((u0+u1)/2,v1).Y()-s->Value((u0+u1)/2,v0).Y());
    const double a0=exchange ? v0 : u0,a1=exchange ? v1 : u1,b0=exchange ? u0 : v0,b1=exchange ? u1 : v1;
    const auto value=[&](double a,double b){return exchange ? s->Value(b,a) : s->Value(a,b);};
    double low=b0,high=b1;
    const bool yAscending=value((a0+a1)/2,b1).Y()>value((a0+a1)/2,b0).Y();
    for(int k=0;k<55;++k) {
        const double mid=(low+high)/2;
        if((value((a0+a1)/2,mid).Y()<y)==yAscending)low=mid;else high=mid;
    }
    const double b=(low+high)/2;
    const double left=value(a0,b).X(),right=value(a1,b).X();
    if(x<std::min(left,right)-1e-8 || x>std::max(left,right)+1e-8)return std::nullopt;
    low=a0;high=a1;
    for(int k=0;k<55;++k) {
        const double mid=(low+high)/2;
        if((value(mid,b).X()<x)==(right>left))low=mid;else high=mid;
    }
    const auto p=value((low+high)/2,b);
    if(std::abs(p.X()-x)>1e-7 || std::abs(p.Y()-y)>1e-7)return std::nullopt;
    return p;
}
#endif

bool InspectBoth(V2MainWindow& window)
{
#ifdef KACHACAD_V2_WITH_OCCT
    std::vector<occ::handle<Geom_Surface>> surfaces;
    double deviation=0;
    for(const auto& feature:window.Session().GetDocument().Snapshot().features) {
        const auto* definition=std::get_if<domain::CreateGuideSurfaceDefinition>(&feature.definition);
        if(!definition || definition->fourEdgeStyle!=3)continue;
        const geometry::GeometryTolerance tolerance;
        const auto table=app::GuideTableFromDefinition(window.Session().GetDocument(),window.Session().Scene(),*definition);
        if(!table.HasValue())return false;
        const auto request=modeling::ToGuideSurfaceRequest(table.Value(),tolerance);if(!request.HasValue())return false;
        const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request.Value(),tolerance);if(!analysis.HasValue())return false;
        const auto built=kernel::BuildGuideSurface(request.Value(),analysis.Value(),tolerance);
        if(!built.HasValue())return false;
        TopoDS_Shape shape;if(!kernel::LookupShape(built.Value().handle,shape))return false;
        TopExp_Explorer faces(shape,TopAbs_FACE);if(!faces.More())return false;
        surfaces.push_back(BRep_Tool::Surface(TopoDS::Face(faces.Current())));
        deviation=std::max(deviation,built.Value().maximumDeviationMm);kernel::ReleaseShape(built.Value().handle);
    }
    if(surfaces.size()!=2)return false;
    if(RoofPoint(surfaces[0],1,-10))std::swap(surfaces[0],surfaces[1]);
    double mirrorError=0,peak=0,slopeError=0;int pairs=0;
    for(double y:{-26.5,-26.0,-25.5,-25.0,-24.0,-23.0,-20.0,-15.0,-10.0,-5.0}) {
        double previous=1e10;
        for(double x=0;x<=17.75;x+=0.25) {
            const auto a=RoofPoint(surfaces[0],-x,y),b=RoofPoint(surfaces[1],x,y);
            if(!a || !b)continue;
            ++pairs;mirrorError=std::max(mirrorError,std::abs(a->Z()-b->Z()));
            peak=std::max(peak,b->Z()-previous);previous=b->Z();
        }
        const auto a=RoofPoint(surfaces[0],-0.001,y),b=RoofPoint(surfaces[1],0.001,y),c=RoofPoint(surfaces[1],0,y);
        if(!a || !b || !c)return false;
        slopeError=std::max(slopeError,std::abs((a->Z()+b->Z()-2*c->Z())/0.001));
    }
    Note(QStringLiteral("115両側: %1組 / 鏡像の高さ差 %2 mm / 余計な山 %3 mm / 接合傾き差 %4 / 境界ずれ %5 mm")
        .arg(pairs).arg(mirrorError,0,'g',8).arg(peak,0,'g',8).arg(slopeError,0,'g',8).arg(deviation,0,'g',8).toUtf8().constData());
    // Circular boundaries are polynomial approximations (1e-6 mm); near the
    // upright skirt their positional error is amplified in vertical intersections.
    const geometry::GeometryTolerance tolerance;
    return pairs>600 && mirrorError<10*tolerance.modelLinearMm && peak<tolerance.modelLinearMm
        && slopeError<0.001 && deviation<1e-4;
#endif
    return false;
}
}

bool CaseDome(V2MainWindow& window,bool both)
{
    const auto path=qEnvironmentVariable("KACHACAD_SURFACE_TEST_FILE");
    if(path.isEmpty() || !window.OpenDocumentFile(path)) return false;
    const auto original=window.Session().GetDocument().Snapshot();
    const int before=CountOfKind(window,domain::EntityKind::GuideSurface);
    window.resize(1600,1000); window.ApplyTheme(UiTheme::Windows95);
    window.Viewport().SetViewDirection(ViewDirection::Isometric); window.Viewport().FitToDocument();
    window.Viewport().SetSelection({}); window.RunCommand("surface.from_lines");
    app::SelectionSet selected;
    for(const auto& entity:original.entities)for(const std::string suffix:{"02e","038","03b","302","340","175","304"})
        if(entity.id.ToString().ends_with(suffix)) selected.entityIds.push_back(entity.id);
    if(selected.entityIds.size()!=7) return false;
    window.Viewport().SetSelection(selected); QApplication::processEvents();
    const auto& plan=window.LoopFacesTool().Plan();
    if(!plan || plan->faces.size()!=2) return false;
    int right=-1;
    for(std::size_t i=0;i<plan->faces.size();++i) {
        double x=0; for(const auto& line:plan->faces[i].previewLines)for(const auto& p:line)x+=p.x;
        if(x>0) right=static_cast<int>(i);
    }
    auto* dock=window.LoopFacesTool().Dock();
    if(right<0 || (!both && !dock->ToggleMake(1-right))) return false;
    const auto output=qEnvironmentVariable("KACHACAD_SURFACE_TEST_OUTPUT")+(both ? "-both-dome" : "-right-dome");
    for(int style:{0,3}) {
        if(!dock->ChooseStyle(right,style)) return false;
        QApplication::processEvents();
        if(both && style==3 && !dock->SummaryTextJa().contains(QStringLiteral("区画ごとに異なります")))return false;
        if(!dock->ClickApplyStyleToAll(right))return false;
        if(!both && !dock->FaceRowTextJa(1-right).contains(QStringLiteral("標準")))return false;
        QApplication::processEvents();
        if(!Explain("選んだ右区画の実面プレビュー",window.Viewport().ToolPreviewFaceCount()>0
            && (both || dock->FaceRowTextJa(1-right).contains(QStringLiteral("作らない"))))) return false;
        if(!qEnvironmentVariable("KACHACAD_SURFACE_TEST_OUTPUT").isEmpty())
            window.grab().save(output+"-style-"+QString::number(style)+".png");
    }
    window.HandleToolKey(Qt::Key_Return,nullptr); QApplication::processEvents();
    if(both) {
        if(CountOfKind(window,domain::EntityKind::GuideSurface)!=before+2
            || window.Session().GetDocument().Snapshot().entities.size()!=original.entities.size()+2 || !InspectBoth(window))return false;
        window.RunCommand("edit.undo");if(CountOfKind(window,domain::EntityKind::GuideSurface)!=before)return false;
        window.RunCommand("edit.redo");if(!window.RebuildProblems().isEmpty() || !InspectBoth(window))return false;
        window.Viewport().SetSelection({});QApplication::processEvents();
        if(!qEnvironmentVariable("KACHACAD_SURFACE_TEST_OUTPUT").isEmpty()) {
            window.grab().save(output+"-result.png");
            window.Viewport().OrbitByPixels(110,90);QApplication::processEvents();window.grab().save(output+"-oblique.png");
            if(!window.SaveAndReopen(output+"-result.kcd2") || !window.RebuildProblems().isEmpty() || !InspectBoth(window))return false;
        }
        return true;
    }
    if(!Explain("右1枚を確定し元の全要素を保持",CountOfKind(window,domain::EntityKind::GuideSurface)==before+1
        && window.Session().GetDocument().Snapshot().entities.size()==original.entities.size()+1)) return false;
    if(!CheckRightShape(window)) return false;
    window.RunCommand("edit.undo"); if(CountOfKind(window,domain::EntityKind::GuideSurface)!=before) return false;
    window.RunCommand("edit.redo"); if(!window.RebuildProblems().isEmpty() || !CheckRightShape(window)) return false;
    window.Viewport().SetSelection({}); QApplication::processEvents();
    if(!qEnvironmentVariable("KACHACAD_SURFACE_TEST_OUTPUT").isEmpty()) {
        window.grab().save(output+"-result.png");
        if(!window.SaveAndReopen(output+"-result.kcd2") || !window.RebuildProblems().isEmpty()) return false;
        if(!CheckRightShape(window)) return false;
    }
    return true;
}

bool CaseOwnerDome(V2MainWindow& window) { return CaseDome(window,false); }
bool CaseOwnerDomeBoth(V2MainWindow& window) { return CaseDome(window,true); }
}
