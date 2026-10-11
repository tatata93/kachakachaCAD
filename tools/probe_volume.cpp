// Read-only comparison of OCCT integration methods for spline-heavy solids.
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Shape.hxx>
#include <ShapeUpgrade_ShapeDivideContinuity.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <Standard_Failure.hxx>
#include <BRepGProp_VinertGK.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepGProp_Domain.hxx>
#include <TopoDS.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    std::cout.precision(17);
    for(int i=1;i<argc;++i) {
        TopoDS_Shape shape;BRep_Builder builder;std::ifstream in(argv[i]);BRepTools::Read(shape,in,builder);
        if(shape.IsNull())return 2;
        if(argc>2&&std::string(argv[2])=="faces") {
            GProp_GProps total;int index=0;double absoluteError=0;int invalid=0;
            for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()) {
                BRepBuilderAPI_Copy copy(e.Current(),true,false);
                ShapeUpgrade_ShapeDivideContinuity split(copy.Shape());
                split.SetSurfaceCriterion(GeomAbs_C3);split.SetBoundaryCriterion(GeomAbs_C3);
                split.SetPCurveCriterion(GeomAbs_C3);split.SetTolerance(1e-7);split.Perform();
                const auto divided=split.Result();GProp_GProps part;double error=0;
                for(TopExp_Explorer leaf(divided,TopAbs_FACE);leaf.More();leaf.Next()) {
                    const auto face=TopoDS::Face(leaf.Current());
                    BRepGProp_VinertGK integral;integral.SetLocation(gp_Pnt(67.85,0,20));
                    BRepGProp_Face surface(true);surface.Load(face);
                    double status;
                    if(face.NbChildren()==0)status=integral.Perform(surface,1e-11,false,false);
                    else {BRepGProp_Domain domain;domain.Init(face);status=integral.Perform(surface,domain,1e-11,false,false);}
                    if(status<0)return 7;
                    part.Add(integral);error+=status;
                }
                if(!BRepCheck_Analyzer(divided).IsValid())++invalid;
                std::cout<<"face="<<++index<<" valid="<<BRepCheck_Analyzer(divided).IsValid()
                    <<" mass="<<part.Mass()<<" error="<<error<<std::endl;
                total.Add(part);absoluteError+=error;
            }
            std::cout<<"face total="<<total.Mass()<<" relativeError="<<absoluteError/std::abs(total.Mass())<<" invalid="<<invalid<<std::endl;
            return 0;
        }
        GProp_GProps fast,adaptive,spans;
        BRepGProp::VolumeProperties(shape,fast);
        const double gaussError=BRepGProp::VolumeProperties(shape,adaptive,1e-10);
        const double spanError=BRepGProp::VolumePropertiesGK(shape,spans,1e-10,false,true,false,false);
        std::cout<<argv[i]<<" fast="<<fast.Mass()<<" adaptive="<<adaptive.Mass()<<" adaptive_error="<<gaussError
            <<" spans="<<spans.Mass()<<" span_error="<<spanError<<'\n';std::cout.flush();
        for(double eps:{1e-11,1e-12,1e-13}) {
            GProp_GProps tight;const double error=BRepGProp::VolumePropertiesGK(shape,tight,eps,false,true,false,false);
            std::cout<<"eps="<<eps<<" volume="<<tight.Mass()<<" error="<<error<<std::endl;
        }
        ShapeUpgrade_ShapeDivideContinuity split(shape);split.SetSurfaceCriterion(GeomAbs_C3);
        split.SetBoundaryCriterion(GeomAbs_C3);split.SetPCurveCriterion(GeomAbs_C3);split.SetTolerance(1e-7);
        split.Perform();const auto divided=split.Result();GProp_GProps pieces;
        const double pieceError=BRepGProp::VolumePropertiesGK(divided,pieces,1e-10,false,true,false,false);
        int faces=0;for(TopExp_Explorer e(divided,TopAbs_FACE);e.More();e.Next())++faces;
        std::cout<<"split_volume="<<pieces.Mass()<<" split_error="<<pieceError<<" faces="<<faces
            <<" valid="<<BRepCheck_Analyzer(divided).IsValid()<<'\n';std::cout.flush();
    }
}
