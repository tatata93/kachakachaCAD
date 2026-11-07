#include "kachakacha/kernel/OcctFoldedBandOffset.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <BRepAlgoAPI_Check.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepLib.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Vec.hxx>
#include <gp_Pnt.hxx>
#include <algorithm>
#include <cmath>
#include <vector>
namespace kachakacha::v2::kernel::detail {
namespace {
using geometry::Vector3;
using Out=base::Result<TopoDS_Shape>;
Out Refused()
{
    return Out::Failure(base::MakeError(kSurfaceOffsetImpossible,
        "折れ目の両側で指定した距離を保って面をつなげられません。",
        "距離を小さくするか、急な折れ目で分けてください。元の面は変更していません。"));
}
Out TransferTrims(const TopoDS_Face& source,const occ::handle<Geom_BSplineSurface>& surface,
    const geometry::GeometryTolerance& tolerance)
{
    const auto forward=TopoDS::Face(source.Oriented(TopAbs_FORWARD));
    TopoDS_Face made;BRep_Builder builder;builder.MakeFace(made,surface,tolerance.modelLinearMm);
    int count=0;
    for(TopExp_Explorer wires(forward,TopAbs_WIRE);wires.More();wires.Next()){
        BRepBuilderAPI_MakeWire wire;
        for(BRepTools_WireExplorer edges(TopoDS::Wire(wires.Current()),forward);edges.More();edges.Next()){
            const auto edge=edges.Current();double first=0,last=0;
            const auto curve=BRep_Tool::CurveOnSurface(edge,forward,first,last);
            if(curve.IsNull())return Refused();
            const auto copy=occ::handle<Geom2d_Curve>::DownCast(curve->Copy());
            BRepBuilderAPI_MakeEdge moved(copy,surface,first,last);
            if(!moved.IsDone())return Refused();
            auto next=moved.Edge();next.Orientation(edge.Orientation());wire.Add(next);
        }
        if(!wire.IsDone())return Refused();builder.Add(made,wire.Wire());++count;
    }
    if(count==0||!BRepLib::BuildCurves3d(made,tolerance.modelLinearMm))return Refused();
    BRepLib::SameParameter(made,tolerance.modelLinearMm,true);
    // These are explicit material wires. Never let a later consumer replace
    // holes or an edge notch with the support's rectangular parameter box.
    builder.NaturalRestriction(made,false);
    made.Orientation(source.Orientation());
    if(!BRepCheck_Analyzer(made).IsValid())return Refused();
    // Topological validity alone does not reject an offset that folds through
    // another part of the same sheet. Do not reject merely short valid edges.
    BRepAlgoAPI_Check checked(made,false,true);
    if(!checked.IsValid())return Refused();
    return Out::Success(made);
}
}
std::optional<Out> OffsetFoldedBand(const TopoDS_Shape& source,double distance,
    const geometry::GeometryTolerance& tolerance)
{
    TopExp_Explorer faces(source,TopAbs_FACE);if(!faces.More())return std::nullopt;
    const auto face=TopoDS::Face(faces.Current());faces.Next();if(faces.More())return std::nullopt;
    const auto surface=occ::handle<Geom_BSplineSurface>::DownCast(BRep_Tool::Surface(face));
    if(surface.IsNull()||surface->UDegree()!=1||surface->VDegree()!=1
        ||surface->IsURational()||surface->IsVRational()||surface->NbVPoles()!=2
        ||surface->IsUPeriodic()||surface->IsVPeriodic())return std::nullopt;
    if(!std::isfinite(distance)||!BRepCheck_Analyzer(face).IsValid())return Refused();
    const int size=surface->NbUPoles();if(size<3)return std::nullopt;
    std::vector<Vector3> a,b,normals,offset(size);
    for(int i=1;i<=size;++i){
        const auto p=surface->Pole(i,1),q=surface->Pole(i,2);
        a.push_back({p.X(),p.Y(),p.Z()});b.push_back({q.X(),q.Y(),q.Z()});
    }
    for(int i=0;i+1<size;++i){
        auto normal=geometry::Cross(a[i+1]-a[i],b[i]-a[i]);
        if(!normal.IsFinite()||!std::isfinite(normal.Length())||normal.Length()<=tolerance.numericEpsilon)return Refused();
        normal=geometry::Normalized(normal);
        // A warped bilinear cell needs a varying normal, not this miter rule.
        if(std::abs(geometry::Dot(b[i+1]-a[i],normal))>tolerance.modelLinearMm*.01)
            return std::nullopt;
        if(face.Orientation()==TopAbs_REVERSED)normal=-normal;normals.push_back(normal);
    }
    offset.front()=normals.front();offset.back()=normals.back();
    for(int i=1;i+1<size;++i){
        const double denominator=1+geometry::Dot(normals[i-1],normals[i]);
        if(denominator<1e-6)return Refused();
        // Intersection of both offset planes, matching folded-band thickness.
        offset[i]=(normals[i-1]+normals[i])*(1./denominator);
    }
    const auto shifted=occ::handle<Geom_BSplineSurface>::DownCast(surface->Copy());
    std::vector<Vector3> movedA,movedB;
    for(int i=0;i<size;++i){
        movedA.push_back(a[i]+offset[i]*distance);movedB.push_back(b[i]+offset[i]*distance);
        if(!movedA.back().IsFinite()||!movedB.back().IsFinite())return Refused();
    }
    for(int i=0;i+1<size;++i){
        auto normal=geometry::Cross(movedA[i+1]-movedA[i],movedB[i]-movedA[i]);
        if(face.Orientation()==TopAbs_REVERSED)normal=-normal;
        if(!normal.IsFinite()||!std::isfinite(normal.Length())||normal.Length()<=tolerance.numericEpsilon
            ||geometry::Dot(normal,normals[i])<=0)return Refused();
    }
    for(int i=0;i<size;++i)for(int j=1;j<=2;++j){const auto& point=j==1?movedA[i]:movedB[i];
        shifted->SetPole(i+1,j,gp_Pnt(point.x,point.y,point.z));}
    return TransferTrims(face,shifted,tolerance);
}
}
#endif
