// Read-only diagnostic for saved/exported geometry. Not a model repair path.
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <BRepLib.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeCustom.hxx>
#include <ShapeUpgrade_ShapeDivideContinuity.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <Standard_Failure.hxx>
#include "kachakacha/kernel/OcctMassProperties.h"
#include <fstream>
#include <iostream>
#include <string>
#include <iomanip>
#include <gp_Pln.hxx>

int main(int argc,char** argv)
{
    std::cout.setf(std::ios::unitbuf);
    try {
    if(argc!=2&&argc!=3){std::cerr<<"usage: inspect_brep input.brep|input.step [output.stl]\n";return 2;}
    TopoDS_Shape shape;
    const std::string path=argv[1];
    if(path.ends_with(".step")||path.ends_with(".stp")) {
        STEPControl_Reader reader;
        if(reader.ReadFile(path.c_str())!=IFSelect_RetDone)return 3;
        reader.TransferRoots();
        reader.PrintCheckLoad(false,IFSelect_ItemsByEntity);
        reader.PrintCheckTransfer(false,IFSelect_ItemsByEntity);
        shape=reader.OneShape();
    } else {std::ifstream in(path);BRep_Builder builder;BRepTools::Read(shape,in,builder);}
    if(shape.IsNull())return 4;
    if(argc==3&&std::string(argv[2])=="--tolerances") {
        double vertex=0,edge=0,face=0;
        for(TopExp_Explorer e(shape,TopAbs_VERTEX);e.More();e.Next())vertex=std::max(vertex,BRep_Tool::Tolerance(TopoDS::Vertex(e.Current())));
        for(TopExp_Explorer e(shape,TopAbs_EDGE);e.More();e.Next())edge=std::max(edge,BRep_Tool::Tolerance(TopoDS::Edge(e.Current())));
        for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next())face=std::max(face,BRep_Tool::Tolerance(TopoDS::Face(e.Current())));
        std::cout<<std::setprecision(17)<<"max_vertex="<<vertex<<" max_edge="<<edge<<" max_face="<<face<<std::endl;
        return 0;
    }
    if(argc==3&&std::string(argv[2])=="--mass-split-parameter") {
        BRepBuilderAPI_Copy copy(shape,true,false);
        ShapeUpgrade_ShapeDivideContinuity split(copy.Shape());
        split.SetSurfaceCriterion(GeomAbs_C3);split.SetBoundaryCriterion(GeomAbs_C3);
        split.SetPCurveCriterion(GeomAbs_C3);split.SetTolerance(1e-7);split.Perform();
        auto result=split.Result();
        std::cout<<"split valid="<<BRepCheck_Analyzer(result).IsValid()<<std::endl;
        BRepLib::SameParameter(result,1e-7,true);
        double maximum=0;
        for(TopExp_Explorer e(result,TopAbs_EDGE);e.More();e.Next())maximum=std::max(maximum,BRep_Tool::Tolerance(TopoDS::Edge(e.Current())));
        std::cout<<"parameterized valid="<<BRepCheck_Analyzer(result).IsValid()<<" max_edge_tolerance="<<maximum<<std::endl;
        GProp_GProps mass;auto error=BRepGProp::VolumePropertiesGK(result,mass,1e-11,false,true,false,false);
        std::cout<<std::setprecision(17)<<"mass="<<mass.Mass()<<" error="<<error<<std::endl;
        for(const auto& axis:{gp_Dir(1,0,0),gp_Dir(0,1,0),gp_Dir(0,0,1)}) {
            GProp_GProps plane;error=BRepGProp::VolumePropertiesGK(result,plane,gp_Pln(gp_Pnt(0,0,0),axis),1e-11,false,true,false,false);
            std::cout<<"plane="<<axis.X()<<","<<axis.Y()<<","<<axis.Z()<<" mass="<<plane.Mass()<<" error="<<error<<std::endl;
        }
        return 0;
    }
    if(argc==3&&std::string(argv[2])=="--strip-mesh") {
        BRepTools::Clean(shape,true);
        BRepTools::Write(shape,(path+".geometry.brep").c_str());
        return 0;
    }
    if(argc==3&&std::string(argv[2])=="--mass-bspline") {
        std::cout.precision(17);
        for(bool revolution:{false,true}) {
            const auto result=ShapeCustom::ConvertToBSpline(shape,true,revolution,false,false);
            std::cout<<"convert revolution="<<revolution<<" valid="<<BRepCheck_Analyzer(result).IsValid()<<std::endl;
            GProp_GProps mass;auto error=BRepGProp::VolumePropertiesGK(result,mass,1e-11,false,true,false,false);
            std::cout<<"mass="<<mass.Mass()<<" error="<<error<<std::endl;
            for(const auto& axis:{gp_Dir(1,0,0),gp_Dir(0,1,0),gp_Dir(0,0,1)}) {
                GProp_GProps plane;error=BRepGProp::VolumePropertiesGK(result,plane,gp_Pln(gp_Pnt(0,0,0),axis),1e-11,false,true,false,false);
                std::cout<<"plane="<<axis.X()<<","<<axis.Y()<<","<<axis.Z()<<" mass="<<plane.Mass()<<" error="<<error<<std::endl;
            }
        }
        return 0;
    }
    if(argc==3&&(std::string(argv[2])=="--mass-surface-split"||std::string(argv[2])=="--mass-remove-internal")) {
        if(std::string(argv[2])=="--mass-remove-internal") {
            BRepBuilderAPI_Copy copy(shape,true,false);shape=copy.Shape();
            BRepTools::RemoveInternals(shape,false);
            std::cout<<"without internal valid="<<BRepCheck_Analyzer(shape).IsValid()<<std::endl;
        }
        std::cout.precision(17);
        for(auto criterion:{GeomAbs_C0,GeomAbs_C1,GeomAbs_C2,GeomAbs_C3}) {
            BRepBuilderAPI_Copy copy(shape,true,false);
            ShapeUpgrade_ShapeDivideContinuity split(copy.Shape());
            split.SetSurfaceCriterion(GeomAbs_C3);split.SetBoundaryCriterion(criterion);
            split.SetPCurveCriterion(criterion);split.SetTolerance(1e-7);split.Perform();
            const auto result=split.Result();
            std::cout<<"boundary criterion="<<int(criterion)<<" valid="<<BRepCheck_Analyzer(result).IsValid()<<std::endl;
            GProp_GProps mass;auto error=BRepGProp::VolumePropertiesGK(result,mass,1e-11,false,true,false,false);
            std::cout<<"mass="<<mass.Mass()<<" error="<<error<<std::endl;
        }
        return 0;
    }
    if(argc==3&&std::string(argv[2])=="--mass-convergence") {
        std::cout.precision(17);
        for(double epsilon:{1e-8,1e-10,1e-12,1e-14}) {
            GProp_GProps mass;
            auto error=BRepGProp::VolumePropertiesGK(shape,mass,epsilon,false,true,false,false);
            std::cout<<"epsilon="<<epsilon<<" mass="<<mass.Mass()<<" error="<<error<<std::endl;
        }
        for(const auto& axis:{gp_Dir(1,0,0),gp_Dir(0,1,0),gp_Dir(0,0,1)}) {
            GProp_GProps mass;
            auto error=BRepGProp::VolumePropertiesGK(shape,mass,gp_Pln(gp_Pnt(0,0,0),axis),1e-11,false,true,false,false);
            std::cout<<"plane="<<axis.X()<<","<<axis.Y()<<","<<axis.Z()<<" mass="<<mass.Mass()<<" error="<<error<<std::endl;
        }
        return 0;
    }
    if(argc==3&&(std::string(argv[2])=="--step-diagnostic"||std::string(argv[2])=="--step-split")) {
        if(std::string(argv[2])=="--step-split") {
            BRepBuilderAPI_Copy copy(shape,true,false);
            ShapeUpgrade_ShapeDivideContinuity split(copy.Shape());
            split.SetSurfaceCriterion(GeomAbs_C3);split.SetBoundaryCriterion(GeomAbs_C3);
            split.SetPCurveCriterion(GeomAbs_C3);split.SetTolerance(1e-7);split.Perform();
            shape=split.Result();
            std::cout<<"Split valid="<<BRepCheck_Analyzer(shape).IsValid()<<std::endl;
        }
        STEPControl_Writer writer;
        if(writer.Transfer(shape,STEPControl_AsIs)!=IFSelect_RetDone)return 9;
        const auto step=path+".unverified.step";
        if(writer.Write(step.c_str())!=IFSelect_RetDone)return 10;
        STEPControl_Reader reader;
        if(reader.ReadFile(step.c_str())!=IFSelect_RetDone)return 11;
        reader.TransferRoots();
        BRepTools::Write(reader.OneShape(),(path+".step-readback.brep").c_str());
        return 0;
    }
    if(argc==3&&std::string(argv[2])=="--surface-bspline") {
        shape=ShapeCustom::ConvertToBSpline(shape,true,false,false,false);
        BRepTools::Write(shape,(path+".surface-bspline.brep").c_str());
        std::cout<<"Surface BSpline valid="<<BRepCheck_Analyzer(shape).IsValid()<<std::endl;
        BOPAlgo_ArgumentAnalyzer check;check.SetShape1(shape);check.SelfInterMode()=true;
        check.StopOnFirstFaulty()=true;check.Perform();
        std::cout<<"Surface BSpline faulty="<<check.HasFaulty()<<" errors="<<check.HasErrors()<<std::endl;
        for(const auto& fault:check.GetCheckResult())std::cout<<"status="<<int(fault.GetCheckStatus())<<std::endl;
        return check.HasFaulty()?12:0;
    }
    if(argc==3&&std::string(argv[2])=="--largest") {
        TopoDS_Shape largest;double best=-1;
        for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next()) {
            GProp_GProps p;BRepGProp::VolumeProperties(e.Current(),p);
            if(p.Mass()>best){best=p.Mass();largest=e.Current();}
        }
        if(largest.IsNull())return 6;
        BRepTools::Write(largest,(path+".largest.brep").c_str());std::cout<<"largest fast volume="<<best<<std::endl;return 0;
    }
    GProp_GProps p;BRepGProp::VolumeProperties(shape,p);
    std::cout<<"signed_volume_mm3="<<p.Mass()<<"\nvalid="<<BRepCheck_Analyzer(shape).IsValid()<<'\n';
    int solids=0,faces=0;
    for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next())++faces;
    for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next()) {
        auto s=TopoDS::Solid(e.Current());BRepGProp::VolumeProperties(s,p);
        std::cout<<"solid_"<<++solids<<"_signed_volume="<<p.Mass()<<'\n';
        // Orient only an analysis copy; never replace input or write a repaired file.
        const bool orientable=BRepLib::OrientClosedSolid(s);BRepGProp::VolumeProperties(s,p);
        std::cout<<"solid_"<<solids<<"_orientable="<<orientable<<"\nsolid_"<<solids<<"_oriented_volume="<<p.Mass()<<'\n';
    }
    std::cout<<"solids="<<solids<<"\nfaces="<<faces<<'\n';
    TopTools_IndexedMapOfShape edgeMap;TopExp::MapShapes(shape,TopAbs_EDGE,edgeMap);
    std::vector<int> edgeUses(edgeMap.Extent()+1),boundaryUses(edgeMap.Extent()+1);
    for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next())
        for(TopExp_Explorer e(f.Current(),TopAbs_EDGE);e.More();e.Next()) {
            const auto index=edgeMap.FindIndex(e.Current());++edgeUses[index];
            if(e.Current().Orientation()==TopAbs_FORWARD||e.Current().Orientation()==TopAbs_REVERSED)++boundaryUses[index];
        }
    for(int i=1;i<=edgeMap.Extent();++i)
        if(!BRep_Tool::Degenerated(TopoDS::Edge(edgeMap(i)))&&edgeUses[i]!=2)
            std::cout<<"edge="<<i<<" uses="<<edgeUses[i]<<" boundaryUses="<<boundaryUses[i]<<std::endl;
    BOPAlgo_ArgumentAnalyzer analyzer;analyzer.SetShape1(shape);
    analyzer.SelfInterMode()=true;analyzer.StopOnFirstFaulty()=true;analyzer.Perform();
    std::cout<<"self_intersection_fault="<<analyzer.HasFaulty()<<'\n';
    for(const auto& fault:analyzer.GetCheckResult())std::cout<<"fault_status="<<int(fault.GetCheckStatus())<<'\n';
    const auto h=kachakacha::v2::kernel::StoreShape(shape);
    const auto check=kachakacha::v2::kernel::CheckSolidForExport(h,1e-6);
    if(check.HasValue())std::cout<<"app_export_accepts="<<(check.Value().closed&&check.Value().positiveVolume&&check.Value().selfIntersectionFree)
        <<" accurate_volume="<<std::setprecision(17)<<check.Value().volumeMm3<<'\n';
    if(argc==3&&std::string(argv[2])=="--nurbs") {
        BRepBuilderAPI_NurbsConvert n(shape,true);
        std::cout<<"NURBS valid="<<BRepCheck_Analyzer(n.Shape()).IsValid()<<std::endl;
        BRepTools::Write(n.Shape(),(path+".nurbs.brep").c_str());
        ShapeFix_Shape fix(n.Shape());fix.SetPrecision(1e-7);fix.SetMaxTolerance(1e-7);fix.Perform();
        const auto fixed=fix.Shape();
        std::cout<<"NURBS parameter fix valid="<<BRepCheck_Analyzer(fixed).IsValid()<<std::endl;
        BRepTools::Write(fixed,(path+".nurbs-fixed.brep").c_str());
        BOPAlgo_ArgumentAnalyzer a;a.SetShape1(fixed);a.SelfInterMode()=true;a.StopOnFirstFaulty()=true;a.Perform();
        std::cout<<"fixed faulty="<<a.HasFaulty()<<std::endl;
        for(const auto& fault:a.GetCheckResult())std::cout<<"fixed fault="<<int(fault.GetCheckStatus())<<std::endl;
        return 0;
    }
    if(argc==3) {
        std::cout<<"Output format="<<(std::string(argv[2]).ends_with(".step")?"STEP":"STL")<<std::endl;
        const auto stl=std::string(argv[2]).ends_with(".step")
            ?kachakacha::v2::kernel::OutputStep({h})
            :kachakacha::v2::kernel::OutputStl({h},.01);
        if(!stl.HasValue()){for(const auto& d:stl.Diagnostics())std::cout<<d.summaryJa<<" / "<<d.detailsJa<<'\n';return 5;}
        std::ofstream out(argv[2],std::ios::binary);out<<stl.Value();
    }
    return 0;
    }catch(const Standard_Failure& failure){std::cout<<"OCCT exception: "<<failure.GetMessageString()<<std::endl;return 7;}
    catch(const std::exception& failure){std::cout<<"Exception: "<<failure.what()<<std::endl;return 8;}
}
