#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include <sstream>
#include <cstdint>
#include <cstring>
#include <limits>
#include <algorithm>
#include <cmath>
#include <array>
#include <iomanip>
#include <map>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctBoundaryCurves.h"
#include <BRepTools.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <TopoDS_Vertex.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <ShapeUpgrade_ShapeDivideContinuity.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <STEPControl_Writer.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <BRepGProp.hxx>
#include "kachakacha/kernel/OcctMassProperties.h"
#include <GProp_GProps.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#endif
namespace kachakacha::v2::kernel {
using namespace geometry;
using modeling::KernelShapeHandle;
using base::Result;
Result<std::vector<std::vector<CurveSegment>>> OutputBoundaryWires(KernelShapeHandle handle, double tolerance) {
    using Out = Result<std::vector<std::vector<CurveSegment>>>;
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        TopoDS_Shape shape;
        if (!LookupShape(handle, shape) || shape.IsNull())
            return Out::Failure(base::MakeError("EXP-P001", "出力する実形状がありません。", {}));
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(shape, TopAbs_EDGE, edges);
        std::vector<std::vector<CurveSegment>> curves;
        double bound=0,squared=0;std::size_t samples=0;
        for (int i = 1; i <= edges.Extent(); ++i) {
            const auto edge = TopoDS::Edge(edges(i));
            if (BRep_Tool::Degenerated(edge)) continue;
            const auto curve = detail::BoundaryCurves(edge, tolerance);
            if (!curve.HasValue()) return Out::Failure(curve.Diagnostics());
            curves.push_back(curve.Value().curves);
            bound=std::max(bound,curve.Value().maxDeviationBoundMm);
            squared+=curve.Value().squaredDeviationMm;samples+=curve.Value().samples;
        }
        if (!curves.empty()) {
            std::vector<base::Diagnostic> diagnostics;
            if(samples) {
                std::ostringstream text;text<<std::setprecision(6)<<"境界ワイヤー：位置偏差の上限 "<<bound
                    <<" mm / RMS（標本） "<<std::sqrt(squared/samples)<<" mm";
                diagnostics.push_back(base::MakeInformation("EXP-I002",text.str()));
            }
            return Out::Success(std::move(curves),std::move(diagnostics));
        }
    } catch (...) {}
#else
    (void)handle; (void)tolerance;
#endif
    return Out::Failure(base::MakeError("EXP-P001", "全ての辺をワイヤーに変換できません。", {}));
}
Result<std::vector<CurveSegment>> OutputEdges(KernelShapeHandle handle,double tolerance) {
    using Out=Result<std::vector<CurveSegment>>;
    const auto wires=OutputBoundaryWires(handle,tolerance);if(!wires.HasValue())return Out::Failure(wires.Diagnostics());
    std::vector<CurveSegment> curves;for(const auto& wire:wires.Value())curves.insert(curves.end(),wire.begin(),wire.end());
    return Out::Success(std::move(curves),wires.Diagnostics());
}
namespace {
template<class T> Result<T> Failed() {return Result<T>::Failure(base::MakeError("EXP-P001","出力形状を作れません。","入力形状または配置基準を確認してください。"));}
using StlTriangle=std::array<Vector3,3>;
bool Collapsed(const StlTriangle& t) {
    const double scale=std::max((t[1]-t[0]).LengthSquared(),(t[2]-t[0]).LengthSquared());
    return Cross(t[1]-t[0],t[2]-t[0]).LengthSquared()<=1e-24*scale*scale;
}
std::vector<StlTriangle> StlTriangles(const modeling::ShapeMesh& mesh) {
    std::vector<StlTriangle> result,splits;
    const auto rounded=[](Vector3 p){return Vector3{static_cast<float>(p.x),static_cast<float>(p.y),static_cast<float>(p.z)};};
    for(const auto& t:mesh.triangles) {
        StlTriangle p{rounded(t.points[0]),rounded(t.points[1]),rounded(t.points[2])};
        if(!Collapsed(p)){result.push_back(p);continue;}
        if(p[0]==p[1]||p[1]==p[2]||p[2]==p[0])continue;
        std::size_t longest=0;
        for(std::size_t i=1;i<3;++i)
            if(Distance(p[i],p[(i+1)%3])>Distance(p[longest],p[(longest+1)%3]))longest=i;
        splits.push_back({p[longest],p[(longest+1)%3],p[(longest+2)%3]});
    }
    // A collinear triangle A-M-B can bridge an edge A-B to A-M/M-B.
    // Removing it alone creates a T-junction. Split its neighbour at the existing
    // point M, retaining orientation and geometry without filling any opening.
    for(const auto& split:splits) {
        const auto count=result.size();
        for(std::size_t index=0;index<count;++index) {
            const auto t=result[index];
            for(std::size_t i=0;i<3;++i) {
                const auto a=t[i],b=t[(i+1)%3],c=t[(i+2)%3];
                if(!((a==split[0]&&b==split[1])||(a==split[1]&&b==split[0])))continue;
                result[index]={a,split[2],c};result.push_back({split[2],b,c});break;
            }
        }
    }
    return result;
}
std::string ClosedStlProblem(const std::vector<StlTriangle>& triangles) {
    // Check the exact float coordinates that will be serialized, per input part.
    // B-Rep validity alone can miss a boundary pinched along a coincident edge.
    std::map<std::array<double,3>,std::size_t> vertices;
    struct Use {int count=0;int orientation=0;};
    std::map<std::array<std::size_t,2>,Use> edges;
    double volume=0;
    for(const auto& triangle:triangles) {
        std::array<std::size_t,3> ids;
        for(std::size_t i=0;i<3;++i) {
            const auto p=triangle[i];
            if(!p.IsFinite())return "non-finite coordinate";
            ids[i]=vertices.emplace(std::array{p.x,p.y,p.z},vertices.size()).first->second;
        }
        volume+=Dot(triangle[0],Cross(triangle[1],triangle[2]))/6.0;
        for(std::size_t i=0;i<3;++i) {
            const auto a=ids[i],b=ids[(i+1)%3];
            if(a==b)return "collapsed edge";
            auto& use=edges[std::array{std::min(a,b),std::max(a,b)}];
            ++use.count;use.orientation+=a<b?1:-1;
        }
    }
    std::size_t open=0,nonmanifold=0,orientation=0;
    for(const auto& [edge,use]:edges) {
        if(use.count==1)++open;
        else if(use.count!=2)++nonmanifold;
        else if(use.orientation!=0)++orientation;
    }
    if(open||nonmanifold||orientation||!(volume>0))
        return "open="+std::to_string(open)+" nonmanifold="+std::to_string(nonmanifold)
            +" winding="+std::to_string(orientation)+" volume="+std::to_string(volume);
    return {};
}
#ifdef KACHACAD_V2_WITH_OCCT
struct StepSolidFacts {
    int count=0;
    double volume=0;
    Bnd_Box bounds;
    bool valid=true;
    void Add(const TopoDS_Shape& shape) {
        for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next()) {
            ++count;GProp_GProps props;detail::VolumeProperties(e.Current(),props);
            volume+=props.Mass();valid=valid&&props.Mass()>0&&BRepCheck_Analyzer(e.Current()).IsValid();
            BRepBndLib::AddOptimal(e.Current(),bounds,false,false);
        }
    }
};
bool StepRetainsSolids(const std::string& content,const StepSolidFacts& source,std::string& reason) {
    try {
    if(!source.count)return true; // Explicit surface/curve exports remain supported.
    STEPControl_Reader reader;std::istringstream in(content);
    if(reader.ReadStream("output-validation.step",in)!=IFSelect_RetDone||!reader.TransferRoots())return false;
    StepSolidFacts restored;restored.Add(reader.OneShape());
    reason="valid="+std::to_string(restored.valid)+" solids="+std::to_string(source.count)+"/"+std::to_string(restored.count)
        +" volume="+std::to_string(source.volume)+"/"+std::to_string(restored.volume);
    if(!restored.valid||restored.count!=source.count
        ||std::abs(restored.volume-source.volume)>std::max(.001,source.volume*1e-5))return false;
    double a[6],b[6];source.bounds.Get(a[0],a[1],a[2],a[3],a[4],a[5]);
    restored.bounds.Get(b[0],b[1],b[2],b[3],b[4],b[5]);
    for(int i=0;i<6;++i)if(std::abs(a[i]-b[i])>1e-4){reason+=" bound["+std::to_string(i)+"]="+std::to_string(a[i])+"/"+std::to_string(b[i]);return false;}
    return true;
    }catch(const Standard_Failure& failure){reason=failure.GetMessageString();return false;}
}

