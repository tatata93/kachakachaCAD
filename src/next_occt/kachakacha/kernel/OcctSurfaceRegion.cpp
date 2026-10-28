#include "kachakacha/kernel/OcctSurfaceRegion.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/WireChain.h"
#include "kachakacha/geometry/WireEdit.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctLoftSurface.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepLib.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <ShapeFix_Edge.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <GeomProjLib.hxx>
#include <Geom_Plane.hxx>
#include <Geom_Curve.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopTools_ListOfShape.hxx>
#include <gp_Trsf.hxx>
#include <gp_Pln.hxx>
#include <Standard_Failure.hxx>
#endif
namespace kachakacha::v2::kernel {
using Out=base::Result<modeling::KernelShapeHandle>;
#ifdef KACHACAD_V2_WITH_OCCT
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::vector<geometry::CurveSegment> OrderedLoop(const std::vector<geometry::CurveSegment>& curves,
    const geometry::GeometryTolerance& tolerance) {
    base::DeterministicIdGenerator ids;
    std::vector<geometry::ChainInput> inputs;
    for(const auto& curve:curves)inputs.push_back({{},ids.NextTyped<base::IdKind::Segment>(),curve});
    const auto chain=geometry::AnalyzeChain(inputs,tolerance);
    Require(chain.HasValue()&&chain.Value().order.closed,"輪郭が閉じていないか、分岐しています。");
    std::vector<geometry::CurveSegment> result;
    for(const auto& item:chain.Value().order.segments) {
        const auto found=std::find_if(inputs.begin(),inputs.end(),[&](const auto& c){return c.segmentId==item.segmentId;});
        Require(found!=inputs.end(),"輪郭の順序を復元できません。");
        if(item.reversed) {
            const auto reversed=geometry::ReverseCurve(found->segment);
            Require(reversed.HasValue(),"輪郭の向きを反転できません。");result.push_back(reversed.Value());
        } else result.push_back(found->segment);
    }
    return result;
}
TopoDS_Shape Cutter(const std::vector<geometry::CurveSegment>& loop,
    const geometry::PlaneFit& fit,double reach,const geometry::GeometryTolerance& tolerance) {
    Require(geometry::SegmentsFormClosedLoop(loop,tolerance),"輪郭が閉じていません。外周と穴を閉じてください。");
    occ::handle<Geom_Plane> plane=new Geom_Plane(gp_Pln(ToPoint(fit.origin),gp_Dir(ToVector(fit.normal))));
    BRepBuilderAPI_MakeWire wire;
    for(const auto& segment:loop) {
        if(segment.Kind()==geometry::CurveKind::Line) {
            const auto project=[&](geometry::Vector3 p){return p-fit.normal*geometry::Dot(p-fit.origin,fit.normal);};
            const auto a=project(segment.StartPoint()),b=project(segment.EndPoint());
            Require(geometry::Distance(a,b)>tolerance.modelLinearMm,"投影方向に潰れる区間があります。");
            BRepBuilderAPI_MakeEdge line(ToPoint(a),ToPoint(b));wire.Add(line.Edge());
            Require(wire.IsDone(),"輪郭の直線が連続していません。");continue;
        }
        const auto edge=ToEdge(segment);Require(edge.HasValue(),"輪郭の曲線を変換できません。");
        double first,last;const auto curve=BRep_Tool::Curve(edge.Value(),first,last);
        auto projected=GeomProjLib::ProjectOnPlane(curve,plane,gp_Dir(ToVector(fit.normal)),true);
        Require(!projected.IsNull(),"輪郭を領域として認識できません。視点方向に折り返しています。");
        BRepBuilderAPI_MakeEdge made(projected,first,last);
        Require(made.IsDone(),"輪郭に潰れた区間があります。支持面を分けて選んでください。");
        auto e=made.Edge();if(edge.Value().Orientation()==TopAbs_REVERSED)e.Reverse();
        BRep_Builder builder;
        for(TopExp_Explorer vertex(e,TopAbs_VERTEX);vertex.More();vertex.Next())
            builder.UpdateVertex(TopoDS::Vertex(vertex.Current()),tolerance.modelLinearMm);
        wire.Add(e);
        Require(wire.IsDone(),"輪郭が連続していません。");
    }
    BRepBuilderAPI_MakeFace face(wire.Wire(),true);
    Require(face.IsDone()&&BRepCheck_Analyzer(face.Face()).IsValid(),"輪郭が重なるか折り返しており、内側を一意に決められません。");
    gp_Trsf shift;shift.SetTranslation(ToVector(fit.normal*(-reach)));
    BRepPrimAPI_MakePrism prism(BRepBuilderAPI_Transform(face.Face(),shift,true).Shape(),ToVector(fit.normal*(2*reach)),true,true);
    Require(prism.IsDone(),"輪郭から切出し領域を作れません。");return prism.Shape();
}
double CheckBoundary(const TopoDS_Shape& result,const std::vector<std::vector<geometry::CurveSegment>>& loops,double eps,double& rms) {
    double maximum=0,squared=0;std::size_t count=0;
    BRep_Builder builder;TopoDS_Compound edges;builder.MakeCompound(edges);
    for(TopExp_Explorer e(result,TopAbs_EDGE);e.More();e.Next())builder.Add(edges,e.Current());
    BRepExtrema_DistShapeShape distance;distance.LoadS2(edges);
    for(const auto& loop:loops)for(const auto& curve:loop)for(int i=0;i<=32;++i) {
        distance.LoadS1(BRepBuilderAPI_MakeVertex(ToPoint(curve.Evaluate(i/32.0))).Vertex());distance.Perform();
        Require(distance.IsDone(),"輪郭と支持面の距離を計算できません。");
        if(distance.Value()>eps)throw std::runtime_error(std::string(&loop==&loops.front()?"外周":"穴の輪郭")+"と支持面境界のずれ "+std::to_string(distance.Value())
            +" mm が許容差 "+std::to_string(eps)+" mm を超えています。投影先の面を確認してください。");
        maximum=std::max(maximum,distance.Value());
        squared+=distance.Value()*distance.Value();++count;
    }
    rms=count?std::sqrt(squared/count):0;
    return maximum;
}
TopoDS_Shape BoundaryConnected(const TopoDS_Shape& shape,
    const std::vector<geometry::CurveSegment>& boundary,double eps) {
    // Sewing connects interior patches as well. Keep only components reached by
    // the specified outer boundary, never the opposite side of a closed support.
    BRep_Builder builder;TopoDS_Compound result;builder.MakeCompound(result);
    const auto append=[&](const TopoDS_Shape& component) {
        bool touches=false;
        for(const auto& curve:boundary)for(int i=0;i<=16&&!touches;++i) {
            BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(ToPoint(curve.Evaluate(i/16.0))).Vertex(),component);
            touches=distance.IsDone()&&distance.Value()<=eps;
        }
        if(touches)builder.Add(result,component);
    };
    for(TopExp_Explorer shell(shape,TopAbs_SHELL);shell.More();shell.Next())append(shell.Current());
    for(TopExp_Explorer face(shape,TopAbs_FACE,TopAbs_SHELL);face.More();face.Next())append(face.Current());
    return result;
}
}
#endif
Out TrimSurfaceRegion(const std::vector<modeling::KernelShapeHandle>& supports,
    const std::vector<std::vector<geometry::CurveSegment>>& inputLoops,const geometry::GeometryTolerance& tolerance,
    double boundaryApproximationMm,double* maximumDeviationMm,double* rmsDeviationMm) {
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        Require(!supports.empty()&&!inputLoops.empty(),"閉輪郭と支持する面が必要です。");
        Require(std::isfinite(boundaryApproximationMm)&&boundaryApproximationMm>=0,"境界近似の許容差が不正です。");
        const double boundaryTolerance=std::max(boundaryApproximationMm,tolerance.modelLinearMm*5);
        std::vector<std::vector<geometry::CurveSegment>> loops;
        for(const auto& loop:inputLoops)loops.push_back(OrderedLoop(loop,tolerance));
        const auto points=geometry::SampleChain(loops.front(),std::max(tolerance.modelLinearMm,1e-4));
        auto fit=geometry::FitPlane(points);Require(fit.valid,"輪郭の内側を判定できません。");
        // The least-squares normal of a narrow wraparound window can point along
        // its height, collapsing the window. Its oriented area points out of it.
        geometry::Vector3 area{};
        for(std::size_t i=0;i<points.size();++i)
            area=area+geometry::Cross(points[i]-fit.origin,points[(i+1)%points.size()]-fit.origin);
        Require(area.Length()>tolerance.modelLinearMm*tolerance.modelLinearMm,"輪郭の内側を一意に判定できません。");
        fit.normal=geometry::Normalized(area);
        double reach=1;for(const auto& p:points)reach=std::max(reach,geometry::Distance(p,fit.origin)*4);
        auto cutter=Cutter(loops.front(),fit,reach,tolerance);
        for(std::size_t i=1;i<loops.size();++i) {
            BRepAlgoAPI_Cut cut(cutter,Cutter(loops[i],fit,reach,tolerance));
            Require(cut.IsDone(),"窓の穴を切り抜けません。");cutter=cut.Shape();
        }
        BRepBuilderAPI_Sewing sewing(tolerance.modelLinearMm);
        int count=0;
        for(const auto handle:supports) {
            TopoDS_Shape support;Require(LookupShape(handle,support),"支持面がありません。");
            for(TopExp_Explorer f(support,TopAbs_FACE);f.More();f.Next()) {
                BRepAlgoAPI_Common common;common.SetNonDestructive(true);
                TopTools_ListOfShape args,tools;args.Append(f.Current());tools.Append(cutter);
                common.SetArguments(args);common.SetTools(tools);common.Build();
                Require(common.IsDone()&&!common.HasErrors(),"支持面から輪郭の領域を切り出せません。");
                for(TopExp_Explorer patch(common.Shape(),TopAbs_FACE);patch.More();patch.Next()) {sewing.Add(patch.Current());++count;}
            }
        }
        Require(count>0,"閉輪郭の内側に支持面がありません。");sewing.Perform();
        const auto result=BoundaryConnected(sewing.SewedShape(),loops.front(),boundaryTolerance);
        Require(BRepCheck_Analyzer(result).IsValid(),"切り出した面が重複または不正です。");
        double rms=0;
        const double deviation=CheckBoundary(result,loops,boundaryTolerance,rms);
        if(maximumDeviationMm)*maximumDeviationMm=deviation;
        if(rmsDeviationMm)*rmsDeviationMm=rms;
        std::vector<base::Diagnostic> notes;
        if(deviation>tolerance.modelLinearMm)notes.push_back(base::MakeWarning("EXT-009",
            "支持面に合わせて境界を切り出しました。最大標本ずれ "+std::to_string(deviation)+" mm。",
            "指定した境界近似許容差 "+std::to_string(boundaryTolerance)+" mm 内。元のワイヤーは変更しません。"));
        return Out::Success(StoreShape(result),std::move(notes));
    }catch(const Standard_Failure& e){return Out::Failure(base::MakeError("EXT-008","面上の輪郭を切り出せません。",e.what()));}
    catch(const std::exception& e){return Out::Failure(base::MakeError("EXT-008","面上の輪郭を切り出せません。",e.what()));}
