#include "kachakacha/kernel/OcctCurvedEmboss.h"
#include <cmath>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <stdexcept>
#include <algorithm>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepFill.hxx>
#include <BRepLib.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_IntCS.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom2dAPI_Interpolate.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <GeomProjLib.hxx>
#include <Geom_OffsetSurface.hxx>
#include <Geom_Line.hxx>
#include <GProp_GProps.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeFix_Solid.hxx>
#include <TColgp_HArray1OfPnt2d.hxx>
#include <TColStd_HArray1OfReal.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Lin.hxx>
#endif
namespace kachakacha::v2::kernel {
using Out = base::Result<CurvedEmbossResult>;
#ifdef KACHACAD_V2_WITH_OCCT
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
gp_Pnt2d NearDomain(gp_Pnt2d uv, const TopoDS_Face& face, const occ::handle<Geom_Surface>& surface)
{
    double u0,u1,v0,v1; BRepTools::UVBounds(face,u0,u1,v0,v1);
    if (surface->IsUPeriodic()) uv.SetX(uv.X()+std::round(((u0+u1)*0.5-uv.X())/surface->UPeriod())*surface->UPeriod());
    if (surface->IsVPeriodic()) uv.SetY(uv.Y()+std::round(((v0+v1)*0.5-uv.Y())/surface->VPeriod())*surface->VPeriod());
    return uv;
}
TopoDS_Face TrimFace(const TopoDS_Face& face, const std::vector<geometry::CurveSegment>& boundary,
    double tolerance)
{
    Require(!boundary.empty(), "曲面上の閉じた輪郭を選んでください。");
    const auto surface = BRep_Tool::Surface(face);
    BRepBuilderAPI_MakeWire wire;
    for (const auto& segment : boundary) {
        const auto converted = ToEdge(segment);
        Require(converted.HasValue(), "輪郭の曲線を変換できません。");
        double first, last;
        const auto curve = BRep_Tool::Curve(converted.Value(), first, last);
        Require(!curve.IsNull(), "輪郭に曲線がありません。");
        for (int i = 0; i <= 32; ++i) {
            const auto point = curve->Value(first + (last-first)*i/32.0);
            GeomAPI_ProjectPointOnSurf projection(point, surface);
            Require(projection.NbPoints() > 0 && projection.LowerDistance() <= tolerance,
                "輪郭が曲面上にありません。先に曲面へ投影してください。");
            double u, v; projection.LowerDistanceParameters(u, v);
            BRepClass_FaceClassifier inside(face, NearDomain(gp_Pnt2d(u,v),face,surface), tolerance);
            Require(inside.State() == TopAbs_IN || inside.State() == TopAbs_ON,
                "輪郭が選択した面の範囲から外れています。");
        }
        double achieved = tolerance;
        const auto pc = GeomProjLib::Curve2d(curve, first, last, surface, achieved);
        Require(!pc.IsNull() && achieved <= tolerance, "曲面上の輪郭を許容偏差内で作れません。");
        const auto middle = pc->Value((first+last)*0.5);
        pc->Translate(gp_Vec2d(middle,NearDomain(middle,face,surface)));
        BRepBuilderAPI_MakeEdge edge(pc, surface, first, last);
        Require(edge.IsDone(), "曲面上の辺を作れません。");
        auto e = edge.Edge();
        Require(BRepLib::BuildCurve3d(e, tolerance), "輪郭の3D曲線を作れません。");
        if (converted.Value().Orientation() == TopAbs_REVERSED) e.Reverse();
        wire.Add(e);
        Require(wire.IsDone(), "輪郭の線が連続していません。");
    }
    Require(wire.Wire().Closed(), "輪郭が閉じていません。");
    BRepBuilderAPI_MakeFace trimmed(surface, wire.Wire(), true);
    Require(trimmed.IsDone(), "輪郭で曲面を切り出せません。");
    ShapeFix_Face fix(trimmed.Face()); fix.SetPrecision(tolerance); fix.Perform();
    auto result = fix.Face();
    Require(BRepCheck_Analyzer(result).IsValid(), "曲面上の輪郭が自己交差しています。");
    if (face.Orientation() == TopAbs_REVERSED) result.Reverse();
    return result;
}
struct BoundaryMapper {
    occ::handle<Geom_Surface> base;
    occ::handle<Geom_Surface> top;
    CurvedEmbossOptions options;
    double normalSign = 1;
    gp_Pnt2d Map(const occ::handle<Geom2d_Curve>& curve, double t, double traversal) const
    {
        gp_Pnt2d uv; gp_Vec2d derivative; curve->D1(t, uv, derivative);
        if (options.draftRad == 0 && options.tiltRad == 0) return uv;
        gp_Pnt p; gp_Vec du, dv; base->D1(uv.X(), uv.Y(), p, du, dv);
        gp_Vec n = du.Crossed(dv); Require(n.Magnitude() > 1e-12, "曲面の法線が定まりません。");
        n.Normalize(); n *= normalSign;
        gp_Vec tangent = (du*derivative.X()+dv*derivative.Y())*traversal;
        Require(tangent.Magnitude() > 1e-12, "輪郭の接線が定まりません。"); tangent.Normalize();
        // Outward of the positively oriented face boundary.
        const gp_Vec outward = tangent.Crossed(n).Normalized();
        gp_Vec leaning(0,0,0);
        if (options.tiltRad != 0) {
            leaning = ToVector(options.tiltReference);
            leaning -= n*leaning.Dot(n);
            Require(leaning.Magnitude() > 1e-10, "傾ける方向が法線と平行です。向きを変えてください。");
            leaning.Normalize();
        }
        const gp_Vec direction = n*options.heightMm
            + leaning*(std::abs(options.heightMm)*std::tan(options.tiltRad))
            - outward*(std::abs(options.heightMm)*std::tan(options.draftRad));
        occ::handle<Geom_Line> ray = new Geom_Line(gp_Lin(p, gp_Dir(direction)));
        GeomAPI_IntCS intersection(ray, top);
        Require(intersection.IsDone(), "側壁と上面の交差を計算できません。");
        double best = 1e100; gp_Pnt2d result;
        for (int i=1; i<=intersection.NbPoints(); ++i) {
            double u,v,w; intersection.Parameters(i,u,v,w);
            if (w <= 0) continue;
            const double error = std::abs(w-direction.Magnitude());
            if (error < best) { best=error; result=gp_Pnt2d(u,v); }
        }
        Require(best < 1e99, "指定角度では側壁が上面に届きません。角度か高さを小さくしてください。");
        if (top->IsUPeriodic()) result.SetX(result.X()+std::round((uv.X()-result.X())/top->UPeriod())*top->UPeriod());
        if (top->IsVPeriodic()) result.SetY(result.Y()+std::round((uv.Y()-result.Y())/top->VPeriod())*top->VPeriod());
        return result;
    }
};
struct TopEdge { TopoDS_Edge edge; double error = 0; };
TopEdge MapEdge(const TopoDS_Edge& edge, const TopoDS_Face& face, const BoundaryMapper& mapper)
{
    double first,last;
    auto pc = BRep_Tool::CurveOnSurface(edge, face, first,last);
    Require(!pc.IsNull(), "輪郭の曲面パラメータがありません。");
    const double traversal = edge.Orientation() == TopAbs_REVERSED ? -1.0 : 1.0;
    if (traversal < 0) std::swap(first,last);
    for (const int count : {16,32,64,128}) {
        occ::handle<TColgp_HArray1OfPnt2d> points = new TColgp_HArray1OfPnt2d(1,count+1);
        occ::handle<TColStd_HArray1OfReal> parameters = new TColStd_HArray1OfReal(1,count+1);
        for (int i=0;i<=count;++i) {
            points->SetValue(i+1,mapper.Map(pc,first+(last-first)*i/count,traversal));
            parameters->SetValue(i+1,static_cast<double>(i)/count);
        }
        if (BRep_Tool::IsClosed(edge)) {
            const auto a=points->Value(1),b=points->Value(count+1);
            Require(mapper.top->Value(a.X(),a.Y()).Distance(mapper.top->Value(b.X(),b.Y()))<=mapper.options.toleranceMm,
                "輪郭の継ぎ目で押し出し方向が不連続です。角を丸めるか角度を小さくしてください。");
            points->SetValue(count+1,a);
        }
        Geom2dAPI_Interpolate fit(points,parameters,false,1e-10); fit.Perform();
        Require(fit.IsDone(), "上面の輪郭を補間できません。");
        double error=0;
        for (int i=0;i<count;++i) {
            const double fraction=(i+0.5)/count;
            const auto expected=mapper.Map(pc,first+(last-first)*fraction,traversal);
            const auto actual=fit.Curve()->Value(fraction);
            error=std::max(error,mapper.top->Value(expected.X(),expected.Y()).Distance(mapper.top->Value(actual.X(),actual.Y())));
        }
        if (error > mapper.options.toleranceMm) continue;
        BRepBuilderAPI_MakeEdge made(fit.Curve(),mapper.top,0.0,1.0);
        Require(made.IsDone(), "上面の辺を作れません。");
        auto result=made.Edge();
        Require(BRepLib::BuildCurve3d(result,mapper.options.toleranceMm), "上面の3D曲線を作れません。");
        return {result,error};
    }
    throw std::runtime_error("上面の輪郭が許容偏差に収まりません。角度や高さを小さくしてください。");
}
CurvedEmbossResult Build(const TopoDS_Face& source, const std::vector<geometry::CurveSegment>& boundary,
    const CurvedEmbossOptions& options)
{
    auto bottom=TrimFace(source,boundary,options.toleranceMm);
    BoundaryMapper mapper; mapper.base=BRep_Tool::Surface(bottom); mapper.options=options;
    mapper.normalSign=bottom.Orientation()==TopAbs_REVERSED ? -1 : 1;
    mapper.top=new Geom_OffsetSurface(mapper.base,options.heightMm*mapper.normalSign);
    BRepBuilderAPI_MakeWire topWire;
    BRepBuilderAPI_Sewing sewing(options.toleranceMm);
    sewing.Add(bottom); double error=0;
    for (BRepTools_WireExplorer edges(BRepTools::OuterWire(bottom),bottom);edges.More();edges.Next()) {
        const auto upper=MapEdge(edges.Current(),bottom,mapper);
        error=std::max(error,upper.error); topWire.Add(upper.edge);
        sewing.Add(BRepFill::Face(edges.Current(),upper.edge));
    }
    Require(topWire.IsDone() && topWire.Wire().Closed(), "上面の輪郭が閉じていません。抜き勾配を使う輪郭は、角を丸めて滑らかにつないでください。");
    BRepBuilderAPI_MakeFace top(mapper.top,topWire.Wire(),true);
    Require(top.IsDone(), "上面を作れません。");
    ShapeFix_Face fix(top.Face()); fix.SetPrecision(options.toleranceMm); fix.Perform();
    sewing.Add(fix.Face()); sewing.Perform();
    TopExp_Explorer shells(sewing.SewedShape(),TopAbs_SHELL);
    Require(shells.More(), "曲面押し出しの面を閉じられません。");
    const auto shell=TopoDS::Shell(shells.Current()); shells.Next();
    Require(!shells.More(), "曲面押し出しの面が分離しています。");
    ShapeFix_Solid solidFix(BRepBuilderAPI_MakeSolid(shell).Solid()); solidFix.Perform();
    auto shape=solidFix.Solid();
    Require(BRepCheck_Analyzer(shape).IsValid(), "曲面押し出しが自己交差しています。高さか角度を小さくしてください。");
    GProp_GProps props; BRepGProp::VolumeProperties(shape,props);
    const double volume=std::abs(props.Mass());
    Require(volume>std::pow(options.toleranceMm,3), "曲面押し出しの体積がありません。");
    return {StoreShape(shape),volume,error};
}
}
#endif
Out BuildCurvedEmboss(modeling::KernelShapeHandle source, const std::vector<geometry::CurveSegment>& boundary,
    const CurvedEmbossOptions& options)
{
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        Require(std::isfinite(options.heightMm) && std::abs(options.heightMm)>options.toleranceMm,
            "高さは許容偏差より大きく指定してください。");
        Require(std::isfinite(options.draftRad) && std::isfinite(options.tiltRad)
            && std::abs(options.draftRad)<1.4 && std::abs(options.tiltRad)<1.4,
            "角度は-80度から80度の範囲で指定してください。");
        Require(options.toleranceMm>0 && std::isfinite(options.toleranceMm) && options.tiltReference.IsFinite(),
            "許容偏差または方向が不正です。");
        TopoDS_Shape shape; Require(LookupShape(source,shape), "元の曲面がありません。");
        TopExp_Explorer faces(shape,TopAbs_FACE); Require(faces.More(), "曲面を1面選んでください。");
        const auto face=TopoDS::Face(faces.Current()); faces.Next();
        Require(!faces.More(), "複数面をまたぐ輪郭は面ごとに指定してください。");
        return Out::Success(Build(face,boundary,options));
    } catch (const std::exception& e) {
        return Out::Failure(base::MakeError("EMB-001", "曲面押し出しを作れません。",e.what()));
    } catch (...) { return Out::Failure(base::MakeError("EMB-001", "曲面押し出しの幾何計算に失敗しました。",{})); }
#else
    (void)source; (void)boundary; (void)options;
    return Out::Failure(base::MakeError("KER-E005", "幾何カーネルが必要です。",{}));
#endif
}
}