std::string StepText(const std::vector<TopoDS_Shape>& shapes,bool divide) {
    STEPControl_Writer writer;
    for(auto shape:shapes) {
        bool composite=false;
        if(divide)for(TopExp_Explorer e(shape,TopAbs_FACE);e.More()&&!composite;e.Next()) {
            BRepAdaptor_Surface surface(TopoDS::Face(e.Current()));
            composite=surface.NbUIntervals(GeomAbs_C3)>1||surface.NbVIntervals(GeomAbs_C3)>1;
        }
        if(composite) {
            // Exact subdivision of an export copy preserves spline continuity
            // spans explicitly so STEP read-back can integrate them accurately.
            BRepBuilderAPI_Copy copy(shape,true,false);
            ShapeUpgrade_ShapeDivideContinuity split(copy.Shape());
            split.SetSurfaceCriterion(GeomAbs_C3);split.SetBoundaryCriterion(GeomAbs_C3);
            split.SetPCurveCriterion(GeomAbs_C3);split.SetTolerance(1e-7);split.Perform();
            if(split.Result().IsNull()||!BRepCheck_Analyzer(split.Result()).IsValid())return {};
            shape=split.Result();
        }
        if(writer.Transfer(shape,STEPControl_AsIs)!=IFSelect_RetDone)return {};
    }
    std::ostringstream out;if(writer.WriteStream(out)!=IFSelect_RetDone)return {};
    return out.str();
}
gp_Trsf Placement(const OutputPlacement& placement) {
    gp_Trsf trsf;
    if(!placement.keepPosition) {
        const auto frame=[](const OutputFrame& f){return gp_Ax3(ToPoint(f.origin),gp_Dir(ToVector(f.normal)),gp_Dir(ToVector(f.xDirection)));};
        trsf.SetDisplacement(frame(placement.source),frame(placement.destination));
    }
    return trsf;
}
#endif
}
Result<std::string> CaptureOutputShape(KernelShapeHandle handle) {
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        TopoDS_Shape shape;if(!LookupShape(handle,shape)||shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())return Failed<std::string>();
        std::ostringstream out;BRepTools::Write(shape,out);return Result<std::string>::Success(out.str());
    }catch(...){return Failed<std::string>();}
