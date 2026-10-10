#include "kachakacha/kernel/OcctMatchedRuled.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <Geom_BSplineSurface.hxx>
#include <NCollection_Array2.hxx>
#include <NCollection_Array1.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopoDS_Shape.hxx>
#include <algorithm>

namespace kachakacha::v2::kernel::detail {
std::optional<base::Result<TopoDS_Shape>> BuildMatchedRuled(
    const modeling::GuideSurfaceRequest& request, const std::vector<std::size_t>& order,
    const geometry::GeometryTolerance& tolerance)
{
    using Out=base::Result<TopoDS_Shape>;
    if(!request.keepSectionOrder || order.size()!=2)return std::nullopt;
    const auto& a=request.chains[order[0]].segments;
    const auto& b=request.chains[order[1]].segments;
    if(a.empty() || a.size()!=b.size())return std::nullopt;
    const auto lines=[](const auto& segments){return std::all_of(segments.begin(),segments.end(),
        [](const auto& segment){return segment.Kind()==geometry::CurveKind::Line;});};
    if(!lines(a) || !lines(b))return std::nullopt;
    // A generated strip already contains the correspondence. Loft compatibility
    // may rotate/repartition these wires and connect unrelated outline vertices.
    const int count=static_cast<int>(a.size())+1;
    NCollection_Array2<gp_Pnt> poles(1,count,1,2);
    NCollection_Array1<double> knots(1,count), across(1,2);
    NCollection_Array1<int> multiplicities(1,count), acrossMult(1,2);
    across.SetValue(1,0);across.SetValue(2,1);acrossMult.Init(2);
    for(int i=1;i<=count;++i) {
        const auto p=i==count ? a.back().EndPoint() : a[static_cast<std::size_t>(i-1)].StartPoint();
        const auto q=i==count ? b.back().EndPoint() : b[static_cast<std::size_t>(i-1)].StartPoint();
        poles.SetValue(i,1,gp_Pnt(p.x,p.y,p.z));poles.SetValue(i,2,gp_Pnt(q.x,q.y,q.z));
        knots.SetValue(i,static_cast<double>(i-1)/(count-1));
        multiplicities.SetValue(i,(i==1 || i==count) ? 2 : 1);
    }
    const occ::handle<Geom_BSplineSurface> surface=new Geom_BSplineSurface(poles,knots,
        across,multiplicities,acrossMult,1,1);
    BRepBuilderAPI_MakeFace made(surface,tolerance.modelLinearMm);
    if(!made.IsDone() || !BRepCheck_Analyzer(made.Shape()).IsValid())return Out::Failure(
        base::MakeError("KER-S001","対応するレールから近似面を作れません。",
            "レールの交差や退化を確認してください。"));
    return Out::Success(made.Shape());
}
}
#endif
