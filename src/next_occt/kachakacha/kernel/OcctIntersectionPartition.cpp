#include "kachakacha/kernel/OcctIntersectionPartition.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include <Geom_BSplineSurface.hxx>
#include <ShapeExtend.hxx>
#include <ShapeUpgrade_FaceDivide.hxx>
#include <ShapeUpgrade_ShapeDivide.hxx>
#include <ShapeUpgrade_SplitSurface.hxx>
#include <TopoDS_Shape.hxx>
#include <TopExp_Explorer.hxx>
#include <algorithm>
#include <vector>

namespace kachakacha::v2::kernel::detail {
namespace {
class BoundedKnotSplit final : public ShapeUpgrade_SplitSurface {
    static void Split(occ::handle<NCollection_HSequence<double>>& values,
        const NCollection_Array1<double>& knots, int poles) {
        if (poles <= 64) return;
        const double lo=values->Value(1), hi=values->Value(values->Length());
        std::vector<double> locations{lo,hi};
        // Twelve distinct knot intervals is a measured balance between solving
        // giant splines and producing too many pairs of neighbouring faces.
        const int stride=std::max(12,(knots.Length()+14)/15);
        for (int i=1+stride;i<knots.Length();i+=stride)
            if (knots(i)>lo && knots(i)<hi) locations.push_back(knots(i));
        std::sort(locations.begin(),locations.end());
        values->Clear();for (double value:locations) values->Append(value);
    }
    void Compute(const bool) override {
        const auto spline=occ::handle<Geom_BSplineSurface>::DownCast(mySurface);
        if (!spline.IsNull()) {
            Split(myUSplitValues,spline->UKnots(),spline->NbUPoles());
            Split(myVSplitValues,spline->VKnots(),spline->NbVPoles());
        }
        myNbResultingRow=myUSplitValues->Length()-1;
        myNbResultingCol=myVSplitValues->Length()-1;
        if (myNbResultingRow*myNbResultingCol>64) {
            for (auto values:{myUSplitValues,myVSplitValues}) {
                const double first=values->First(),last=values->Last();values->Clear();values->Append(first);values->Append(last);
            }
            myNbResultingRow=myNbResultingCol=1;
        }
        myStatus=ShapeExtend::EncodeStatus(myNbResultingRow*myNbResultingCol>1?ShapeExtend_DONE1:ShapeExtend_OK);
    }
};
}
TopoDS_Shape PartitionIntersectionFaces(const TopoDS_Shape& shape) {
    int count=0;for (TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next())++count;
    if (count>256) return shape;
    ShapeUpgrade_ShapeDivide split(shape);
    const occ::handle<ShapeUpgrade_FaceDivide> divider=new ShapeUpgrade_FaceDivide;
    divider->SetSplitSurfaceTool(new BoundedKnotSplit);
    split.SetSplitFaceTool(divider);split.SetPrecision(1e-7);split.SetMaxTolerance(1e-6);
    split.SetSurfaceSegmentMode(true);split.Perform();
    return split.Result();
}
}
#endif