#else
    (void)handle;return Failed<std::string>();
#endif
}
Result<KernelShapeHandle> RestoreOutputShape(const std::string& brep) {
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        std::istringstream in(brep);TopoDS_Shape shape;BRep_Builder builder;BRepTools::Read(shape,in,builder);
        if(shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())return Failed<KernelShapeHandle>();
        return Result<KernelShapeHandle>::Success(StoreShape(shape));
    }catch(...){return Failed<KernelShapeHandle>();}
#else
    (void)brep;return Failed<KernelShapeHandle>();
#endif
}
Result<KernelShapeHandle> PlaceOutputShape(KernelShapeHandle handle,const OutputPlacement& placement) {
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        if(!placement.Valid())return Failed<KernelShapeHandle>();
        TopoDS_Shape shape;if(!LookupShape(handle,shape))return Failed<KernelShapeHandle>();
        if(placement.keepPosition)return Result<KernelShapeHandle>::Success(handle);
        BRepBuilderAPI_Transform moved(shape,Placement(placement),true);
        if(!moved.IsDone()||!BRepCheck_Analyzer(moved.Shape()).IsValid())return Failed<KernelShapeHandle>();
        return Result<KernelShapeHandle>::Success(StoreShape(moved.Shape()));
    }catch(...){return Failed<KernelShapeHandle>();}
#else
    (void)handle;(void)placement;return Failed<KernelShapeHandle>();
#endif
}
Result<std::vector<CurveSegment>> PlaceOutputCurves(const std::vector<CurveSegment>& curves,const OutputPlacement& placement,double tolerance) {
    using Curves=std::vector<CurveSegment>;
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        if(!placement.Valid())return Failed<Curves>();
        if(placement.keepPosition)return Result<Curves>::Success(curves);
        Curves result;
        for(const auto& curve:curves) {
            const auto edge=ToEdge(curve);if(!edge.HasValue())return Result<Curves>::Failure(edge.Diagnostics());
            BRepBuilderAPI_Transform moved(edge.Value(),Placement(placement),true);
            if(!moved.IsDone())return Failed<Curves>();
            const auto value=FromEdge(TopoDS::Edge(moved.Shape()),tolerance);
            if(!value.HasValue())return Result<Curves>::Failure(value.Diagnostics());
            result.push_back(value.Value());
        }
        return Result<Curves>::Success(std::move(result));
    }catch(...){return Failed<Curves>();}