#else
    (void)supports;(void)inputLoops;(void)tolerance;(void)boundaryApproximationMm;(void)maximumDeviationMm;(void)rmsDeviationMm;
    return Out::Failure(base::MakeError("KER-E005","幾何カーネルが必要です。",{}));
#endif
}
Out TrimFabricationFace(modeling::KernelShapeHandle support,
    const std::vector<std::vector<geometry::CurveSegment>>& loops,const geometry::GeometryTolerance& tolerance) {
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        TopoDS_Shape shape;Require(LookupShape(support,shape),"製作の支持面がありません。");
        TopExp_Explorer faces(shape,TopAbs_FACE);Require(faces.More(),"製作の支持面がありません。");
        auto face=TopoDS::Face(faces.Current());faces.Next();Require(!faces.More(),"製作境界の直接生成には1枚の支持面を指定してください。");
        const auto orientation=face.Orientation();face.Orientation(TopAbs_FORWARD);
        const auto surface=BRep_Tool::Surface(face);Require(!surface.IsNull()&&!loops.empty(),"製作の支持曲面と外周が必要です。");
        std::vector<TopoDS_Wire> wires;
        auto exact=tolerance;exact.interactiveJoinMm=exact.modelLinearMm;
        for(std::size_t loop=0;loop<loops.size();++loop){
            const auto ordered=OrderedLoop(loops[loop],exact);BRepBuilderAPI_MakeWire wire;double area=0;
            for(const auto& curve:ordered){
                const auto converted=ToEdge(curve);Require(converted.HasValue(),"製作境界の辺を変換できません。");
                auto edge=converted.Value();ShapeFix_Edge fix;
                (void)fix.FixAddPCurve(edge,face,false,tolerance.modelLinearMm);
                double first,last;const auto pc=BRep_Tool::CurveOnSurface(edge,face,first,last);
                Require(!pc.IsNull(),"製作境界を支持曲面へ対応付けできません。");
                auto previous=pc->Value(first);
                for(int i=1;i<=16;++i){const auto p=pc->Value(first+(last-first)*i/16.);area+=(edge.Orientation()==TopAbs_REVERSED?-1:1)*(previous.X()*p.Y()-p.X()*previous.Y());previous=p;}
                wire.Add(edge);Require(wire.IsDone(),"製作境界の辺がつながりません。");
            }
            auto made=wire.Wire();Require(made.Closed(),"製作境界が閉じていません。");
            if((area>0)!=(loop==0))made.Reverse();wires.push_back(made);
        }
        BRepBuilderAPI_MakeFace made(surface,wires.front(),true);Require(made.IsDone(),"製作外周から面を作れません。");
        for(std::size_t i=1;i<wires.size();++i){
            for(BRepTools_WireExplorer edges(wires[i],face);edges.More();edges.Next()){
                double first,last;const auto pc=BRep_Tool::CurveOnSurface(edges.Current(),face,first,last);
                Require(!pc.IsNull(),"窓の面上境界がありません。");
                for(int k=0;k<=8;++k){BRepClass_FaceClassifier classified(made.Face(),pc->Value(first+(last-first)*k/8.),tolerance.modelLinearMm);
                    Require(classified.State()==TopAbs_IN,"窓が外周や他の窓と交わるか、材料の外にあります。");}
            }
            made.Add(wires[i]);
        }
        Require(made.IsDone(),"製作の穴を追加できません。");auto result=made.Face();result.Orientation(orientation);
        BRepLib::SameParameter(result,tolerance.modelLinearMm,true);
        Require(BRepCheck_Analyzer(result).IsValid(),"製作境界から作った面が不正です。自己交差や面から離れた辺を確認してください。");
        return Out::Success(StoreShape(result));
    }catch(const Standard_Failure& e){return Out::Failure(base::MakeError("FAB-O003","製作境界から面を生成できません。",e.GetMessageString()));}
    catch(const std::exception& e){return Out::Failure(base::MakeError("FAB-O003","製作境界から面を生成できません。",e.what()));}
