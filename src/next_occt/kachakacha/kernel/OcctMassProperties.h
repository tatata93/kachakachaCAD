#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
class TopoDS_Shape;
class GProp_GProps;
namespace kachakacha::v2::kernel::detail {
// Signed volume and centroid. Throws if the requested integration accuracy
// cannot be established; never substitutes triangulation for the native shape.
void VolumeProperties(const TopoDS_Shape& shape, GProp_GProps& properties, bool centroid=false);
}
#endif
