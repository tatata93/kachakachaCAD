#include "kachakacha/kernel/OcctFoldedBandThicken.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <Geom_BSplineSurface.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepLib.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopExp_Explorer.hxx>
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
    if(wires!=1)return std::nullopt; // Never fill a trimmed opening accidentally.
    double u0,u1,v0,v1,s0,s1,t0,t1;BRepTools::UVBounds(face,u0,u1,v0,v1);surface->Bounds(s0,s1,t0,t1);
    if(std::abs(u0-s0)>1e-10||std::abs(u1-s1)>1e-10||std::abs(v0-t0)>1e-10||std::abs(v1-t1)>1e-10)return std::nullopt;
    const int n=surface->NbUPoles();std::vector<Vector3> a,b,normals,offset(n);
    for(int i=1;i<=n;++i){const auto p=surface->Pole(i,1),q=surface->Pole(i,2);a.push_back({p.X(),p.Y(),p.Z()});b.push_back({q.X(),q.Y(),q.Z()});}
    for(int i=0;i+1<n;++i) {
        auto normal=geometry::Cross(a[i+1]-a[i],b[i]-a[i]);
        if(normal.Length()<1e-10)return std::nullopt;normal=geometry::Normalized(normal);
        if(std::abs(geometry::Dot(b[i+1]-a[i],normal))>tolerance.modelLinearMm)return std::nullopt;
        if(face.Orientation()==TopAbs_REVERSED)normal=-normal;normals.push_back(normal);
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
