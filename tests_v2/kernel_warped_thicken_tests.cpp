#include "kachakacha/base/TestHarness.h"
#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctOutput.h"
#include <cmath>
#include <cstring>
#include <cstdint>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <Geom_BSplineSurface.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Lin.hxx>
using namespace kachakacha::v2;
using test::Require;
double StlVolume(const std::string& bytes) {
    std::uint32_t count=0;Require(bytes.size()>=84,"STL header exists");std::memcpy(&count,bytes.data()+80,4);
    Require(bytes.size()==84+50*static_cast<std::size_t>(count),"STL rereads with exact triangle count");double volume=0;
    for(std::size_t i=0;i<count;++i) {
        geometry::Vector3 p[3];
        for(int j=0;j<3;++j){float xyz[3];std::memcpy(xyz,bytes.data()+84+50*i+12+12*j,12);p[j]={xyz[0],xyz[1],xyz[2]};}
        volume+=geometry::Dot(p[0],geometry::Cross(p[1],p[2]))/6;
    }
    return volume;
}
KACHA_V2_TEST(warped_thicken, small_warped_band_preserves_source_and_only_accepts_measured_walls)
{
    bool allStepExports=true;
    struct Specimen {int spans;double warp;bool smooth;bool accepted;};
    for(const auto specimen:{Specimen{2,0,false,true},{2,.0003,false,false},{2,.01,false,false},
            {2,.2,false,false},{1,.2,true,true},{2,.2,true,true}}) {
        const int n=specimen.spans+1;const double warp=specimen.warp;
        TColgp_Array2OfPnt poles(1,n,1,2);
        for(int i=1;i<=n;++i){poles.SetValue(i,1,gp_Pnt(10*(i-1),0,specimen.smooth?0:i==n?1:0));
            poles.SetValue(i,2,gp_Pnt(10*(i-1),5,specimen.smooth?warp*(i-1)/(n-1):i==2?warp:i==n?1:0));}
        TColStd_Array1OfReal uk(1,n),vk(1,2);for(int i=1;i<=n;++i)uk.SetValue(i,i-1);vk.SetValue(1,0);vk.SetValue(2,1);
        TColStd_Array1OfInteger um(1,n),vm(1,2);um.Init(1);um.SetValue(1,2);um.SetValue(n,2);vm.Init(2);
        occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(poles,uk,vk,um,vm,1,1);
        const auto face=BRepBuilderAPI_MakeFace(surface,1e-7).Face();const auto source=kernel::StoreShape(face);
        const auto original=kernel::CaptureOutputShape(source);const auto cache=kernel::CachedShapeCount();
        const auto made=kernel::ThickenSurface(source,.8,fabrication::ThicknessPlacement::Inside,{});
        std::cout<<"band: spans="<<specimen.spans<<", smooth="<<specimen.smooth<<", rail warp="<<warp<<" mm, accepted="<<made.HasValue()<<std::endl;
        const auto after=kernel::CaptureOutputShape(source);
        Require(original.HasValue()&&after.HasValue()&&after.Value()==original.Value(),"source BRep remains unchanged");
        Require(made.HasValue()==specimen.accepted,"supported smooth/planar panels and unsupported warped creases are distinguished");
        if(!made.HasValue()) {
            std::cout<<made.FirstSummaryJa()<<" / "<<made.FirstDetailsJa()<<std::endl;
            Require(made.FirstDiagnostic().code==kernel::kThickenFailed,"unsupported warped crease has a stable error code");
            Require(!made.FirstDetailsJa().empty()&&kernel::CachedShapeCount()==cache,"refusal explains why and leaves no result");
        } else {
            TopoDS_Shape solid;Require(kernel::LookupShape(made.Value().handle,solid),"accepted wall exists");double maximum=0;
            for(int patch=0;patch<specimen.spans;++patch)for(int i=0;i<9;++i) {
                gp_Pnt p;gp_Vec du,dv;surface->D1(patch+.1+.1*i,.35,p,du,dv);
                auto normal=du.Crossed(dv);normal.Normalize();IntCurvesFace_ShapeIntersector ray;ray.Load(solid,1e-7);
                ray.Perform(gp_Lin(p,gp_Dir(-normal)),-1e-5,1.6);double inside=1e9;
                for(int j=1;j<=ray.NbPnt();++j)if(ray.WParameter(j)>.01)inside=std::min(inside,ray.WParameter(j));
                // The ray intersector can omit its zero-distance hit on a C0 skin.
                // Check source retention independently rather than loosening tolerance.
                BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(p).Vertex(),solid);
                Require(distance.IsDone()&&distance.Value()<1e-6,"accepted wall retains original skin");
                maximum=std::max(maximum,std::abs(inside-.8));
            }
            std::cout<<"normal thickness maximum error="<<maximum<<" mm / "<<9*specimen.spans<<" rays"<<std::endl;
            Require(maximum<1e-5,"accepted band has measured .8mm thickness");
            const auto step=kernel::OutputStep({made.Value().handle});
            if(!step.HasValue())std::cout<<"STEP refused: "<<step.FirstSummaryJa()<<" / "<<step.FirstDetailsJa()<<std::endl;
            allStepExports=allStepExports&&step.HasValue();
            const auto stl=kernel::OutputStl({made.Value().handle},.002);
            std::cout<<"STL accepted="<<stl.HasValue()<<std::endl;
            if(!stl.HasValue())std::cout<<stl.FirstSummaryJa()<<" / "<<stl.FirstDetailsJa()<<std::endl;
            Require(stl.HasValue(),"supported panel writes closed STL");
            if(stl.HasValue()) {
                const double meshVolume=StlVolume(stl.Value());
                std::cout<<"STL signed volume="<<meshVolume<<", BRep volume="<<made.Value().volumeMm3<<std::endl;
                Require(meshVolume>0&&std::abs(meshVolume-made.Value().volumeMm3)<made.Value().volumeMm3*.001,"STL volume agrees within tessellation error");
            }
            kernel::ReleaseShape(made.Value().handle);
        }
        kernel::ReleaseShape(source);
    }
    Require(allStepExports,"accepted bands survive STEP reread");
}
KACHA_V2_TEST(warped_thicken, real_long_warped_band_is_refused_without_cache_growth) {
    static const char* brep=
#include "fixtures/warped-panel-source.inc"
    ;
    const auto source=kernel::RestoreOutputShape(brep);Require(source.HasValue(),"actual band source is valid");
    const auto before=kernel::CaptureOutputShape(source.Value());const auto count=kernel::CachedShapeCount();
    const auto made=kernel::ThickenSurface(source.Value(),.8,fabrication::ThicknessPlacement::Inside,{});
    Require(!made.HasValue()&&made.FirstDiagnostic().code==kernel::kThickenFailed,"actual warped creases are refused");
    Require(kernel::CachedShapeCount()==count&&kernel::CaptureOutputShape(source.Value()).Value()==before.Value(),"refusal preserves source and cache");
    kernel::ReleaseShape(source.Value());
}
#else
KACHA_V2_TEST(warped_thicken, missing_kernel_refuses_thickening) {
    kachakacha::v2::test::Require(!kachakacha::v2::kernel::ThickenSurface(0,.8,
        kachakacha::v2::fabrication::ThicknessPlacement::Inside,{}).HasValue(),"kernel is required");
}
#endif
KACHA_V2_TEST_MAIN("kernel_warped_thicken_tests")
