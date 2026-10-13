#include "kachakacha/kernel/OcctWireProjection.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <cmath>
#include <stdexcept>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctBoundaryCurves.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <Standard_Failure.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Pln.hxx>
#include <TopTools_ListOfShape.hxx>
#endif
namespace kachakacha::v2::kernel {
using Out=base::Result<RegionProjectionResult>;
#ifdef KACHACAD_V2_WITH_OCCT
namespace {
void Require(bool ok,const char* text) {if(!ok)throw std::runtime_error(text);}
TopoDS_Shape RegionPrism(const WireProjectionInput& input,const WireProjectionOptions& options) {
    Require(!input.curves.empty(),"窓の閉じた平面輪郭を選んでください。");
    BRepBuilderAPI_MakeWire wire;
    for(const auto& c:input.curves) {
        const auto edge=ToEdge(c);Require(edge.HasValue(),"輪郭の辺を変換できません。");
        wire.Add(edge.Value());Require(wire.IsDone(),"輪郭の辺がつながっていません。");
    }
    Require(wire.Wire().Closed(),"面の投影には閉じた輪郭が必要です。不足する辺を追加するか、ワイヤーのみを選んでください。");
    BRepBuilderAPI_MakeFace face(wire.Wire(),true);
    Require(face.IsDone(),"面の投影元は同一平面の閉じた輪郭にしてください。");
    Require(BRepCheck_Analyzer(face.Face()).IsValid(),"輪郭が交差しています。閉領域を選び直してください。");
    BRepAdaptor_Surface surface(face.Face());
    const auto direction=geometry::Normalized(input.direction);
    Require(std::isfinite(direction.x+direction.y+direction.z)&&direction.LengthSquared()>.5,"投影方向を指定してください。");
    const auto n=surface.Plane().Axis().Direction();
    Require(std::abs(n.X()*direction.x+n.Y()*direction.y+n.Z()*direction.z)>1e-9,
        "投影方向が元の平面と平行です。面に交わる方向へ変更してください。");
    const gp_Vec travel(direction.x*options.maximumDistanceMm,direction.y*options.maximumDistanceMm,direction.z*options.maximumDistanceMm);
    TopoDS_Shape start=face.Face();
    if(options.hits!=WireProjectionHits::NearestPositive) {
        gp_Trsf move;move.SetTranslation(-travel);start=BRepBuilderAPI_Transform(start,move,true).Shape();
    }
    BRepPrimAPI_MakePrism prism(start,options.hits==WireProjectionHits::NearestPositive?travel:travel*2,true,true);
    Require(prism.IsDone(),"投影する閉領域を作れません。");return prism.Shape();
}
void Boundary(RegionProjectionResult& result,const TopoDS_Face& face,double tolerance) {
    for(TopExp_Explorer wires(face,TopAbs_WIRE);wires.More();wires.Next()) {
        std::vector<geometry::CurveSegment> run;
        for(BRepTools_WireExplorer e(TopoDS::Wire(wires.Current()),face);e.More();e.Next()) {
            const auto curves=detail::BoundaryCurves(e.Current(),tolerance);
            Require(curves.HasValue(),"投影した面の境界を許容偏差内で変換できません。");
            result.boundary.maxCurveDeviationMm=std::max(result.boundary.maxCurveDeviationMm,curves.Value().maxDeviationBoundMm);
            run.insert(run.end(),curves.Value().curves.begin(),curves.Value().curves.end());
        }
        Require(!run.empty(),"投影した面の境界がありません。");
        Require((run.back().EndPoint()-run.front().StartPoint()).Length()<=tolerance*3,"投影した面の境界が閉じていません。");
        result.boundary.wires.push_back(std::move(run));
    }
}
}
#endif
Out ProjectNativeRegions(const std::vector<modeling::KernelShapeHandle>& targets,
    const std::vector<WireProjectionInput>& inputs,const WireProjectionOptions& options) {
    RegionProjectionResult result;
#ifdef KACHACAD_V2_WITH_OCCT
    try {
        Require(options.mode==WireProjectionMode::Direction,"面の出力は方向投影で指定してください。");
        Require(std::isfinite(options.toleranceMm)&&options.toleranceMm>=1e-7&&options.toleranceMm<=1
            &&std::isfinite(options.maximumDistanceMm)&&options.maximumDistanceMm>0,"許容偏差と最大距離を確認してください。");
        Require(!targets.empty()&&!inputs.empty(),"投影元の閉輪郭と投影先フェイスを選んでください。");
        std::vector<TopoDS_Shape> prisms;
        for(const auto& input:inputs)prisms.push_back(RegionPrism(input,options));
        for(const auto& handle:targets) {
            TopoDS_Shape target;Require(LookupShape(handle,target),"投影先のフェイスがありません。");
            for(TopExp_Explorer face(target,TopAbs_FACE);face.More();face.Next()) {
                BRepBuilderAPI_Copy copy(face.Current(),true,false);Require(copy.IsDone(),"投影先を複製できません。");
                for(const auto& prism:prisms) {
                    TopTools_ListOfShape arguments,tools;arguments.Append(copy.Shape());tools.Append(prism);
                    BRepAlgoAPI_Common common;common.SetArguments(arguments);common.SetTools(tools);
                    common.SetNonDestructive(true);common.SetFuzzyValue(options.toleranceMm);common.Build();
                    Require(common.IsDone()&&!common.HasErrors(),"窓の領域をフェイスへ投影できません。");
                    for(TopExp_Explorer patch(common.Shape(),TopAbs_FACE);patch.More();patch.Next()) {
                        Require(BRepCheck_Analyzer(patch.Current()).IsValid(),"投影した面の検査に失敗しました。");
                        const auto f=TopoDS::Face(patch.Current());Boundary(result,f,options.toleranceMm);
                        result.faces.push_back(StoreShape(f));
                    }
                }
            }
        }
        Require(!result.faces.empty(),"指定した方向・距離では閉領域が選択フェイスに当たりません。");
        return Out::Success(std::move(result));
    } catch(const Standard_Failure& e) {
        for(const auto& h:result.faces)ReleaseShape(h);
        return Out::Failure(base::MakeError(kWireProjectionFailed,"窓の面投影に失敗しました。",e.what()));
    } catch(const std::exception& e) {
        for(const auto& h:result.faces)ReleaseShape(h);
        return Out::Failure(base::MakeError(kWireProjectionFailed,"窓の面投影に失敗しました。",e.what()));
    }
#else
    (void)targets;(void)inputs;(void)options;
    return Out::Failure(base::MakeError(kWireProjectionFailed,"面の投影にはネイティブ幾何カーネルが必要です。",{}));
#endif
}
}
