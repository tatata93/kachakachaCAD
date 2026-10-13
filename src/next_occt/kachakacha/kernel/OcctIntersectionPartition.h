#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include <TopoDS_Shape.hxx>
namespace kachakacha::v2::kernel::detail {
// Exact knot-domain subdivision of an analysis copy; never refits the source.
TopoDS_Shape PartitionIntersectionFaces(const TopoDS_Shape& shape);
}
#endif
