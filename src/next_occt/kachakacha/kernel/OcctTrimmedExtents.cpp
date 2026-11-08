#include "kachakacha/kernel/OcctTrimmedExtents.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <Bnd_Box.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <Standard_Failure.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <cmath>
#include <exception>

namespace kachakacha::v2::kernel::detail {
std::optional<double> TrimmedCoordinateExtent(const TopoDS_Shape& original,int component)
{
    if(original.IsNull()||component<0||component>5)return std::nullopt;
    try {
        // Native distance tools may cache data on subshapes. Keep this a
        // read-only validation of the editable/output shape.
        BRepBuilderAPI_Copy copy(original,true,false);const auto shape=copy.Shape();
        Bnd_Box enclosure;BRepBndLib::Add(shape,enclosure,false);
        if(enclosure.IsVoid()||enclosure.IsWhole())return std::nullopt;
        double bounds[6];enclosure.Get(bounds[0],bounds[1],bounds[2],bounds[3],bounds[4],bounds[5]);
        for(const auto coordinate:bounds)if(!std::isfinite(coordinate))return std::nullopt;
        const gp_Pnt low(bounds[0],bounds[1],bounds[2]),high(bounds[3],bounds[4],bounds[5]);
        const double margin=std::max(low.Distance(high),1.0),radius=margin*2+1;
        if(!std::isfinite(radius))return std::nullopt;
        const int axis=component%3;const bool maximum=component>=3;
        double origin[3];for(int i=0;i<3;++i)origin[i]=bounds[i]*.5+bounds[i+3]*.5;
        origin[axis]=bounds[component]+(maximum?margin:-margin);
        for(const auto coordinate:origin)if(!std::isfinite(coordinate))return std::nullopt;
        if(maximum?origin[axis]<=bounds[component]:origin[axis]>=bounds[component])return std::nullopt;
        const gp_Dir normal(axis==0?1:0,axis==1?1:0,axis==2?1:0);
        BRepBuilderAPI_MakeFace plane(gp_Pln(gp_Pnt(origin[0],origin[1],origin[2]),normal),
            -radius,radius,-radius,radius);
        if(!plane.IsDone())return std::nullopt;
        // The finite plane lies outside the conservative enclosure and covers
        // every projection of the shape. Its minimum distance is therefore the
        // requested coordinate extremum, including face interiors and holes.
        BRepExtrema_DistShapeShape distance(shape,plane.Face(),kTrimmedExtentPrecisionMm);
        if(!distance.IsDone()||distance.NbSolution()==0||distance.InnerSolution()
            ||!std::isfinite(distance.Value())||distance.Value()<0)return std::nullopt;
        const double value=origin[axis]+(maximum?-distance.Value():distance.Value());
        const auto& point=distance.PointOnShape1(1);
        if(!std::isfinite(value)||std::abs(point.Coord(axis+1)-value)>kTrimmedExtentPrecisionMm*4)
            return std::nullopt;
        return value;
    } catch(const Standard_Failure&) {return std::nullopt;}
      catch(const std::exception&) {return std::nullopt;}
}
}
#endif
