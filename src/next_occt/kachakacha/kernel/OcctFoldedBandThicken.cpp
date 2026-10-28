#include "kachakacha/kernel/OcctFoldedBandThicken.h"
#include "kachakacha/kernel/OcctFabricationRegion.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/fabrication/BandMaterialRegion.h"
#include "kachakacha/fabrication/BandFold.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepLib.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <array>
namespace kachakacha::v2::kernel::detail {
namespace {
using geometry::Vector3;
using Out=base::Result<TopoDS_Shape>;
Out Failed(){return Out::Failure(base::MakeError(kThickenFailed,
    "折れた帯の厚み面をつなげられません。","折れ角が鋭すぎるか、厚み面が交差しています。帯を分けるか厚みを小さくしてください。"));}
bool AddFace(BRepBuilderAPI_Sewing& sewing,const std::vector<Vector3>& points) {
    BRepBuilderAPI_MakePolygon polygon;
    for(const auto& p:points)polygon.Add(gp_Pnt(p.x,p.y,p.z));polygon.Close();
    if(!polygon.IsDone())return false;
    BRepBuilderAPI_MakeFace face(polygon.Wire(),true);
    if(!face.IsDone())return false;sewing.Add(face.Face());return true;
}
bool AddSide(BRepBuilderAPI_Sewing& sewing,const Vector3& a,const Vector3& b,const Vector3& c,const Vector3& d) {
    return AddFace(sewing,{a,b,c})&&AddFace(sewing,{a,c,d});
}

std::vector<Vector3> Clean(std::vector<Vector3> points,double eps){
    std::vector<Vector3> out;for(auto p:points)if(out.empty()||geometry::Distance(p,out.back())>eps*1.001)out.push_back(p);
    if(out.size()>1&&geometry::Distance(out.front(),out.back())<=eps*1.001)out.pop_back();return out;
}
bool AddMaterialFace(BRepBuilderAPI_Sewing& sewing,const std::vector<std::vector<Vector3>>& loops,Vector3 normal,double eps){
    if(loops.empty()||normal.Length()<1e-12)return false;
    normal=geometry::Normalized(normal);std::vector<TopoDS_Wire> wires;
    for(std::size_t l=0;l<loops.size();++l){const auto points=Clean(loops[l],eps);if(points.size()<3)return false;
        BRepBuilderAPI_MakePolygon polygon;Vector3 area{};
        for(std::size_t i=0;i<points.size();++i){const auto p=points[i];polygon.Add(gp_Pnt(p.x,p.y,p.z));area=area+geometry::Cross(p,points[(i+1)%points.size()]);}
        polygon.Close();if(!polygon.IsDone())return false;auto wire=polygon.Wire();if((geometry::Dot(area,normal)>0)!=(l==0))wire.Reverse();wires.push_back(wire);
    }
    const auto p=loops.front().front();BRepBuilderAPI_MakeFace face(gp_Pln(gp_Pnt(p.x,p.y,p.z),gp_Dir(normal.x,normal.y,normal.z)),wires.front(),true);
    for(std::size_t l=1;l<wires.size();++l)face.Add(wires[l]);if(!face.IsDone())return false;sewing.Add(face.Face());return true;
}
Out TrimmedBandSolid(const TopoDS_Face& face,const occ::handle<Geom_BSplineSurface>& surface,
    const std::vector<Vector3>& a,const std::vector<Vector3>& b,const std::vector<Vector3>& normals,
    const std::vector<Vector3>& al,const std::vector<Vector3>& bl,const std::vector<Vector3>& ah,const std::vector<Vector3>& bh,
    const geometry::GeometryTolerance& tolerance)
{
    const auto handle=StoreShape(face);const auto source=FabricationRegionOf(handle,0,tolerance.modelLinearMm*.1);ReleaseShape(handle);
    if(!source.HasValue())return Out::Failure(source.Diagnostics());
    double u0,u1,v0,v1,s0,s1,t0,t1;BRepTools::UVBounds(face,u0,u1,v0,v1);surface->Bounds(s0,s1,t0,t1);
    fabrication::BandMesh mesh;mesh.rows=2;mesh.columns=static_cast<int>(a.size());mesh.world={a,b};
    std::vector<double> parameters;std::vector<std::vector<Vector3>> uvRails(2);
    for(std::size_t i=0;i<a.size();++i){const double t=double(i)/(a.size()-1);parameters.push_back(t);uvRails[0].push_back({0,t,0});uvRails[1].push_back({1,t,0});}
    auto loops=source.Value().loops;
    for(auto& loop:loops)for(auto& uv:loop){
        const auto p=surface->Value(u0+(u1-u0)*uv.v,v0+(v1-v0)*uv.u);
        const auto mapped=fabrication::MapPointToBandState(mesh,uvRails,{p.X(),p.Y(),p.Z()});
        if(!mapped.HasValue()||mapped.Value().distanceMm>tolerance.modelLinearMm*5)return Failed();
        uv={mapped.Value().point.x,mapped.Value().point.y};
    }
    for(auto& loop:loops)fabrication::CompactMaterialLoop(loop);
    BRepBuilderAPI_Sewing sewing(std::max(1e-7,tolerance.modelLinearMm));
    for(std::size_t i=1;i<a.size();++i){
        const auto pieces=fabrication::ClipBandMaterial(loops,fabrication::BandSplitAxis::V,parameters[i-1],parameters[i]);
        if(!pieces.HasValue())return Out::Failure(pieces.Diagnostics());
        for(const auto& piece:pieces.Value()){
            std::vector<std::vector<Vector3>> lower,upper;
            for(const auto& loop:piece.loops){
                lower.push_back(fabrication::MapBandMaterialLoop(loop,fabrication::BandSplitAxis::U,0,1,parameters,{al,bl}));
                upper.push_back(fabrication::MapBandMaterialLoop(loop,fabrication::BandSplitAxis::U,0,1,parameters,{ah,bh}));
            }
            if(!AddMaterialFace(sewing,lower,-normals[i-1],tolerance.modelLinearMm)
                ||!AddMaterialFace(sewing,upper,normals[i-1],tolerance.modelLinearMm))return Failed();
        }
    }
    double uvArea=0;const auto& outer=loops.front();
    for(std::size_t i=0;i<outer.size();++i){const auto p=outer[i],q=outer[(i+1)%outer.size()];uvArea+=p.u*q.v-q.u*p.v;}
    // A returning bend can reverse its globally projected area. Winding in
    // the injective parameter chart remains consistent with every facet.
    const bool clockwise=(uvArea>0)!=(face.Orientation()==TopAbs_REVERSED);
    for(const auto& loop:loops){
        const auto low=fabrication::MapBandMaterialLoop(loop,fabrication::BandSplitAxis::U,0,1,parameters,{al,bl});
        const auto high=fabrication::MapBandMaterialLoop(loop,fabrication::BandSplitAxis::U,0,1,parameters,{ah,bh});
        if(low.size()!=high.size())return Failed();
        for(std::size_t i=0;i<low.size();++i){const auto j=(i+1)%low.size();
            if(geometry::Distance(low[i],low[j])<=tolerance.modelLinearMm)continue;
            if(clockwise?!AddSide(sewing,low[i],high[i],high[j],low[j]):!AddSide(sewing,low[i],low[j],high[j],high[i]))return Failed();
        }
    }
    sewing.Perform();const auto sewn=sewing.SewedShape();if(sewn.IsNull()||sewn.ShapeType()!=TopAbs_SHELL)return Failed();
    BRepBuilderAPI_MakeSolid made(TopoDS::Shell(sewn));if(!made.IsDone())return Failed();auto solid=made.Solid();
    if(!BRepLib::OrientClosedSolid(solid)||!BRepCheck_Analyzer(solid).IsValid())return Failed();return Out::Success(solid);
}
}
std::optional<base::Result<TopoDS_Shape>> ThickenFoldedBand(
    const TopoDS_Shape& shape,double thickness,fabrication::ThicknessPlacement placement,
    const geometry::GeometryTolerance& tolerance)
{
    TopExp_Explorer faces(shape,TopAbs_FACE);if(!faces.More())return std::nullopt;
    const auto face=TopoDS::Face(faces.Current());faces.Next();if(faces.More())return std::nullopt;
    const auto surface=occ::handle<Geom_BSplineSurface>::DownCast(BRep_Tool::Surface(face));
    if(surface.IsNull()||surface->UDegree()!=1||surface->VDegree()!=1
        ||surface->IsURational()||surface->IsVRational()||surface->NbVPoles()!=2)return std::nullopt;
    int wires=0;for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next())++wires;
    bool trimmed=wires!=1;
    double u0,u1,v0,v1,s0,s1,t0,t1;BRepTools::UVBounds(face,u0,u1,v0,v1);surface->Bounds(s0,s1,t0,t1);
    trimmed=trimmed||std::abs(u0-s0)>1e-10||std::abs(u1-s1)>1e-10||std::abs(v0-t0)>1e-10||std::abs(v1-t1)>1e-10;
    // Equal UV bounds do not imply a rectangular outer boundary: a C notch
    // can still reach all four extrema. Only proven isoparametric edges may
    // use the old whole-rectangle fast path.
    if(!trimmed)for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()){
        double first,last;auto curve=BRep_Tool::CurveOnSurface(TopoDS::Edge(e.Current()),face,first,last);
        while(const auto limited=occ::handle<Geom2d_TrimmedCurve>::DownCast(curve))curve=limited->BasisCurve();
        const auto onSide=[&](const std::vector<gp_Pnt2d>& points){
            for(int side=0;side<4;++side){const double bound=side==0?s0:side==1?s1:side==2?t0:t1;
                if(std::all_of(points.begin(),points.end(),[&](auto p){return std::abs((side<2?p.X():p.Y())-bound)<1e-10;}))return true;}
            return false;
        };
        bool rectangle=false;
        if(const auto line=occ::handle<Geom2d_Line>::DownCast(curve))rectangle=onSide({line->Value(first),line->Value(last)});
        else if(const auto spline=occ::handle<Geom2d_BSplineCurve>::DownCast(curve)){
            std::vector<gp_Pnt2d> poles;for(int i=1;i<=spline->NbPoles();++i)poles.push_back(spline->Pole(i));rectangle=onSide(poles);
        }
        if(!rectangle){trimmed=true;break;}
    }
    const int n=surface->NbUPoles();std::vector<Vector3> a,b,normals,offset(n);
    for(int i=1;i<=n;++i){const auto p=surface->Pole(i,1),q=surface->Pole(i,2);a.push_back({p.X(),p.Y(),p.Z()});b.push_back({q.X(),q.Y(),q.Z()});}
    bool nonPlanar=false;
    for(int i=0;i+1<n;++i) {
        auto normal=geometry::Cross(a[i+1]-a[i],b[i]-a[i]);
        if(normal.Length()<1e-10)return std::nullopt;normal=geometry::Normalized(normal);
        nonPlanar=nonPlanar||std::abs(geometry::Dot(b[i+1]-a[i],normal))>tolerance.modelLinearMm;
        if(face.Orientation()==TopAbs_REVERSED)normal=-normal;normals.push_back(normal);
    }
    if(nonPlanar) {
        for(int i=1;i+1<n;++i)for(double v:{0.0,.5,1.0}) {
            const auto across=b[i]-a[i];
            const auto left=geometry::Cross((a[i]-a[i-1])*(1-v)+(b[i]-b[i-1])*v,across);
            const auto right=geometry::Cross((a[i+1]-a[i])*(1-v)+(b[i+1]-b[i])*v,across);
            if(left.Length()<tolerance.numericEpsilon||right.Length()<tolerance.numericEpsilon)return Failed();
            const auto nl=geometry::Normalized(left),nr=geometry::Normalized(right);
            if(geometry::Cross(nl,nr).Length()>tolerance.modelAngularRad||geometry::Dot(nl,nr)<0)
                return Out::Failure(base::MakeError(kThickenFailed,
                    "ねじれた面が折れ目でつながる帯には、まだ正しい厚みを付けられません。",
                    "汎用オフセットでは折れ目の厚み面を接合できず、出力時に体積が変わることがあります。"
                    "帯を折れ目で分割して別部材にするか、平面部材へ近似してください。元の面は保持しています。"));
        }
        // A smooth bilinear patch (including redundant collinear stations) still
        // uses the general offset path. Only unsupported warped creases are refused.
        return std::nullopt;
    }
    offset.front()=normals.front();offset.back()=normals.back();
    for(int i=1;i+1<n;++i) {
        const double denominator=1+geometry::Dot(normals[i-1],normals[i]);
        if(denominator<1e-6)return Failed();
        // Intersection of the two offset facet planes: exact normal thickness
        // on both sides of the fold, not an averaged-normal displacement.
        offset[i]=(normals[i-1]+normals[i])*(1.0/denominator);
    }
    const double low=placement==fabrication::ThicknessPlacement::Inside?-thickness:
        placement==fabrication::ThicknessPlacement::Centered?-thickness*.5:0;
    std::vector<Vector3> al,bl,ah,bh;
    for(int i=0;i<n;++i){al.push_back(a[i]+offset[i]*low);bl.push_back(b[i]+offset[i]*low);
        ah.push_back(a[i]+offset[i]*(low+thickness));bh.push_back(b[i]+offset[i]*(low+thickness));}
    if(trimmed)return TrimmedBandSolid(face,surface,a,b,normals,al,bl,ah,bh,tolerance);
    BRepBuilderAPI_Sewing sewing(std::max(1e-7,tolerance.modelLinearMm));
    for(int i=0;i+1<n;++i) {
        if(!AddFace(sewing,{ah[i],ah[i+1],bh[i+1],bh[i]})
            ||!AddFace(sewing,{bl[i],bl[i+1],al[i+1],al[i]})
            ||!AddSide(sewing,al[i],al[i+1],ah[i+1],ah[i])
            ||!AddSide(sewing,bh[i],bh[i+1],bl[i+1],bl[i]))return Failed();
    }
    if(!AddSide(sewing,ah.front(),bh.front(),bl.front(),al.front())
        ||!AddSide(sewing,al.back(),bl.back(),bh.back(),ah.back()))return Failed();
    sewing.Perform();const auto sewn=sewing.SewedShape();
    if(sewn.IsNull()||sewn.ShapeType()!=TopAbs_SHELL)return Failed();
    BRepBuilderAPI_MakeSolid makeSolid(TopoDS::Shell(sewn));
    if(!makeSolid.IsDone())return Failed();auto solid=makeSolid.Solid();
    if(!BRepLib::OrientClosedSolid(solid)||!BRepCheck_Analyzer(solid).IsValid())return Failed();
    return Out::Success(solid);
}
}
#endif
