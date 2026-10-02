#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include <sstream>
#include <cstdint>
#include <cstring>
#include <limits>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <STEPControl_Writer.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <TopExp_Explorer.hxx>
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
namespace {
template<class T> Result<T> Failed() {return Result<T>::Failure(base::MakeError("EXP-P001","出力形状を作れません。","入力形状または配置基準を確認してください。"));}
#ifdef KACHACAD_V2_WITH_OCCT
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
Result<std::string> OutputStep(const std::vector<KernelShapeHandle>& handles) {
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        if(handles.empty())return Failed<std::string>();
        STEPControl_Writer writer;
        for(const auto handle:handles){TopoDS_Shape shape;if(!LookupShape(handle,shape)||shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())return Failed<std::string>();
            if(writer.Transfer(shape,STEPControl_AsIs)!=IFSelect_RetDone)return Failed<std::string>();}
        std::ostringstream out;if(writer.WriteStream(out)!=IFSelect_RetDone)return Failed<std::string>();
        return Result<std::string>::Success(out.str());
    }catch(...){return Failed<std::string>();}
#else
    (void)handles;return Failed<std::string>();
#endif
}
Result<std::string> OutputStl(const std::vector<KernelShapeHandle>& handles,double deflection) {
    if(handles.empty())return Failed<std::string>();
    std::string bytes(84,'\0');std::uint32_t count=0;
    const auto append=[&](float f){std::uint32_t bits;std::memcpy(&bits,&f,4);for(int i=0;i<4;++i)bytes.push_back(static_cast<char>(bits>>(8*i)));};
    for(const auto handle:handles) {
        const auto mesh=BuildShapeMesh(handle,deflection);
        if(!mesh.HasValue())return Result<std::string>::Failure(mesh.Diagnostics());
        if(mesh.Value().triangles.empty())return Result<std::string>::Failure(base::MakeError("EXP-P002","線や点はSTLにできません。","STEPかKCDを選ぶか、対象を外してください。"));
        for(const auto& t:mesh.Value().triangles){const auto n=Normalized(Cross(t.points[1]-t.points[0],t.points[2]-t.points[0]));
            for(const auto p:{n,t.points[0],t.points[1],t.points[2]}){append(static_cast<float>(p.x));append(static_cast<float>(p.y));append(static_cast<float>(p.z));}
            bytes.append(2,'\0');++count;}
    }
    for(int i=0;i<4;++i)bytes[80+i]=static_cast<char>(count>>(8*i));
    return Result<std::string>::Success(std::move(bytes));
}
}
