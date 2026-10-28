#include "kachakacha/kernel/OcctPlanarSheetThicken.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepLib.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Pln.hxx>
#include <exception>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
namespace kachakacha::v2::kernel::detail {
namespace {
using V=geometry::Vector3;using Out=base::Result<TopoDS_Shape>;
V Point(const TopoDS_Vertex& vertex){const auto p=BRep_Tool::Pnt(vertex);return {p.X(),p.Y(),p.Z()};}
V Normal(const TopoDS_Face& face){const auto n=BRepAdaptor_Surface(face).Plane().Axis().Direction();
    const V v{n.X(),n.Y(),n.Z()};return face.Orientation()==TopAbs_REVERSED?-v:v;}
Out Failed(const std::string& stage={}){return Out::Failure(base::MakeError(kThickenFailed,
    "近似面の厚みを折れ目で接合できません。",
    "オフセット面の交点と閉殻を検査しました。鋭い折れ・自己交差・非多様体の接続を確認してください。元の面は保持します。 "+stage));}
bool AddFace(BRepBuilderAPI_Sewing& sewing,const std::vector<std::vector<V>>& loops,V normal){
    std::vector<TopoDS_Wire> wires;
    for(std::size_t l=0;l<loops.size();++l){const auto& points=loops[l];if(points.size()<3)return false;
        BRepBuilderAPI_MakePolygon polygon;V area{};
        for(std::size_t i=0;i<points.size();++i){const auto p=points[i];polygon.Add(gp_Pnt(p.x,p.y,p.z));area=area+geometry::Cross(p,points[(i+1)%points.size()]);}
        polygon.Close();if(!polygon.IsDone())return false;auto wire=polygon.Wire();
        if((geometry::Dot(area,normal)>0)!=(l==0))wire.Reverse();wires.push_back(wire);
    }
    // A boundary wall can be oblique to the source normal at a miter.
    // The supporting plane comes from its actual vertices, not the desired outward direction.
    const auto p=loops.front().front();V actual{};
    for(std::size_t i=1;i+1<loops.front().size();++i)actual=actual+geometry::Cross(loops.front()[i]-p,loops.front()[i+1]-p);
    if(actual.LengthSquared()<1e-24)return false;if(geometry::Dot(actual,normal)<0)actual=-actual;
    actual=geometry::Normalized(actual);
    BRepBuilderAPI_MakeFace face(gp_Pln(gp_Pnt(p.x,p.y,p.z),gp_Dir(actual.x,actual.y,actual.z)),wires.front(),true);
    for(std::size_t l=1;l<wires.size();++l)face.Add(wires[l]);if(!face.IsDone())return false;
    sewing.Add(face.Face());return true;
}
bool Miter(const std::vector<V>& normals,double thickness,double tolerance,V& offset){
    if(normals.empty())return false;offset=normals.front();double pairSize=0,tripleSize=0;
    std::size_t ai=0,bi=0,ci=0;
    for(std::size_t i=0;i<normals.size();++i)for(std::size_t j=i+1;j<normals.size();++j){
        const double size=geometry::Cross(normals[i],normals[j]).Length();if(size>pairSize){pairSize=size;ai=i;bi=j;}
    }
    if(pairSize>1e-9){const double d=1+geometry::Dot(normals[ai],normals[bi]);if(d<1e-8)return false;offset=(normals[ai]+normals[bi])*(1/d);}
    std::size_t ta=0,tb=0;
    for(std::size_t i=0;i<normals.size();++i)for(std::size_t j=i+1;j<normals.size();++j)for(std::size_t k=j+1;k<normals.size();++k){
        const double size=std::abs(geometry::Dot(normals[i],geometry::Cross(normals[j],normals[k])));
        if(size>tripleSize){tripleSize=size;ta=i;tb=j;ci=k;}
    }
    if(tripleSize>1e-10){const auto a=normals[ta],b=normals[tb],c=normals[ci];
        offset=(geometry::Cross(b,c)+geometry::Cross(c,a)+geometry::Cross(a,b))*(1/geometry::Dot(a,geometry::Cross(b,c)));}
    if(!std::isfinite(offset.LengthSquared())||offset.LengthSquared()>1e12)return false;
    for(auto n:normals)if(std::abs(geometry::Dot(n,offset)-1)*thickness>tolerance)return false;
    return true;
}
}
std::optional<base::Result<TopoDS_Shape>> ThickenPlanarSheet(const TopoDS_Shape& shape,double thickness,
    fabrication::ThicknessPlacement placement,const geometry::GeometryTolerance& tolerance){
    try{
        int faces=0;for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next()){
            if(BRepAdaptor_Surface(TopoDS::Face(f.Current())).GetType()!=GeomAbs_Plane)return std::nullopt;++faces;
        }
        if(faces<2)return std::nullopt;
        for(TopExp_Explorer e(shape,TopAbs_EDGE);e.More();e.Next())if(BRepAdaptor_Curve(TopoDS::Edge(e.Current())).GetType()!=GeomAbs_Line)return std::nullopt;
        TopTools_IndexedDataMapOfShapeListOfShape adjacent,edgeFaces;
        TopExp::MapShapesAndAncestors(shape,TopAbs_VERTEX,TopAbs_FACE,adjacent);
        TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,edgeFaces);
        const double low=placement==fabrication::ThicknessPlacement::Inside?-thickness:placement==fabrication::ThicknessPlacement::Centered?-thickness*.5:0;
        std::vector<V> offsets(adjacent.Extent());
        for(int i=1;i<=adjacent.Extent();++i){std::vector<V> normals;
            for(const auto& face:adjacent.FindFromIndex(i)){const auto n=Normal(TopoDS::Face(face));
                if(std::none_of(normals.begin(),normals.end(),[&](auto other){return (n-other).Length()<1e-10;}))normals.push_back(n);}
            if(!Miter(normals,thickness,tolerance.modelLinearMm*.1,offsets[i-1]))return Failed("頂点 "+std::to_string(i)+" / 隣接法線 "+std::to_string(normals.size()));
        }
        const auto moved=[&](const TopoDS_Vertex& vertex,double amount){const auto i=adjacent.FindIndex(vertex);return Point(vertex)+offsets[i-1]*amount;};
        BRepBuilderAPI_Sewing sewing(tolerance.modelLinearMm);
        for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next()){
            auto face=TopoDS::Face(f.Current());const auto n=Normal(face);const bool reversed=face.Orientation()==TopAbs_REVERSED;face.Orientation(TopAbs_FORWARD);
            std::vector<TopoDS_Wire> wires{BRepTools::OuterWire(face)};
            for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next())if(!w.Current().IsSame(wires.front()))wires.push_back(TopoDS::Wire(w.Current()));
            std::vector<std::vector<V>> lower,upper;
            for(const auto& wire:wires){lower.emplace_back();upper.emplace_back();
                for(BRepTools_WireExplorer e(wire,face);e.More();e.Next()){
                    const auto vertex=e.CurrentVertex();lower.back().push_back(moved(vertex,low));upper.back().push_back(moved(vertex,low+thickness));
                    const auto& owners=edgeFaces.FindFromKey(e.Current());TopTools_IndexedMapOfShape unique;for(const auto& owner:owners)unique.Add(owner);
                    if(unique.Extent()>2)return Failed("辺に3面以上");
                    if(unique.Extent()==1){TopoDS_Vertex a,b;TopExp::Vertices(e.Current(),a,b,true);if(reversed)std::swap(a,b);
                        const auto al=moved(a,low),bl=moved(b,low),ah=moved(a,low+thickness),bh=moved(b,low+thickness);
                        const auto outward=geometry::Cross(Point(b)-Point(a),n);
                        if(!AddFace(sewing,{{al,bl,bh}},outward)||!AddFace(sewing,{{al,bh,ah}},outward))return Failed("境界側壁");
                    }
                }
            }
            if(!AddFace(sewing,lower,-n)||!AddFace(sewing,upper,n))return Failed("表裏の面");
        }
        sewing.Perform();const auto result=sewing.SewedShape();if(result.IsNull()||result.ShapeType()!=TopAbs_SHELL)return Failed("縫合後が単一シェルではありません");
        BRepBuilderAPI_MakeSolid made(TopoDS::Shell(result));if(!made.IsDone())return Failed("ソリッド化");auto solid=made.Solid();
        if(!BRepLib::OrientClosedSolid(solid))return Failed("閉鎖性検査");
        const BRepCheck_Analyzer check(solid);
        if(!check.IsValid()){
            std::string details="位相検査";int count=0;
            for(auto kind:{TopAbs_WIRE,TopAbs_FACE,TopAbs_EDGE})for(TopExp_Explorer item(solid,kind);item.More()&&count<8;item.Next()){
                const auto result=check.Result(item.Current());
                if(!result.IsNull())for(const auto status:result->Status())if(status!=BRepCheck_NoError){details+=" "+std::to_string(int(kind))+":"+std::to_string(int(status));
                    ++count;}
            }
            return Failed(details);
        }
        return Out::Success(solid);
    }catch(const Standard_Failure&){return Failed();}catch(const std::exception&){return Failed();}
}
}
#endif
