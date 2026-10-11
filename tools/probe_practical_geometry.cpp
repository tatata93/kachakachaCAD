// Diagnostic reproductions: never repairs an input file or disables validation.
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctExtrude.h"
#include "kachakacha/kernel/OcctRevolvedThicken.h"
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/modeling/GuideSurfaceInput.h"
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <Geom_Surface.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_OffsetCurve.hxx>
#include <fstream>
#include <STEPControl_Reader.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <iostream>
#include <cmath>
using namespace kachakacha::v2;
void Facts(const TopoDS_Shape& shape,const std::string& label) {
    GProp_GProps p;BRepGProp::VolumeProperties(shape,p);
    GProp_GProps accurate;BRepGProp::VolumeProperties(shape,accurate,1e-10);
    std::cout.precision(17);std::cout<<"adaptive_volume="<<accurate.Mass()<<'\n';
    GProp_GProps spans;const double error=BRepGProp::VolumePropertiesGK(shape,spans,1e-10,false,true,false,false);
    std::cout<<"span_volume="<<spans.Mass()<<" integration_error="<<error<<'\n';
    double maxTol=0;for(TopExp_Explorer v(shape,TopAbs_VERTEX);v.More();v.Next())
        maxTol=std::max(maxTol,BRep_Tool::Tolerance(TopoDS::Vertex(v.Current())));
    BOPAlgo_ArgumentAnalyzer check;check.SetShape1(shape);check.SelfInterMode()=true;check.Perform();
    std::cout<<label<<" volume="<<p.Mass()<<" brep_valid="<<BRepCheck_Analyzer(shape).IsValid()
        <<" vertex_tolerance="<<maxTol<<" bop_fault="<<check.HasFaulty()<<" errors="<<check.HasErrors()<<'\n';
    for(const auto& fault:check.GetCheckResult()) {
        std::cout<<" status="<<int(fault.GetCheckStatus());
        for(const auto& item:fault.GetFaultyShapes1())std::cout<<" type="<<int(item.ShapeType());
        std::cout<<'\n';
    }
    BRepTools::Write(shape,(label+".brep").c_str());std::cout.flush();
}
int main(int argc,char** argv) {
    if(argc==2||argc==3) {
        std::vector<TopoDS_Shape> parts;
        if(argc==2) {
            STEPControl_Reader reader;if(reader.ReadFile(argv[1])!=IFSelect_RetDone)return 2;reader.TransferRoots();
            for(TopExp_Explorer e(reader.OneShape(),TopAbs_SOLID);e.More();e.Next())parts.push_back(e.Current());
        }else for(int i=1;i<3;++i) {
            TopoDS_Shape shape;BRep_Builder builder;std::ifstream in(argv[i]);BRepTools::Read(shape,in,builder);parts.push_back(shape);
        }
        if(parts.size()!=2)return 2;
        Facts(parts[0],"union-input0");Facts(parts[1],"union-input1");
        BRepAlgoAPI_Fuse fuse(parts[0],parts[1]);fuse.Build();
        if(!fuse.IsDone())return 3;
        Facts(fuse.Shape(),"union-raw");fuse.SimplifyResult(true,true);Facts(fuse.Shape(),"union-simplified");
        BRepBuilderAPI_NurbsConvert a(parts[0],true),b(parts[1],true);
        Facts(a.Shape(),"nurbs-input0");Facts(b.Shape(),"nurbs-input1");
        BRepAlgoAPI_Fuse exact(a.Shape(),b.Shape());exact.Build();if(exact.IsDone())Facts(exact.Shape(),"union-nurbs");
        for(double z:{.2,.4,.8}) {
            gp_Trsf tr;tr.SetTranslation(gp_Vec(0,0,z));BRepBuilderAPI_Transform moved(parts[0],tr,true);
            BRepAlgoAPI_Fuse changed(moved.Shape(),parts[1]);changed.Build();
            if(changed.IsDone())Facts(changed.Shape(),"union-base-z"+std::to_string(z));
        }
        return 0;
    }
    const double pi=std::acos(-1.0),k=4.0/3*std::tan(pi/8);
    for(const double tolerance:{1e-6,1e-8}) {
        modeling::ExtrudeRequest request;modeling::ExtrudeProfile profile;profile.closed=true;
        for(int q=0;q<4;++q) {
            const double a=q*pi/2,b=a+pi/2,r=1.6;
            const geometry::Vector3 p{0,r*std::cos(a),23+r*std::sin(a)},t{0,r*std::cos(b),23+r*std::sin(b)};
            const geometry::Vector3 v{0,-r*std::sin(a),r*std::cos(a)},w{0,-r*std::sin(b),r*std::cos(b)};
            profile.segments.push_back(geometry::CurveSegment::MakeCubicBezier({p,p+v*k,t-w*k,t}).Value());
        }
        request.profiles={profile};request.directionMode=modeling::ExtrudeDirectionMode::CustomXYZ;
        request.customDirection={1,0,0};request.distanceMm=1;request.outputs.part=true;
        geometry::GeometryTolerance tol;tol.modelLinearMm=tolerance;
        const auto analysis=modeling::AnalyzeExtrudeRequest(request,tol);
        const auto built=kernel::BuildExtrude(request,analysis.Value(),tol);
        if(built.HasValue()) {
            TopoDS_Shape shape;if(!kernel::LookupShape(built.Value().parts.front().handle,shape))return 3;
            Facts(shape,"tank-end-"+std::to_string(tolerance));
            BRepBuilderAPI_NurbsConvert converted(shape,true);Facts(converted.Shape(),"tank-nurbs-"+std::to_string(tolerance));
        }
    }
    const std::vector<geometry::Vector3> points{
#include "../tests_v2/fixtures/vase_profile_points.inc"
    };
    for(const double angle:{pi/2,2*pi}) {
        modeling::GuideChain chain;chain.role=modeling::ChainRole::Section;
        chain.segments={geometry::CurveSegment::MakeCubicBSpline(points).Value()};
        modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::Revolve;
        request.chains={chain};request.revolveAxisDirection={0,0,1};request.revolveAngleRad=angle;
        const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,{});
        const auto built=kernel::BuildGuideSurface(request,analysis.Value(),{});
        TopoDS_Shape shape;if(!kernel::LookupShape(built.Value().handle,shape))return 4;
        int faceCount=0;for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()) {
            const auto surface=BRep_Tool::Surface(TopoDS::Face(e.Current()));
            std::cout<<"source face "<<++faceCount<<" type="<<surface->DynamicType()->Name()<<'\n';
            const auto rev=occ::handle<Geom_SurfaceOfRevolution>::DownCast(surface);
            if(!rev.IsNull()) {
                std::cout<<"basis="<<rev->BasisCurve()->DynamicType()->Name()<<'\n';
                double u0,u1,v0,v1;BRepTools::UVBounds(TopoDS::Face(e.Current()),u0,u1,v0,v1);
                const auto curve=rev->BasisCurve();
                const occ::handle<Geom_OffsetCurve> inner=new Geom_OffsetCurve(curve,-.8,gp_Dir(0,-1,0));
                std::ofstream csv("vase-meridian.csv");csv<<"t,x,z,ix,iz\n";csv.precision(17);
                for(int i=0;i<=10000;++i) {
                    const double t=v0+(v1-v0)*i/10000;const auto a=curve->Value(t),b=inner->Value(t);
                    csv<<t<<','<<a.X()<<','<<a.Z()<<','<<b.X()<<','<<b.Z()<<'\n';
                }
            }
        }
        const auto profileSolid=kernel::detail::ThickenRevolvedProfile(shape,.8,fabrication::ThicknessPlacement::Inside,{});
        if(profileSolid&&profileSolid->HasValue())Facts(profileSolid->Value(),"vase-profile-"+std::to_string(angle));
        else if(profileSolid)for(const auto& diagnostic:profileSolid->Diagnostics())std::cout<<diagnostic.summaryJa<<" / "<<diagnostic.detailsJa<<'\n';
        for(double t:{-.8,-.4,.8}) {
            BRepOffsetAPI_MakeThickSolid maker;maker.MakeThickSolidBySimple(shape,t);
            if(maker.IsDone())Facts(maker.Shape(),"vase-"+std::to_string(angle)+"-"+std::to_string(t));
        }
    }
}
