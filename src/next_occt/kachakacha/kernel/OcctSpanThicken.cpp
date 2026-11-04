#include "kachakacha/kernel/OcctSpanThicken.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepLib_CheckCurveOnSurface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffset_MakeOffset.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <TopTools_ListOfShape.hxx>
#include <BRepTools_Quilt.hxx>
#include <BRep_Tool.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_Surface.hxx>
#include <GeomAbs_JoinType.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <ShapeExtend.hxx>
#include <ShapeFix_ShapeTolerance.hxx>
#include <ShapeUpgrade_FaceDivide.hxx>
#include <ShapeUpgrade_ShapeDivide.hxx>
#include <ShapeUpgrade_SplitSurface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <Geom2d_Curve.hxx>
#include <gp_Pnt2d.hxx>
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <Standard_Type.hxx>
#include <Standard_Failure.hxx>

namespace kachakacha::v2::kernel::detail {
namespace {
void Trace(const std::string& why) {
    if(const auto* path=std::getenv("KACHACAD_SPAN_TRACE")) {
        const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if(std::string(path)=="1")std::cerr<<"span thickening "<<ms<<": "<<why<<std::endl;
        else {std::ofstream out(path,std::ios::app);out<<ms<<" "<<why<<"\n";}
    }
}
std::optional<TopoDS_Shape> Unavailable(const std::string& why) {
    Trace(why);
    return std::nullopt;
}
class ExactKnotSplit final : public ShapeUpgrade_SplitSurface {
    static void Split(occ::handle<NCollection_HSequence<double>>& values,
        const NCollection_Array1<double>& knots,const NCollection_Array1<int>& multiplicities,int degree) {
        const double lo=values->First(),hi=values->Last();std::vector<double> splits{lo,hi};
        for(int i=knots.Lower();i<=knots.Upper();++i)
            if(multiplicities(i)>=degree-1&&knots(i)>lo&&knots(i)<hi)splits.push_back(knots(i));
        std::sort(splits.begin(),splits.end());values->Clear();for(double u:splits)values->Append(u);
    }
    void Compute(const bool) override {
        const auto spline=occ::handle<Geom_BSplineSurface>::DownCast(mySurface);
        if(!spline.IsNull()) {
            Split(myUSplitValues,spline->UKnots(),spline->UMultiplicities(),spline->UDegree());
            Split(myVSplitValues,spline->VKnots(),spline->VMultiplicities(),spline->VDegree());
        }
        myNbResultingRow=myUSplitValues->Length()-1;myNbResultingCol=myVSplitValues->Length()-1;
        myStatus=ShapeExtend::EncodeStatus(myNbResultingRow*myNbResultingCol>1?ShapeExtend_DONE1:ShapeExtend_OK);
    }
};
double MaximumTolerance(const TopoDS_Shape& shape) {
    double result=Precision::Confusion();
    for(TopExp_Explorer e(shape,TopAbs_EDGE);e.More();e.Next())result=std::max(result,BRep_Tool::Tolerance(TopoDS::Edge(e.Current())));
    for(TopExp_Explorer v(shape,TopAbs_VERTEX);v.More();v.Next())result=std::max(result,BRep_Tool::Tolerance(TopoDS::Vertex(v.Current())));
    return result;
}
bool SmoothConnections(const TopoDS_Shape& shape,double precision,double angleTolerance) {
    TopTools_IndexedDataMapOfShapeListOfShape map;TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,map);
    for(int i=1;i<=map.Extent();++i) {
        const auto& faces=map.FindFromIndex(i);if(faces.Size()!=2)continue;
        const auto edge=TopoDS::Edge(map.FindKey(i));const auto a=TopoDS::Face(faces.First()),b=TopoDS::Face(faces.Last());
        double a0,a1,b0,b1;const auto ac=BRep_Tool::CurveOnSurface(edge,a,a0,a1),bc=BRep_Tool::CurveOnSurface(edge,b,b0,b1);
        if(ac.IsNull()||bc.IsNull())return false;
        BRepAdaptor_Surface as(a),bs(b);
        for(int j=1;j<32;++j) {
            const double u=a0+(a1-a0)*j/32.;const auto auv=ac->Value(u),buv=bc->Value(u);
            gp_Pnt ap,bp;gp_Vec au,av,bu,bv;as.D1(auv.X(),auv.Y(),ap,au,av);bs.D1(buv.X(),buv.Y(),bp,bu,bv);
            const auto an=au.Crossed(av),bn=bu.Crossed(bv);
            if(an.Magnitude()<=1e-20||bn.Magnitude()<=1e-20||ap.Distance(bp)>precision
                ||an.Angle(bn)>angleTolerance)return false;
        }
    }
    return true;
}
bool RestoreProvenTolerance(const TopoDS_Shape& shape,double limit) {
    // ComposeShell can double tolerances conservatively. Restore the original
    // bound only after measuring every affected native curve representation and
    // endpoint. Geometry and pcurves are never moved or fitted here.
    for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next()) {
        const auto face=TopoDS::Face(f.Current());
        for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()) {
            const auto edge=TopoDS::Edge(e.Current());
            if(BRep_Tool::Degenerated(edge))continue;
            if(BRep_Tool::Tolerance(edge)>limit) {
                if(!BRep_Tool::SameParameter(edge))return false;
                BRepLib_CheckCurveOnSurface check(edge,face);check.Perform();
                if(!check.IsDone()||!std::isfinite(check.MaxDistance())||check.MaxDistance()>limit*.9)return false;
            }
            BRepAdaptor_Curve curve(edge);
            for(TopExp_Explorer v(edge,TopAbs_VERTEX);v.More();v.Next()) {
                const auto vertex=TopoDS::Vertex(v.Current());
                if(BRep_Tool::Tolerance(vertex)<=limit)continue;
                const double error=BRep_Tool::Pnt(vertex).Distance(curve.Value(BRep_Tool::Parameter(vertex,edge)));
                if(!std::isfinite(error)||error>limit*.9)return false;
            }
        }
    }
    ShapeFix_ShapeTolerance fix;fix.LimitTolerance(shape,0,limit);
    return BRepCheck_Analyzer(shape).IsValid()&&MaximumTolerance(shape)<=limit+1e-10;
}
std::optional<TopoDS_Shape> SplitSource(const TopoDS_Shape& source,double tolerance) {
    bool needed=false;int patches=0;
    for(TopExp_Explorer f(source,TopAbs_FACE);f.More();f.Next()) {
        BRepAdaptor_Surface s(TopoDS::Face(f.Current()),false);
        Trace(std::string("source ")+BRep_Tool::Surface(TopoDS::Face(f.Current()))->DynamicType()->Name()+" type="+std::to_string(s.GetType()));
        if(s.GetType()!=GeomAbs_BSplineSurface){++patches;continue;}
        const auto b=s.BSpline();int nu=1,nv=1;
        Trace("degree "+std::to_string(b->UDegree())+"/"+std::to_string(b->VDegree())+" knots "+std::to_string(b->NbUKnots())+"/"+std::to_string(b->NbVKnots()));
        for(int i=2;i<b->NbUKnots();++i)if(b->UMultiplicity(i)>=b->UDegree()-1)++nu;
        for(int i=2;i<b->NbVKnots();++i)if(b->VMultiplicity(i)>=b->VDegree()-1)++nv;
        if(nu>256/nv)return std::nullopt;
        // C1 supports already use the ordinary native offset path. Splitting
        // them here needlessly repeats expensive trimming and area checks.
        needed=needed||(nu*nv>1&&b->Continuity()==GeomAbs_C0);
        patches+=nu*nv;if(patches>256)return std::nullopt;
    }
    if(!needed)return Unavailable("no C0 parameter spans");
    BRepBuilderAPI_Copy copy(source,true,false);if(!copy.IsDone())return std::nullopt;
    const double limit=std::max(tolerance,MaximumTolerance(source));
    Trace("begin exact span split");
    ShapeUpgrade_ShapeDivide split(copy.Shape());
    const occ::handle<ShapeUpgrade_FaceDivide> divider=new ShapeUpgrade_FaceDivide;
    divider->SetSplitSurfaceTool(new ExactKnotSplit);split.SetSplitFaceTool(divider);
    split.SetSurfaceSegmentMode(true);split.SetPrecision(tolerance);
    split.SetMinTolerance(Precision::Confusion());split.SetMaxTolerance(limit);split.Perform();
    if(split.Status(ShapeExtend_FAIL))return Unavailable("native split reported failure");
    const auto result=split.Result();
    Trace("end exact span split");
    if(result.IsNull()||!BRepCheck_Analyzer(result).IsValid())return Unavailable("invalid split");
    if(MaximumTolerance(result)>limit+1e-10&&!RestoreProvenTolerance(result,limit))
        return Unavailable("split tolerance "+std::to_string(MaximumTolerance(result))+" / "+std::to_string(limit));
    GProp_GProps before,after;
    Trace("begin original area check");
    const double aError=BRepGProp::SurfaceProperties(source,before,1e-10,false);
    Trace("begin split area check");
    const double bError=BRepGProp::SurfaceProperties(result,after,1e-10,false);
    Trace("end area checks");
    if(!std::isfinite(aError)||!std::isfinite(bError)||aError>1e-8||bError>1e-8
        ||!std::isfinite(before.Mass())||!std::isfinite(after.Mass())||before.Mass()<=0||after.Mass()<=0
        ||std::abs(before.Mass()-after.Mass())>std::max(1e-7,before.Mass()*1e-8))
        return Unavailable("split area "+std::to_string(before.Mass())+" / "+std::to_string(after.Mass()));
    for(TopExp_Explorer f(result,TopAbs_FACE);f.More();f.Next())
        if(BRep_Tool::Surface(TopoDS::Face(f.Current()))->Continuity()<GeomAbs_C2)return Unavailable("split basis continuity");
    BRepTools_Quilt quilt;quilt.Add(result);auto shells=quilt.Shells();TopoDS_Iterator i(shells);
    if(!i.More())return Unavailable("no shell");const auto only=i.Value();i.Next();
    if(i.More())return Unavailable("disconnected split"); // Never join disconnected material by proximity.
    return only;
}
std::optional<TopoDS_Shape> OrientedWall(const TopoDS_Shape& shape) {
    if(shape.IsNull())return Unavailable("null wall");
    TopExp_Explorer s(shape,TopAbs_SOLID);if(!s.More())return Unavailable("wall has no solid type="+std::to_string(shape.ShapeType()));
    auto solid=TopoDS::Solid(s.Current());s.Next();
    if(s.More())return Unavailable("wall has multiple solids");
    if(!BRepLib::OrientClosedSolid(solid))return Unavailable("cannot orient wall");
    if(!BRepCheck_Analyzer(solid).IsValid())return Unavailable("wall BRep check");
    return solid;
}
std::optional<TopoDS_Shape> CheckedWall(const TopoDS_Shape& shape,double tolerance) {
    const auto solid=OrientedWall(shape);if(!solid)return std::nullopt;
    const auto handle=StoreShape(*solid);const auto checked=CheckSolidForExport(handle,tolerance);ReleaseShape(handle);
    if(!checked.HasValue()||!checked.Value().Ok()) {
        if(checked.HasValue())Trace("wall closed="+std::to_string(checked.Value().closed)+" self="+
            std::to_string(checked.Value().selfIntersectionFree)+" volume="+std::to_string(checked.Value().volumeMm3));
        else Trace(checked.FirstSummaryJa());
        return Unavailable("wall export check");
    }
    return *solid;
}
std::optional<TopoDS_Shape> VerifiedSimpleWall(const TopoDS_Shape& source,double thickness,
    fabrication::ThicknessPlacement placement,double tolerance) {
    try {
        BRepBuilderAPI_Copy copy(source,true,false);if(!copy.IsDone())return std::nullopt;
        TopoDS_Shape base=copy.Shape();
        if(placement==fabrication::ThicknessPlacement::Centered) {
            BRepOffsetAPI_MakeOffsetShape offset;offset.PerformBySimple(base,-thickness*.5);
            if(!offset.IsDone())return std::nullopt;base=offset.Shape();
        }
        BRepOffsetAPI_MakeThickSolid maker;
        maker.MakeThickSolidBySimple(base,placement==fabrication::ThicknessPlacement::Inside?-thickness:thickness);
        if(!maker.IsDone())return std::nullopt;
        const auto solid=OrientedWall(maker.Shape());if(!solid)return std::nullopt;
        const auto handle=StoreShape(*solid);
        struct Guard {modeling::KernelShapeHandle handle;~Guard(){ReleaseShape(handle);}}guard{handle};
        const auto exported=BuildStepText(handle,tolerance);
        if(!exported.HasValue())return Unavailable("simple C0 wall failed strict STEP readback");
        Trace("simple C0 wall passed strict STEP readback");return *solid;
    }catch(const Standard_Failure& e){Trace(e.what());return std::nullopt;}
}
std::optional<TopoDS_Shape> JoinedAtSource(const TopoDS_Shape& source,double thickness,double tolerance) {
    BRepBuilderAPI_Copy copy(source,true,false);if(!copy.IsDone())return std::nullopt;
    BRepOffset_MakeOffset maker;
    maker.Initialize(copy.Shape(),thickness,tolerance,BRepOffset_Skin,false,false,GeomAbs_Intersection,true,true);
    maker.AllowLinearization(false);
    maker.MakeOffsetShape();if(!maker.IsDone())return Unavailable("source join error="+std::to_string(maker.Error()));
    return CheckedWall(maker.Shape(),tolerance);
}
std::optional<TopoDS_Shape> SymmetricWall(const TopoDS_Shape& source,double thickness,double tolerance) {
    const auto outside=JoinedAtSource(source,thickness*.5,tolerance);if(!outside)return std::nullopt;
    const auto inside=JoinedAtSource(source,-thickness*.5,tolerance);if(!inside)return std::nullopt;
    BRepAlgoAPI_Fuse fuse;fuse.SetNonDestructive(true);
    TopTools_ListOfShape args,tools;args.Append(*outside);tools.Append(*inside);
    fuse.SetArguments(args);fuse.SetTools(tools);fuse.Build();
    if(!fuse.IsDone()||fuse.HasErrors())return std::nullopt;
    return CheckedWall(fuse.Shape(),tolerance);
}
std::optional<TopoDS_Shape> JoinedWall(const TopoDS_Shape& source,double thickness,
    fabrication::ThicknessPlacement placement,double tolerance) {
    try {
        if(placement==fabrication::ThicknessPlacement::Centered)
            if(const auto symmetric=SymmetricWall(source,thickness,tolerance))return symmetric;
        BRepBuilderAPI_Copy copy(source,true,false);if(!copy.IsDone())return std::nullopt;
        TopoDS_Shape base=copy.Shape();
        if(placement==fabrication::ThicknessPlacement::Centered) {
            BRepOffsetAPI_MakeOffsetShape offset;
            offset.PerformByJoin(base,-thickness*.5,tolerance,BRepOffset_Skin,false,false,GeomAbs_Intersection,true);
            if(!offset.IsDone())return std::nullopt;base=offset.Shape();
        }
        BRepOffset_MakeOffset maker;
        maker.Initialize(base,placement==fabrication::ThicknessPlacement::Inside?-thickness:thickness,
            tolerance,BRepOffset_Skin,false,false,GeomAbs_Intersection,true,true);
        maker.AllowLinearization(false);
        maker.MakeOffsetShape();if(!maker.IsDone())return Unavailable("shifted join error="+std::to_string(maker.Error()));
        return CheckedWall(maker.Shape(),tolerance);
    }catch(const Standard_Failure& e){Trace(e.what());return std::nullopt;}
}
std::optional<TopoDS_Shape> IndividualWalls(const TopoDS_Shape& source,double thickness,
    fabrication::ThicknessPlacement placement,double tolerance) {
    TopoDS_Shape united;
    int faceIndex=0;
    for(TopExp_Explorer f(source,TopAbs_FACE);f.More();f.Next()) {
        ++faceIndex;
        BRepBuilderAPI_Copy copy(f.Current(),true,false);if(!copy.IsDone())return std::nullopt;
        TopoDS_Shape base=copy.Shape();
        if(placement==fabrication::ThicknessPlacement::Centered) {
            BRepOffsetAPI_MakeOffsetShape offset;offset.PerformBySimple(base,-thickness*.5);
            if(!offset.IsDone())return Unavailable("face initial offset "+std::to_string(faceIndex));base=offset.Shape();
        }
        BRepOffsetAPI_MakeThickSolid maker;
        maker.MakeThickSolidBySimple(base,placement==fabrication::ThicknessPlacement::Inside?-thickness:thickness);
        if(!maker.IsDone())return Unavailable("face thickening "+std::to_string(faceIndex));
        auto wall=CheckedWall(maker.Shape(),tolerance);
        if(!wall)wall=JoinedWall(f.Current(),thickness,placement,tolerance);
        if(!wall)return Unavailable("face orientation/validity "+std::to_string(faceIndex));
        if(united.IsNull()){united=*wall;continue;}
        BRepAlgoAPI_Fuse fuse;fuse.SetNonDestructive(true);
        TopTools_ListOfShape args,tools;args.Append(united);tools.Append(*wall);
        fuse.SetArguments(args);fuse.SetTools(tools);fuse.Build();
        if(!fuse.IsDone()||fuse.HasErrors()) {
            std::ostringstream errors;fuse.DumpErrors(errors);Trace(errors.str());
            return Unavailable("face union "+std::to_string(faceIndex));
        }
        united=fuse.Shape();
    }
    if(united.IsNull())return std::nullopt;return united;
}
}
std::optional<TopoDS_Shape> ThickenSmoothSpans(const TopoDS_Shape& source,double thickness,
    fabrication::ThicknessPlacement placement,const geometry::GeometryTolerance& tolerance,bool individualOnly) {
    try {
        auto prepared=SplitSource(source,tolerance.modelLinearMm);if(!prepared)return std::nullopt;
        const double sourcePrecision=std::max(tolerance.modelLinearMm,MaximumTolerance(source));
        // This is exact parameter subdivision, not a way to fill genuine
        // creases. OCCT's weak tangent classification accepts angles up to
        // 0.1 rad; do not let that connect displaced skins across a real fold.
        if(!SmoothConnections(*prepared,sourcePrecision,std::max(1e-9,tolerance.modelAngularRad)))
            return Unavailable("source spans have a geometric crease");
        if(!individualOnly) {
            Trace("begin verified simple wall");
            // Use only the already checked C2 patches. A global-C0 offset can
            // be healed by the STEP reader without changing aggregate bounds.
            if(const auto simple=VerifiedSimpleWall(*prepared,thickness,placement,sourcePrecision))return simple;
        }
        Trace("begin joined exact span wall");
        if(!individualOnly)if(const auto joined=JoinedWall(*prepared,thickness,placement,sourcePrecision)){Trace("end joined exact span wall");return joined;}
        Trace("individual exact span walls");const auto walls=IndividualWalls(*prepared,thickness,placement,sourcePrecision);
        if(!walls)return Unavailable("individual wall union");
        const auto checked=CheckedWall(*walls,tolerance.modelLinearMm);
        if(!checked)return Unavailable("individual solid validation");
        return checked;
    }catch(const Standard_Failure& e){return Unavailable(e.what());}
    catch(const std::exception& e){return Unavailable(e.what());}catch(...){return Unavailable("native exception");}
}
}
#endif