#else
    (void)curves;(void)placement;(void)tolerance;return Failed<Curves>();
#endif
}
Result<KernelShapeHandle> OutputCurveShape(const std::vector<CurveSegment>& curves) {
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        if(curves.empty())return Failed<KernelShapeHandle>();
        BRep_Builder builder;TopoDS_Compound shape;builder.MakeCompound(shape);
        for(const auto& curve:curves){const auto edge=ToEdge(curve);if(!edge.HasValue())return Result<KernelShapeHandle>::Failure(edge.Diagnostics());builder.Add(shape,edge.Value());}
        return Result<KernelShapeHandle>::Success(StoreShape(shape));
    }catch(...){return Failed<KernelShapeHandle>();}
#else
    (void)curves;return Failed<KernelShapeHandle>();
#endif
}
Result<OutputFrame> OutputSurfaceFrame(KernelShapeHandle handle,const Vector3& point) {
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        TopoDS_Shape shape;if(!LookupShape(handle,shape)||!point.IsFinite())return Failed<OutputFrame>();
        OutputFrame result;double nearest=std::numeric_limits<double>::max();
        for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()) {
            const auto face=TopoDS::Face(e.Current());const auto surface=BRep_Tool::Surface(face);
            GeomAPI_ProjectPointOnSurf project(ToPoint(point),surface);
            if(project.NbPoints()==0||project.LowerDistance()>=nearest)continue;
            double u=0,v=0;project.LowerDistanceParameters(u,v);gp_Pnt p;gp_Vec du,dv;surface->D1(u,v,p,du,dv);
            auto normal=du.Crossed(dv);if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
            OutputFrame candidate{FromPoint(p),{normal.X(),normal.Y(),normal.Z()},{du.X(),du.Y(),du.Z()}};
            if(!candidate.Valid())continue;
            result=candidate;nearest=project.LowerDistance();
        }
        return nearest<std::numeric_limits<double>::max() ? Result<OutputFrame>::Success(result):Failed<OutputFrame>();
    }catch(...){return Failed<OutputFrame>();}
#else
    (void)handle;(void)point;return Failed<OutputFrame>();
#endif
}
Result<KernelShapeHandle> OutputFace(KernelShapeHandle handle,std::size_t faceIndex) {
#ifdef KACHACAD_V2_WITH_OCCT
    try {TopoDS_Shape shape;if(!LookupShape(handle,shape))return Failed<KernelShapeHandle>();
        std::size_t at=0;for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next(),++at)if(at==faceIndex)return Result<KernelShapeHandle>::Success(StoreShape(e.Current()));
    }catch(...){}
#else
    (void)handle;(void)faceIndex;
