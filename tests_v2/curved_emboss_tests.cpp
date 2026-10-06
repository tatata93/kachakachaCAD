#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctCurvedEmboss.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepTools.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include "kachakacha/kernel/OcctBoolean.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepLib.hxx>
#include <Geom2d_Ellipse.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Elips2d.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_BezierSurface.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <gp_Pln.hxx>
#include <gp_Ax3.hxx>
#include <cmath>
using namespace kachakacha::v2;
using test::Require;
using test::RequireNear;
KACHA_V2_TEST(curved_emboss, plane_normal_volume) {
    const auto face=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-30,30,-30,30).Face();
    auto small=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0,20,0,10).Face();
    const auto curves=kernel::FromWire(BRepTools::OuterWire(small),1e-6);Require(curves.HasValue(),"curves");
    kernel::CurvedEmbossOptions options; options.heightMm=2; options.toleranceMm=0.001;
    const auto result=kernel::BuildCurvedEmboss(kernel::StoreShape(face),curves.Value(),options);
    Require(result.HasValue(),result.HasValue() ? "ok" : result.FirstSummaryJa()+result.FirstDiagnostic().detailsJa);
    RequireNear(result.Value().volumeMm3,400,0.05,"closed rectangular volume");
    options.heightMm=-2;
    const auto reverse=kernel::BuildCurvedEmboss(kernel::StoreShape(face),curves.Value(),options);
    Require(reverse.HasValue(),"negative height");RequireNear(reverse.Value().volumeMm3,400,0.05,"reversed closed volume");
}
KACHA_V2_TEST(curved_emboss, circle_draft_and_tilt) {
    const auto face=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-30,30,-30,30).Face();
    const auto circle=geometry::CurveSegment::MakeCircle({0,0,0},{0,0,1},{1,0,0},10);
    Require(circle.HasValue(),"circle");
    for (double tilt : {0.0,0.2}) {
        kernel::CurvedEmbossOptions options; options.heightMm=2;options.draftRad=0.15;options.tiltRad=tilt;options.toleranceMm=0.001;
        const auto result=kernel::BuildCurvedEmboss(kernel::StoreShape(face),{circle.Value()},options);
        Require(result.HasValue(),result.HasValue() ? "ok" : result.FirstSummaryJa()+result.FirstDiagnostic().detailsJa);
        const double r=10-2*std::tan(0.15);
        RequireNear(result.Value().volumeMm3,3.141592653589793*2/3*(100+10*r+r*r),0.15,"conical frustum volume with shear");
    }
}
KACHA_V2_TEST(curved_emboss, cylinder_normal_volume) {
    occ::handle<Geom_CylindricalSurface> surface=new Geom_CylindricalSurface(gp_Ax3(),20);
    const auto face=BRepBuilderAPI_MakeFace(surface,-1,1,-20,20,1e-7).Face();
    const auto patch=BRepBuilderAPI_MakeFace(surface,-0.5,0.5,-5,5,1e-7).Face();
    const auto boundary=kernel::FromWire(BRepTools::OuterWire(patch),1e-6);Require(boundary.HasValue(),"boundary");
    kernel::CurvedEmbossOptions options;options.heightMm=2;options.toleranceMm=0.001;
    const auto result=kernel::BuildCurvedEmboss(kernel::StoreShape(face),boundary.Value(),options);
    Require(result.HasValue(),result.HasValue() ? "ok" : result.FirstSummaryJa()+result.FirstDiagnostic().detailsJa);
    RequireNear(result.Value().volumeMm3,420,0.1,"annular sector volume");
    const auto cylinder=kernel::StoreShape(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,-20),gp_Dir(0,0,1)),20,40).Shape());
    const auto joined=kernel::BuildBoolean(kernel::BooleanOperation::Union,cylinder,result.Value().handle,options.toleranceMm);
    Require(joined.HasValue(),joined.FirstSummaryJa());
    RequireNear(joined.Value().volumeMm3-joined.Value().previousVolumeMm3,420,0.2,"emboss union adds only protrusion");
}
KACHA_V2_TEST(curved_emboss, cylinder_rounded_boundary_angles) {
    occ::handle<Geom_CylindricalSurface> surface=new Geom_CylindricalSurface(gp_Ax3(),20);
    const auto face=BRepBuilderAPI_MakeFace(surface,-1,1,-20,20,1e-7).Face();
    occ::handle<Geom2d_Ellipse> ellipse=new Geom2d_Ellipse(gp_Elips2d(gp_Ax2d(gp_Pnt2d(0,0),gp_Dir2d(0,1)),5,0.3));
    auto edge=BRepBuilderAPI_MakeEdge(ellipse,surface).Edge();Require(BRepLib::BuildCurve3d(edge,1e-5),"3d curve");
    const auto boundary=kernel::FromEdge(edge,1e-5);Require(boundary.HasValue(),"boundary");
    for(double tilt:{0.0,0.2}) {
        kernel::CurvedEmbossOptions options;options.heightMm=1;options.draftRad=0.1;options.tiltRad=tilt;options.tiltReference={0,0,1};options.toleranceMm=0.002;
        const auto result=kernel::BuildCurvedEmboss(kernel::StoreShape(face),{boundary.Value()},options);
        Require(result.HasValue(),result.HasValue()?"ok":result.FirstSummaryJa()+result.FirstDiagnostic().detailsJa);
        Require(result.Value().volumeMm3>70&&result.Value().volumeMm3<110,"plausible rounded patch volume");
        Require(result.Value().sampledBoundaryErrorMm<=options.toleranceMm,"boundary deviation");
    }
}
KACHA_V2_TEST(curved_emboss, freeform_surface_normal) {
    TColgp_Array2OfPnt poles(1,3,1,3);
    for(int i=1;i<=3;++i)for(int j=1;j<=3;++j)
        poles.SetValue(i,j,gp_Pnt(10*(i-2),10*(j-2),(i==2?0:2)+(j==2?0:2)));
    occ::handle<Geom_BezierSurface> surface=new Geom_BezierSurface(poles);
    const auto face=BRepBuilderAPI_MakeFace(surface,0,1,0,1,1e-7).Face();
    const auto patch=BRepBuilderAPI_MakeFace(surface,0.2,0.8,0.2,0.8,1e-7).Face();
    const auto curves=kernel::FromWire(BRepTools::OuterWire(patch),1e-6);Require(curves.HasValue(),"freeform boundary");
    kernel::CurvedEmbossOptions options;options.heightMm=1;options.toleranceMm=0.005;
    const auto result=kernel::BuildCurvedEmboss(kernel::StoreShape(face),curves.Value(),options);
    Require(result.HasValue(),result.FirstSummaryJa()+result.FirstDiagnostic().detailsJa);
    Require(result.Value().volumeMm3>130&&result.Value().volumeMm3<160,"freeform patch volume");
}
KACHA_V2_TEST(curved_emboss, rejects_off_surface_and_open) {
    const auto face=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-30,30,-30,30).Face();
    const auto circle=geometry::CurveSegment::MakeCircle({0,0,2},{0,0,1},{1,0,0},10);
    const auto line=geometry::CurveSegment::MakeLine({0,0,0},{10,0,0});
    Require(!kernel::BuildCurvedEmboss(kernel::StoreShape(face),{circle.Value()},{}).HasValue(),"off-surface rejected");
    Require(!kernel::BuildCurvedEmboss(kernel::StoreShape(face),{line.Value()},{}).HasValue(),"open wire rejected");
}
#endif
KACHA_V2_TEST_MAIN("curved_emboss_tests")
