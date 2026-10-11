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
#include <IFSelect_ReturnStatus.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <BRepLib.hxx>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc,char** argv)
{
    if(argc!=2){std::cerr<<"usage: inspect_brep input.brep|input.step\n";return 2;}
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
    BOPAlgo_ArgumentAnalyzer analyzer;analyzer.SetShape1(shape);
    analyzer.SelfInterMode()=true;analyzer.StopOnFirstFaulty()=true;analyzer.Perform();
    std::cout<<"self_intersection_fault="<<analyzer.HasFaulty()<<'\n';
    for(const auto& fault:analyzer.GetCheckResult())std::cout<<"fault_status="<<int(fault.GetCheckStatus())<<'\n';
    const auto h=kachakacha::v2::kernel::StoreShape(shape);
    const auto check=kachakacha::v2::kernel::CheckSolidForExport(h,1e-6);
    if(check.HasValue())std::cout<<"app_export_accepts="<<(check.Value().closed&&check.Value().positiveVolume&&check.Value().selfIntersectionFree)<<'\n';
    return 0;
}