#else
    (void)support;(void)loops;(void)tolerance;
    return Out::Failure(base::MakeError("KER-E005","幾何カーネルが必要です。",{}));
#endif
}
base::Result<modeling::GuideSurfaceResult> BuildSurfaceWithHoles(
    const modeling::GuideSurfaceRequest& request,const geometry::GeometryTolerance& tolerance) {
    using Result=base::Result<modeling::GuideSurfaceResult>;
    auto outer=request;
    outer.chains.erase(std::remove_if(outer.chains.begin(),outer.chains.end(),[](const auto& c){return c.role==modeling::ChainRole::HoleBoundary||c.role==modeling::ChainRole::SourceSurface;}),outer.chains.end());
    const auto analysis=modeling::AnalyzeGuideSurfaceRequest(outer,tolerance);
    if(!analysis.HasValue())return Result::Failure(analysis.Diagnostics());
    std::vector<modeling::KernelShapeHandle> supports;
    for(const auto& chain:request.chains)if(chain.role==modeling::ChainRole::SourceSurface&&!chain.supportShapeHandle)
        return Result::Failure(base::MakeError("EXT-008","指定した支持面の形状を取得できません。",
            "別の曲面へ置き換えず、元の支持面を再構築してください。"));
    for(const auto& chain:request.chains)if(chain.role==modeling::ChainRole::SourceSurface&&chain.supportShapeHandle) {
        supports.push_back({chain.supportShapeHandle});
    }
    const bool owned=supports.empty();
    std::vector<base::Diagnostic> warnings;
    if(owned) {
        const auto source=BuildGuideSurface(outer,analysis.Value(),tolerance);
        if(!source.HasValue())return source;
        supports.push_back(source.Value().handle);
        warnings=source.Diagnostics();
    }
    std::vector<std::vector<geometry::CurveSegment>> loops(1);
    for(const auto& c:outer.chains)if(c.role==modeling::ChainRole::BoundarySide)
        loops.front().insert(loops.front().end(),c.segments.begin(),c.segments.end());
    for(const auto& c:request.chains)if(c.role==modeling::ChainRole::HoleBoundary)loops.push_back(c.segments);
    double deviation=0,rms=0;
    const auto trimmed=TrimSurfaceRegion(supports,loops,tolerance,tolerance.interactiveJoinMm,&deviation,&rms);
    if(owned)ReleaseShape(supports.front());
    if(!trimmed.HasValue())return Result::Failure(trimmed.Diagnostics());
    warnings.insert(warnings.end(),trimmed.Diagnostics().begin(),trimmed.Diagnostics().end());
#ifdef KACHACAD_V2_WITH_OCCT
    TopoDS_Shape shape;LookupShape(trimmed.Value(),shape);
    auto result=detail::FinishSurfaceResult(shape,tolerance);ReleaseShape(trimmed.Value());
    if(!result.HasValue())return result;
    warnings.insert(warnings.end(),result.Diagnostics().begin(),result.Diagnostics().end());
    auto value=result.Value();value.maximumDeviationMm=std::max(value.maximumDeviationMm,deviation);value.rmsDeviationMm=rms;
    return Result::Success(std::move(value),std::move(warnings));
#else
    return Result::Failure(base::MakeError("KER-E005","幾何カーネルが必要です。",{}));
#endif
}
}
