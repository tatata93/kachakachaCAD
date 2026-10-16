#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctRegionSplit.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include <limits>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#endif
using namespace kachakacha::v2;
using test::Require;
using test::RequireNear;
namespace {
constexpr double tol=1e-6;
geometry::CurveSegment Line(geometry::Vector3 a,geometry::Vector3 b) {return geometry::CurveSegment::MakeLine(a,b).Value();}
void Verify(const kernel::ContactResult& result,int count,int dim,double mass) {
    Require(static_cast<int>(result.pieces.size())==count,"connected region count");double total=0;
    for(const auto& piece:result.pieces){Require(piece.dimension==dim,"dimension retained");total+=piece.volumeMm3;}
    RequireNear(total,mass,1e-5,"area/length/volume conserved");
}
}
#ifdef KACHACAD_V2_WITH_OCCT
KACHA_V2_TEST(region_split,SolidPlaneBoundaryAndInputUnchanged) {
    const auto box=kernel::StoreShape(BRepPrimAPI_MakeBox(10,8,6).Shape());
    const auto before=kernel::CaptureOutputShape(box).Value();
    const auto plane=kernel::RegionSplitPoint({4,0,0},{1,0,0},true).Value();
    for(bool wires:{false,true}) {
        const auto made=kernel::SplitNativeRegions(box,plane,wires,tol);
        Require(made.HasValue(),made.FirstMessageJa());Verify(made.Value(),2,3,480);
        Require(wires?!made.Value().wires.empty():made.Value().wires.empty(),"requested boundary output");
        Require(kernel::CaptureOutputShape(box).Value()==before,"source BRep unchanged");
    }
    kernel::ReleaseShape(box);kernel::ReleaseShape(plane);
}
KACHA_V2_TEST(region_split,ClosedWireCurtainCutsWindow) {
    const auto box=kernel::StoreShape(BRepPrimAPI_MakeBox(10,8,6).Shape());
    const std::vector<geometry::CurveSegment> loop={Line({2,2,0},{8,2,0}),Line({8,2,0},{8,6,0}),
        Line({8,6,0},{2,6,0}),Line({2,6,0},{2,2,0})};
    const auto curtain=kernel::RegionSplitWire(loop,box,{0,0,1},true,tol);
    Require(curtain.HasValue(),curtain.FirstMessageJa());
    const auto made=kernel::SplitNativeRegions(box,curtain.Value(),true,tol);
    Require(made.HasValue(),made.FirstMessageJa());Verify(made.Value(),2,3,480);
    Require(!made.Value().wires.empty(),"cut window boundary retained");
    kernel::ReleaseShape(box);kernel::ReleaseShape(curtain.Value());
}
KACHA_V2_TEST(region_split,SurfaceAcrossSeamKeepsConnectedRegions) {
    BRepBuilderAPI_Sewing sewing(tol);
    sewing.Add(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0,5,0,8).Face());
    sewing.Add(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),5,10,0,8).Face());sewing.Perform();
    const auto surface=kernel::StoreShape(sewing.SewedShape());
    const auto line=kernel::RegionSplitWire({Line({0,3,0},{10,3,0})},{},{0,0,1},false,tol).Value();
    const auto made=kernel::SplitNativeRegions(surface,line,true,tol);
    Require(made.HasValue(),made.FirstMessageJa());Verify(made.Value(),2,2,80);
    kernel::ReleaseShape(surface);kernel::ReleaseShape(line);
}
KACHA_V2_TEST(region_split,CircularWindowBoundaryPreservesAnalyticCurves) {
    const auto box=kernel::StoreShape(BRepPrimAPI_MakeBox(10,8,6).Shape());
    const auto circle=geometry::CurveSegment::MakeCircle({5,4,0},{0,0,1},{1,0,0},2).Value();
    const auto curtain=kernel::RegionSplitWire({circle},box,{0,0,1},true,tol).Value();
    const auto made=kernel::SplitNativeRegions(box,curtain,true,tol);
    Require(made.HasValue(),made.FirstMessageJa());Verify(made.Value(),2,3,480);
    bool curved=false;for(const auto& wire:made.Value().wires)for(const auto& c:wire)
        curved=curved||c.Kind()==geometry::CurveKind::Circle||c.Kind()==geometry::CurveKind::CircularArc;
    Require(curved,"native circular cut boundary is not a polygon");
    kernel::ReleaseShape(box);kernel::ReleaseShape(curtain);
}
KACHA_V2_TEST(region_split,WirePointKeepsConnectedOriginalSegments) {
    const auto wire=kernel::RegionSplitWire({Line({0,0,0},{4,0,0}),Line({4,0,0},{8,0,0})},{},{},false,tol).Value();
    const auto point=kernel::RegionSplitPoint({2,0,0},{},false).Value();
    const auto made=kernel::SplitNativeRegions(wire,point,false,tol);
    Require(made.HasValue(),made.FirstMessageJa());Verify(made.Value(),2,1,8);
    kernel::ReleaseShape(wire);kernel::ReleaseShape(point);
}
KACHA_V2_TEST(region_split,UnsupportedNonintersectionAndInvalidTolerance) {
    const auto wire=kernel::RegionSplitWire({Line({0,0,0},{8,0,0})},{},{},false,tol).Value();
    const auto point=kernel::RegionSplitPoint({2,1,0},{},false).Value();
    Require(!kernel::SplitNativeRegions(wire,point,false,tol).HasValue(),"off-wire point refused");
    Require(!kernel::SplitNativeRegions(point,wire,false,tol).HasValue(),"point target refused");
    Require(!kernel::SplitNativeRegions(wire,point,false,std::numeric_limits<double>::quiet_NaN()).HasValue(),"NaN refused");
    kernel::ReleaseShape(wire);kernel::ReleaseShape(point);
}
KACHA_V2_TEST(region_split,SharedPreviewOwnershipReleasesOnLastCopy) {
    const auto box=kernel::StoreShape(BRepPrimAPI_MakeBox(10,8,6).Shape());
    const auto plane=kernel::RegionSplitPoint({4,0,0},{1,0,0},true).Value();
    modeling::KernelShapeHandle output;
    {
        auto made=kernel::SplitNativeRegions(box,plane,false,tol);Require(made.HasValue(),made.FirstMessageJa());
        output=made.Value().pieces.front().handle;
        {auto copy=made.Value();TopoDS_Shape shape;Require(kernel::LookupShape(output,shape),"copy owns native shape");}
        TopoDS_Shape shape;Require(kernel::LookupShape(output,shape),"original result still owns shape");
    }
    TopoDS_Shape shape;Require(!kernel::LookupShape(output,shape),"discard frees native result");
    Require(kernel::LookupShape(box,shape),"discard preserves inputs");
    kernel::ReleaseShape(box);kernel::ReleaseShape(plane);
}
#endif
KACHA_V2_TEST_MAIN("kernel_region_split_tests")
