#include "kachakacha/kernel/OcctFabricationRegion.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#endif
namespace kachakacha::v2::kernel {
base::Result<FabricationFaceRegion> FabricationRegionOf(modeling::KernelShapeHandle handle,
    std::size_t index,double tolerance)
{
    using Out=base::Result<FabricationFaceRegion>;
#ifdef KACHACAD_V2_WITH_OCCT
    try{
        const auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        require(std::isfinite(tolerance)&&tolerance>0,"境界の許容差を正の数にしてください。");
        TopoDS_Shape shape;require(LookupShape(handle,shape),"元の面がありません。");
        TopExp_Explorer faces(shape,TopAbs_FACE);for(std::size_t i=0;i<index&&faces.More();++i)faces.Next();
        require(faces.More(),"面番号が範囲外です。");
        auto face=TopoDS::Face(faces.Current());face.Orientation(TopAbs_FORWARD);
        require(BRepCheck_Analyzer(face).IsValid(),"元の面の境界が不正です。");
        const auto surface=BRep_Tool::Surface(face);require(!surface.IsNull(),"支持曲面がありません。");
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        require(std::isfinite(u0)&&std::isfinite(u1)&&std::isfinite(v0)&&std::isfinite(v1)&&u1-u0>1e-12&&v1-v0>1e-12,"支持曲面の範囲が退化しています。");
        FabricationFaceRegion result;
        result.evaluate=[surface,u0,u1,v0,v1](double u,double v){const auto p=surface->Value(u0+(u1-u0)*v,v0+(v1-v0)*u);return geometry::Vector3{p.X(),p.Y(),p.Z()};};
        result.samples.rowCount=result.samples.columnCount=17;
        for(int r=0;r<17;++r)for(int c=0;c<17;++c)result.samples.points.push_back(result.evaluate(c/16.,r/16.));
        require(result.samples.Valid(),"支持曲面の標本が不正です。");
        std::vector<TopoDS_Wire> wires;const auto outer=BRepTools::OuterWire(face);require(!outer.IsNull(),"外周がありません。");wires.push_back(outer);
        for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next())if(!w.Current().IsSame(outer))wires.push_back(TopoDS::Wire(w.Current()));
        std::size_t totalPoints=0;
        for(const auto& wire:wires){std::vector<geometry::Point2> loop;
            for(BRepTools_WireExplorer edge(wire,face);edge.More();edge.Next()){
                double begin,end;const auto curve=BRep_Tool::CurveOnSurface(edge.Current(),face,begin,end);require(!curve.IsNull(),"境界の曲面上の位置がありません。");
                if(edge.Current().Orientation()==TopAbs_REVERSED)std::swap(begin,end);
                const auto uv=[&](double t){const auto p=curve->Value(t);return geometry::Point2{(p.Y()-v0)/(v1-v0),(p.X()-u0)/(u1-u0)};};
                std::function<void(double,double,int)> sample;
                sample=[&](double a,double b,int depth){
                    const auto pa=uv(a),pb=uv(b);const auto wa=result.evaluate(pa.u,pa.v),wb=result.evaluate(pb.u,pb.v);
                    require(wa.IsFinite()&&wb.IsFinite(),"境界の端点が不正です。");
                    double error=0;
                    for(double t:{.25,.5,.75}){const auto p=uv(a+(b-a)*t);const auto actual=result.evaluate(p.u,p.v);
                        const auto chord=wa+(wb-wa)*t;
                        const auto chart=result.evaluate(pa.u+(pb.u-pa.u)*t,pa.v+(pb.v-pa.v)*t);
                        require(actual.IsFinite()&&chart.IsFinite(),"境界の評価が不正です。");
                        error=std::max({error,geometry::Distance(actual,chord),geometry::Distance(actual,chart)});}
                    require(std::isfinite(error),"境界の標本が不正です。");
                    if(depth<3||error>tolerance){require(depth<20&&loop.size()<20000,"境界標本の上限で許容差を満たせません。");const double mid=(a+b)*.5;sample(a,mid,depth+1);sample(mid,b,depth+1);}
                    else{require(totalPoints++<20000,"境界標本の上限に達しました。");result.boundarySamplingDeviationMm=std::max(result.boundarySamplingDeviationMm,error);loop.push_back(pa);}
                };sample(begin,end,0);
            }
            require(loop.size()>=3,"閉じた境界を取得できません。");result.loops.push_back(std::move(loop));
        }
        return Out::Success(std::move(result));
    }catch(const Standard_Failure& e){return Out::Failure(base::MakeError("FAB-O001","製作する曲面の領域を取得できません。",e.what()));}
    catch(const std::exception& e){return Out::Failure(base::MakeError("FAB-O001","製作する曲面の領域を取得できません。",e.what()));}
#else
    (void)handle;(void)index;(void)tolerance;return Out::Failure(base::MakeError("KER-E005","製作領域には幾何カーネルが必要です。",{}));
#endif
}
}
