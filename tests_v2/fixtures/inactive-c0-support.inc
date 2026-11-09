// Test fixture only: a smooth active patch with a genuine crease outside it.
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepLib.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_BSplineSurface.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Dir2d.hxx>
#include <gp_Pnt2d.hxx>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace kachakacha_test_fixture {
inline TopoDS_Face InactiveC0Support(bool creaseInside, bool rational, bool holes,
    double scale=1, bool nearKnot=false, double vMinimum=0)
{
    NCollection_Array2<gp_Pnt> poles(1,5,1,2);
    NCollection_Array2<double> weights(1,5,1,2);
    const std::vector<gp_Pnt> profile{{0,0,0},{1,0,0},{2,0,0},{3,0,1},{2,0,2}};
    for(int u=1;u<=5;++u)for(int v=1;v<=2;++v){
        const auto& p=profile[static_cast<std::size_t>(u-1)];
        poles.SetValue(u,v,gp_Pnt(p.X()*scale,(v-1)*4.*scale,p.Z()*scale));
        weights.SetValue(u,v,rational&&u==4?.8:1.);
    }
    NCollection_Array1<double> uk(1,3),vk(1,2);
    NCollection_Array1<int> um(1,3),vm(1,2);
    uk.SetValue(1,0);uk.SetValue(2,1);uk.SetValue(3,2);
    um.SetValue(1,3);um.SetValue(2,2);um.SetValue(3,3);
    vk.SetValue(1,0);vk.SetValue(2,1);vm.SetValue(1,2);vm.SetValue(2,2);
    const occ::handle<Geom_BSplineSurface> support=new Geom_BSplineSurface(
        poles,weights,uk,vk,um,vm,2,1,false,false);
    if(nearKnot)support->InsertUKnot(1.2,1,0);
    const double u0=creaseInside?.5:(nearKnot?std::nextafter(1.2,2.):1.2),u1=creaseInside?1.5:1.8;
    const auto loop=[&](const std::vector<gp_Pnt2d>& rectangle){
        BRepBuilderAPI_MakeWire wire;
        for(std::size_t i=0;i<rectangle.size();++i){
            const auto a=rectangle[i],b=rectangle[(i+1)%rectangle.size()];
            const occ::handle<Geom2d_Line> line=new Geom2d_Line(a,gp_Dir2d(b.X()-a.X(),b.Y()-a.Y()));
            BRepBuilderAPI_MakeEdge edge(line,support,0,a.Distance(b));
            if(!edge.IsDone())throw std::runtime_error("fixture trim edge failed");
            wire.Add(edge.Edge());
        }
        if(!wire.IsDone())throw std::runtime_error("fixture trim loop failed");
        return wire.Wire();
    };
    BRepBuilderAPI_MakeFace maker(support,
        loop({{u0,vMinimum},{u1,vMinimum},{u1,1},{u0,1}}),true);
    if(holes)for(const auto& rectangle:std::vector<std::vector<gp_Pnt2d>>{
        {{1.28,.15},{1.28,.4},{1.4,.4},{1.4,.15}},
        {{1.6,.6},{1.6,.85},{1.73,.85},{1.73,.6}}})maker.Add(loop(rectangle));
    auto face=maker.Face();BRepLib::BuildCurves3d(face);
    if(!BRepCheck_Analyzer(face).IsValid())throw std::runtime_error("invalid inactive-C0 fixture");
    return face;
}
}