#endif
    return Failed<KernelShapeHandle>();
}
Result<CurveSegment> OutputEdge(KernelShapeHandle handle,const Vector3& point) {
#ifdef KACHACAD_V2_WITH_OCCT
    try {TopoDS_Shape shape;if(!LookupShape(handle,shape))return Failed<CurveSegment>();
        const auto vertex=BRepBuilderAPI_MakeVertex(ToPoint(point)).Vertex();TopoDS_Edge chosen;double nearest=std::numeric_limits<double>::max();
        for(TopExp_Explorer e(shape,TopAbs_EDGE);e.More();e.Next()){BRepExtrema_DistShapeShape distance(vertex,e.Current());
            if(distance.IsDone()&&distance.Value()<nearest){nearest=distance.Value();chosen=TopoDS::Edge(e.Current());}}
        if(!chosen.IsNull())return FromEdge(chosen,1e-7);
    }catch(...){}
#else
    (void)handle;(void)point;
#endif
    return Failed<CurveSegment>();
}
Result<std::string> OutputStep(const std::vector<KernelShapeHandle>& handles) {
#ifdef KACHACAD_V2_WITH_OCCT
    std::string stage="元の形の検証";
    try {
        if(handles.empty())return Failed<std::string>();
        StepSolidFacts source;std::vector<TopoDS_Shape> shapes;
        for(const auto handle:handles){TopoDS_Shape shape;if(!LookupShape(handle,shape)||shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())return Failed<std::string>();
            if(TopExp_Explorer(shape,TopAbs_SOLID).More()) {
                const auto check=CheckSolidForExport(handle,1e-6);
                if(!check.HasValue()||!check.Value().Ok())return Result<std::string>::Failure(base::MakeError("EXP-P001","不正な立体は書き出せません。","閉鎖性・内外の向き・自己交差を確認してください。"));
            }
            source.Add(shape);shapes.push_back(shape);}
        auto content=StepText(shapes,false);if(content.empty())return Failed<std::string>();
        stage="STEPの再読込検証";
        std::string reason;
        if(!StepRetainsSolids(content,source,reason)) {
            content=StepText(shapes,true);
            if(content.empty()||!StepRetainsSolids(content,source,reason))return Result<std::string>::Failure(base::MakeError(
            "EXP-P003","STEPを再読込すると立体の形または閉鎖性が変わります。",
            "このSTEPは保存しません。元のKCDを保ち、形状を整理するかSTLを選んでください。 "+reason));
        }
        return Result<std::string>::Success(std::move(content));
    }catch(const Standard_Failure& failure){return Result<std::string>::Failure(base::MakeError(
        "EXP-P003","STEPの形状を確認できなかったため、保存しませんでした。",stage+": "+failure.GetMessageString()));}
    catch(const std::exception& failure){return Result<std::string>::Failure(base::MakeError(
        "EXP-P003","STEPの形状を確認できなかったため、保存しませんでした。",stage+": "+failure.what()));}
    catch(...){return Failed<std::string>();}
#else
    (void)handles;return Failed<std::string>();
#endif
}
Result<std::string> OutputStl(const std::vector<KernelShapeHandle>& handles,double deflection) {
    if(handles.empty())return Failed<std::string>();
    std::string bytes(84,'\0');std::uint32_t count=0;
    const auto append=[&](float f){std::uint32_t bits;std::memcpy(&bits,&f,4);for(int i=0;i<4;++i)bytes.push_back(static_cast<char>(bits>>(8*i)));};
    std::size_t outputIndex=0;
    for(const auto handle:handles) {
        ++outputIndex;
        bool requireClosed=false;
#ifdef KACHACAD_V2_WITH_OCCT
        TopoDS_Shape shape;
        if(!LookupShape(handle,shape))return Failed<std::string>();
        requireClosed=TopExp_Explorer(shape,TopAbs_SOLID).More();
        if(requireClosed) {
            const auto check=CheckSolidForExport(handle,1e-6);
            if(!check.HasValue()||!check.Value().Ok())return Result<std::string>::Failure(base::MakeError("EXP-P001",
                "不正な立体はSTLにできません。対象 " + std::to_string(outputIndex)
                + (check.HasValue() ? " / volume="+std::to_string(check.Value().volumeMm3)
                    + " / closed="+std::to_string(check.Value().closed)
                    + " / intersectionFree="+std::to_string(check.Value().selfIntersectionFree) : ""),
                "閉鎖性・内外の向き・自己交差を確認してください。"));
        }
#endif
        const auto mesh=BuildShapeMesh(handle,deflection);
        if(!mesh.HasValue())return Result<std::string>::Failure(mesh.Diagnostics());
        if(mesh.Value().triangles.empty())return Result<std::string>::Failure(base::MakeError("EXP-P002","線や点はSTLにできません。","STEPかKCDを選ぶか、対象を外してください。"));
        const auto triangles=StlTriangles(mesh.Value());
        const auto problem=requireClosed?ClosedStlProblem(triangles):std::string();
        if(!problem.empty())return Result<std::string>::Failure(base::MakeError("EXP-P001",
            "STLの三角形が閉じた立体を表していません。対象 "+std::to_string(outputIndex),
            "辺だけで接する部品や接合方向を確認してください。 / "+problem));
        for(const auto& t:triangles){
            const auto n=Normalized(Cross(t[1]-t[0],t[2]-t[0]),0.0);
            for(const auto p:{n,t[0],t[1],t[2]}){append(static_cast<float>(p.x));append(static_cast<float>(p.y));append(static_cast<float>(p.z));}
            bytes.append(2,'\0');++count;}
    }
    if(count==0)return Failed<std::string>();
    for(int i=0;i<4;++i)bytes[80+i]=static_cast<char>(count>>(8*i));
    return Result<std::string>::Success(std::move(bytes));
}
}
