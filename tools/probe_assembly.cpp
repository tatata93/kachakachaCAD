// Read-only diagnostic: inputs are never altered; failed intermediate is saved.
#include "kachakacha/kernel/OcctMassProperties.h"
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <TopTools_ListOfShape.hxx>
#include <chrono>
int main(int argc,char** argv) {
    if(argc!=2&&argc!=3)return 2;const std::filesystem::path dir(argv[1]);
    std::vector<std::filesystem::path> inputs;
    for(const auto& e:std::filesystem::directory_iterator(dir))
        if(e.path().filename().string().starts_with("ideal-part-")&&e.path().extension()==".brep")inputs.push_back(e.path());
    std::sort(inputs.begin(),inputs.end());TopoDS_Shape accumulated;std::cout.precision(17);
    if(argc==3&&(std::string(argv[2])=="batch"||std::string(argv[2])=="batch-raw"||std::string(argv[2])=="incremental")) {
        const auto start=std::chrono::steady_clock::now();
        try {
            TopTools_ListOfShape arguments,tools;double sum=0,largest=0;
            for(const auto& path:inputs) {
                std::cout<<"read/mass "<<path.filename().string()<<std::endl;
                TopoDS_Shape shape;BRep_Builder b;std::ifstream in(path);BRepTools::Read(shape,in,b);
                GProp_GProps p; kachakacha::v2::kernel::detail::VolumeProperties(shape,p);
                sum+=p.Mass();largest=std::max(largest,p.Mass());
                if(arguments.IsEmpty())arguments.Append(shape);else tools.Append(shape);
            }
            TopoDS_Shape result;
            if(std::string(argv[2])=="incremental") {
                result=arguments.First();int n=1;
                for(const auto& tool:tools) {
                    std::cout<<"incremental fuse "<<++n<<std::endl;
                    TopTools_ListOfShape base,addition;base.Append(result);addition.Append(tool);
                    BRepAlgoAPI_Fuse fuse;fuse.SetArguments(base);fuse.SetTools(addition);
                    fuse.SetNonDestructive(true);fuse.Build();
                    if(!fuse.IsDone()){fuse.DumpErrors(std::cout);return 4;}
                    fuse.SimplifyResult(true,true);result=fuse.Shape();
                }
            } else {
                BRepAlgoAPI_Fuse fuse;fuse.SetArguments(arguments);fuse.SetTools(tools);
                std::cout<<"batch fuse start"<<std::endl;
                fuse.SetNonDestructive(true);fuse.Build();
                std::cout<<"batch fuse complete; simplify"<<std::endl;
                if(!fuse.IsDone()){fuse.DumpErrors(std::cout);return 4;}
                if(std::string(argv[2])!="batch-raw")fuse.SimplifyResult(true,true);
                result=fuse.Shape();
            }
            std::cout<<"simplify complete; volume"<<std::endl;
            BRepTools::Write(result,(dir/"unchecked-union.brep").string().c_str());
            GProp_GProps volume;
            kachakacha::v2::kernel::detail::VolumeProperties(result,volume);
            std::cout<<"batch parts="<<inputs.size()<<" volume="<<volume.Mass()<<" valid="<<BRepCheck_Analyzer(result).IsValid()<<std::endl;
            const auto handle=kachakacha::v2::kernel::StoreShape(result);
            const auto check=kachakacha::v2::kernel::CheckSolidForExport(handle,1e-6);
            const bool ok=check.HasValue()&&check.Value().Ok()
                &&volume.Mass()>=largest-sum*1e-7&&volume.Mass()<=sum*(1+1e-7);
            BRepTools::Write(result,(dir/"batch-union.brep").string().c_str());
            std::cout<<"app_ok="<<ok<<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<std::endl;
            return ok?0:5;
        }catch(const Standard_Failure& e){std::cout<<e.GetMessageString()<<std::endl;return 6;}
    }
    try {for(const auto& path:inputs) {
        TopoDS_Shape shape;BRep_Builder b;std::ifstream in(path);BRepTools::Read(shape,in,b);if(shape.IsNull())return 3;
        if(argc==3&&std::string(argv[2])=="nurbs") {
            try {
                BRepBuilderAPI_NurbsConvert conversion(shape,true);
                if(conversion.IsDone()&&BRepCheck_Analyzer(conversion.Shape()).IsValid()) {
                    GProp_GProps a,b; kachakacha::v2::kernel::detail::VolumeProperties(shape,a);
                    kachakacha::v2::kernel::detail::VolumeProperties(conversion.Shape(),b);
                    if(std::abs(a.Mass()-b.Mass())>std::max(1e-7,std::abs(a.Mass())*1e-8))return 9;
                    shape=conversion.Shape();std::cout<<"Normalized "<<path.filename().string()<<std::endl;
                }else std::cout<<"NURBS invalid; retained original "<<path.filename().string()<<std::endl;
            }catch(const Standard_Failure& e){std::cout<<"NURBS skipped: "<<e.GetMessageString()<<std::endl;}
        }
        if(accumulated.IsNull()){accumulated=shape;continue;}
        std::cout<<"Adding "<<path.filename().string()<<std::endl;
        BRepAlgoAPI_Fuse fuse(accumulated,shape);
        if(argc==3&&std::string(argv[2])=="fuzzy")fuse.SetFuzzyValue(1e-6);
        fuse.Build();if(!fuse.IsDone())return 4;
        if(argc!=3||std::string(argv[2])!="raw")fuse.SimplifyResult(true,true);const auto result=fuse.Shape();
        GProp_GProps volume; kachakacha::v2::kernel::detail::VolumeProperties(result,volume);
        const auto handle=kachakacha::v2::kernel::StoreShape(result);
        const auto check=kachakacha::v2::kernel::CheckSolidForExport(handle,1e-6);
        std::cout<<"volume="<<volume.Mass()<<" valid="<<BRepCheck_Analyzer(result).IsValid()
            <<" app_ok="<<(check.HasValue()&&check.Value().Ok())<<std::endl;
        kachakacha::v2::kernel::ReleaseShape(handle);
        if(!check.HasValue()||!check.Value().Ok()) {
            BRepTools::Write(result,(dir/"failed-union.brep").string().c_str());
            BRepTools::Write(accumulated,(dir/"before-failure.brep").string().c_str());
            BOPAlgo_ArgumentAnalyzer a;a.SetShape1(result);a.SelfInterMode()=true;a.StopOnFirstFaulty()=true;a.Perform();
            for(const auto& fault:a.GetCheckResult())std::cout<<"fault_status="<<int(fault.GetCheckStatus())<<std::endl;
            return 5;
        }
        accumulated=result;
    }}catch(const Standard_Failure& e){std::cout<<e.GetMessageString()<<std::endl;return 6;}
    BRepTools::Write(accumulated,(dir/"complete-union.brep").string().c_str());return 0;
}
