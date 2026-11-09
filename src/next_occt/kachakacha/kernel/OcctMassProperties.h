#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
class TopoDS_Shape;
class GProp_GProps;
class gp_Pnt;
namespace kachakacha::v2::kernel::detail {
// Signed volume and centroid. Throws if the requested integration accuracy
// cannot be established; never substitutes triangulation for the native shape.
// Related offset operands may use one local flux origin for material comparisons.
// Omitting it retains the established per-shape measurement convention.
void VolumeProperties(const TopoDS_Shape& shape, GProp_GProps& properties, bool centroid=false,
    const gp_Pnt* offsetReference=nullptr);
}
#endif
