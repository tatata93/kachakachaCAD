#include "kachakacha/kernel/OcctMassProperties.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <BRepGProp.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_ShapeDivideContinuity.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace kachakacha::v2::kernel::detail {
namespace {
bool Accurate(double error,const GProp_GProps& p) {
    return std::isfinite(error)&&error>=0&&error<=1e-8&&std::isfinite(p.Mass());
}
TopoDS_Shape DividedCopy(const TopoDS_Shape& shape) {
    BRepBuilderAPI_Copy copy(shape,true,false);
    ShapeUpgrade_ShapeDivideContinuity split(copy.Shape());
    split.SetSurfaceCriterion(GeomAbs_C3);split.SetBoundaryCriterion(GeomAbs_C3);
    split.SetPCurveCriterion(GeomAbs_C3);split.SetTolerance(1e-7);split.Perform();
    return split.Result();
}
}
void VolumeProperties(const TopoDS_Shape& shape,GProp_GProps& properties,bool centroid) {
    GProp_GProps measured;
    bool composite=false;
    for(TopExp_Explorer e(shape,TopAbs_FACE);e.More()&&!composite;e.Next()) {
        BRepAdaptor_Surface surface(TopoDS::Face(e.Current()));
        composite=surface.NbUIntervals(GeomAbs_C3)>1||surface.NbVIntervals(GeomAbs_C3)>1;
    }
    if(composite) {
        // Avoid first integrating blindly across known spline continuity breaks.
        // This is the same exact analysis subdivision as the fallback below.
        const auto divided=DividedCopy(shape);
        if(!divided.IsNull()&&BRepCheck_Analyzer(divided).IsValid()) {
            const double error=BRepGProp::VolumePropertiesGK(divided,measured,1e-10,false,true,centroid,false);
            if(Accurate(error,measured)){properties=measured;return;}
            measured=GProp_GProps();
        }
    }
    double error=BRepGProp::VolumePropertiesGK(shape,measured,1e-10,false,true,centroid,false);
    if(!Accurate(error,measured)) {
        GProp_GProps adaptive;
        const double adaptiveError=BRepGProp::VolumeProperties(shape,adaptive,1e-10);
        if(Accurate(adaptiveError,adaptive)&&std::isfinite(measured.Mass())
            &&std::abs(adaptive.Mass()-measured.Mass())<=std::max(1e-10,std::abs(adaptive.Mass())*1e-8)) {
            properties=adaptive;return;
        }
        // IsUseSpan only handles BSpline surfaces, not a revolution whose
        // meridian is a spline. Split an analysis copy at C3 breaks so those
        // integration intervals are respected too. Stored geometry is untouched.
        const auto divided=DividedCopy(shape);
        if(divided.IsNull()||!BRepCheck_Analyzer(divided).IsValid()) {
            std::ostringstream message;message<<std::setprecision(17)
                <<"Volume integration subdivision is invalid; source_valid="<<BRepCheck_Analyzer(shape).IsValid()
                <<" GK="<<measured.Mass()<<" error="<<error
                <<" adaptive="<<adaptive.Mass()<<" error="<<adaptiveError;
            throw Standard_Failure(message.str().c_str());
        }
        measured=GProp_GProps();
        error=BRepGProp::VolumePropertiesGK(divided,measured,1e-10,false,true,centroid,false);
    }
    if(!Accurate(error,measured))
        throw Standard_Failure("Volume integration did not reach relative accuracy 1e-8");
    properties=measured;
}
}
#endif
