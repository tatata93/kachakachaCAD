#include "kachakacha/kernel/OcctRegionSplit.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <algorithm>
#include <cmath>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctBoundaryCurves.h"
#include <BOPAlgo_Splitter.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <gp_Trsf.hxx>
#include <gp_Pln.hxx>
#include <gp_Dir.hxx>
#include <Standard_Failure.hxx>
#endif
namespace kachakacha::v2::kernel {
namespace {
base::Diagnostic Problem(const char* detail) {return base::MakeError("KER-TR01","交点で区画を分割できません。",detail);}
#ifdef KACHACAD_V2_WITH_OCCT
int Dimension(const TopoDS_Shape& shape) {
    if(TopExp_Explorer(shape,TopAbs_SOLID).More())return 3;
    if(TopExp_Explorer(shape,TopAbs_FACE).More())return 2;
    if(TopExp_Explorer(shape,TopAbs_EDGE).More())return 1;
    return 0;
}
bool IsCutBoundary(const TopoDS_Shape& boundary,const TopoDS_Shape& tool,int dimension,double tolerance) {
    if(dimension==1) {
        BRepExtrema_DistShapeShape distance(boundary,tool);
        return distance.IsDone()&&distance.Value()<=tolerance;
    }
    BRepAlgoAPI_Common common;
    TopTools_ListOfShape arguments,tools;arguments.Append(boundary);tools.Append(tool);
    common.SetArguments(arguments);common.SetTools(tools);common.SetNonDestructive(true);common.SetFuzzyValue(tolerance);common.Build();
    if(!common.IsDone())throw Standard_Failure("Cannot classify a cut boundary");
    GProp_GProps full,overlap;BRepGProp::LinearProperties(boundary,full);BRepGProp::LinearProperties(common.Shape(),overlap);
    return std::abs(full.Mass()-overlap.Mass())<=tolerance;
}
std::vector<TopoDS_Shape> ConnectedRegions(const TopTools_IndexedMapOfShape& shapes,
    const TopoDS_Shape& tool,int dimension,double tolerance) {
    std::vector<TopoDS_Shape> result;
    if(dimension==3){for(int i=1;i<=shapes.Extent();++i)result.push_back(shapes(i));return result;}
    std::vector<TopTools_IndexedMapOfShape> boundaries(shapes.Extent());
    for(int i=1;i<=shapes.Extent();++i)TopExp::MapShapes(shapes(i),dimension==2?TopAbs_EDGE:TopAbs_VERTEX,boundaries[i-1]);
    std::vector<bool> used(shapes.Extent(),false);
    for(int seed=0;seed<shapes.Extent();++seed)if(!used[seed]) {
        std::vector<int> group{seed};used[seed]=true;
        for(std::size_t next=0;next<group.size();++next)for(int j=0;j<shapes.Extent();++j)if(!used[j]) {
            const auto& first=boundaries[group[next]];
            for(int edge=1;edge<=first.Extent();++edge) {
                bool shared=boundaries[j].Contains(first(edge));
                if(dimension==1&&!shared)for(int v=1;v<=boundaries[j].Extent();++v) {
                    BRepExtrema_DistShapeShape distance(first(edge),boundaries[j](v));
                    shared=shared||(distance.IsDone()&&distance.Value()<=tolerance);
                }
                if(shared&&!IsCutBoundary(first(edge),tool,dimension,tolerance)){used[j]=true;group.push_back(j);break;}
            }
        }
        BRep_Builder builder;
        if(dimension==2) {
            TopoDS_Shell shell;builder.MakeShell(shell);for(int i:group)builder.Add(shell,shapes(i+1));result.push_back(shell);
        } else {
            BRepBuilderAPI_MakeWire wire;TopTools_ListOfShape edges;for(int i:group)edges.Append(shapes(i+1));wire.Add(edges);
            if(!wire.IsDone())throw Standard_Failure("Cannot assemble a connected wire region");result.push_back(wire.Wire());
        }
    }
    return result;
}
#endif
}
base::Result<ContactResult> SplitNativeRegions(modeling::KernelShapeHandle target,
    modeling::KernelShapeHandle tool,bool boundaryWires,double toleranceMm) {
    using Out=base::Result<ContactResult>;
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        if(!std::isfinite(toleranceMm)||toleranceMm<=0)return Out::Failure(Problem("正の有限な許容差が必要です。"));
        TopoDS_Shape a,b;
        if(!LookupShape(target,a)||!LookupShape(tool,b))return Out::Failure(Problem("対象か切断要素がありません。"));
        const int dim=Dimension(a);
        if(dim==0)return Out::Failure(Problem("点を削る区画はありません。点はワイヤーの切断位置として選んでください。"));
        BOPAlgo_Splitter split;split.SetNonDestructive(true);split.SetFuzzyValue(toleranceMm);
        split.AddArgument(a);split.AddTool(b);split.Perform();
        if(split.HasErrors()||!BRepCheck_Analyzer(split.Shape()).IsValid())return Out::Failure(Problem("切断要素が対象を分割できる形か確認してください。"));
        const auto type=dim==3?TopAbs_SOLID:dim==2?TopAbs_FACE:TopAbs_EDGE;
        TopTools_IndexedMapOfShape before,after;TopExp::MapShapes(a,type,before);TopExp::MapShapes(split.Shape(),type,after);
        if(after.Extent()<=before.Extent())return Out::Failure(Problem("分割する交差がありません。面は対象を横切る範囲、ワイヤーは対象面上、点はワイヤー上に置いてください。"));
        ContactResult result;result.frozen=true;
        result.ownedShapes={new std::vector<modeling::KernelShapeHandle>,[](auto* shapes){
            for(auto handle:*shapes)ReleaseShape(handle);delete shapes;}};
        const auto regions=ConnectedRegions(after,b,dim,toleranceMm);
        for(std::size_t i=0;i<regions.size();++i) {
            GProp_GProps props;
            if(dim==3)BRepGProp::VolumeProperties(regions[i],props);
            else if(dim==2)BRepGProp::SurfaceProperties(regions[i],props);
            else BRepGProp::LinearProperties(regions[i],props);
            if(std::abs(props.Mass())<=std::pow(toleranceMm,dim))return Out::Failure(Problem("許容差以下の区画があります。"));
            const auto handle=StoreShape(regions[i]);result.ownedShapes->push_back(handle);
            result.pieces.push_back({handle,std::abs(props.Mass()),false,static_cast<int>(i),0,FromPoint(props.CentreOfMass()),dim});
        }
        if(boundaryWires&&dim>1) {
            BRepAlgoAPI_Section section(a,b,false);section.SetNonDestructive(true);section.Build();
            if(!section.IsDone())return Out::Failure(Problem("切断境界のワイヤーを作れません。"));
            TopTools_IndexedMapOfShape edges;TopExp::MapShapes(section.Shape(),TopAbs_EDGE,edges);
            for(int i=1;i<=edges.Extent();++i) {
                const auto curves=detail::BoundaryCurves(TopoDS::Edge(edges(i)),toleranceMm);
                if(!curves.HasValue())return Out::Failure(curves.Diagnostics());
                result.wires.push_back(curves.Value().curves);
            }
        }
        return Out::Success(std::move(result));
    }catch(const Standard_Failure& failure){return Out::Failure(Problem(failure.GetMessageString()));}
#else
    (void)target;(void)tool;(void)boundaryWires;(void)toleranceMm;return Out::Failure(Problem("幾何カーネルが必要です。"));
#endif
}
base::Result<modeling::KernelShapeHandle> RegionSplitWire(const std::vector<geometry::CurveSegment>& curves,
    modeling::KernelShapeHandle target,geometry::Vector3 direction,bool curtain,double toleranceMm) {
    using Out=base::Result<modeling::KernelShapeHandle>;
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        BRep_Builder builder;TopoDS_Compound edges;builder.MakeCompound(edges);
        for(const auto& curve:curves){const auto edge=ToEdge(curve);if(!edge.HasValue())return Out::Failure(edge.Diagnostics());builder.Add(edges,edge.Value());}
        if(curves.empty())return Out::Failure(Problem("ワイヤーに線がありません。"));
        if(!curtain)return Out::Success(StoreShape(edges));
        TopoDS_Shape source;if(!LookupShape(target,source))return Out::Failure(Problem("対象形状がありません。"));
        if(direction.Length()<=toleranceMm)return Out::Failure(Problem("切断方向を指定してください。"));
        Bnd_Box box;BRepBndLib::Add(source,box);BRepBndLib::Add(edges,box);
        double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
        const double reach=2*geometry::Vector3{x1-x0,y1-y0,z1-z0}.Length();direction=geometry::Normalized(direction);
        gp_Trsf move;move.SetTranslation(ToVector(direction*(-reach)));
        BRepPrimAPI_MakePrism prism(BRepBuilderAPI_Transform(edges,move,true).Shape(),ToVector(direction*(2*reach)),true);
        if(!prism.IsDone())return Out::Failure(Problem("ワイヤーから貫通する切断面を作れません。"));
        return Out::Success(StoreShape(prism.Shape()));
    }catch(const Standard_Failure& failure){return Out::Failure(Problem(failure.GetMessageString()));}
#else
    (void)curves;(void)target;(void)direction;(void)curtain;(void)toleranceMm;return Out::Failure(Problem("幾何カーネルが必要です。"));
#endif
}
base::Result<modeling::KernelShapeHandle> RegionSplitPoint(geometry::Vector3 point,geometry::Vector3 normal,bool asPlane) {
    using Out=base::Result<modeling::KernelShapeHandle>;
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        if(!point.IsFinite()||!normal.IsFinite())return Out::Failure(Problem("有限の座標を指定してください。"));
        if(!asPlane)return Out::Success(StoreShape(BRepBuilderAPI_MakeVertex(ToPoint(point)).Vertex()));
        if(normal.Length()==0)return Out::Failure(Problem("切断面の法線を指定してください。"));
        return Out::Success(StoreShape(BRepBuilderAPI_MakeFace(gp_Pln(ToPoint(point),gp_Dir(ToVector(normal)))).Face()));
    }catch(const Standard_Failure& failure){return Out::Failure(Problem(failure.GetMessageString()));}
#else
    (void)point;(void)normal;(void)asPlane;return Out::Failure(Problem("幾何カーネルが必要です。"));
#endif
}
}
