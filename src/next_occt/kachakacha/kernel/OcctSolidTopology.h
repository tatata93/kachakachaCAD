#pragma once

#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shape.hxx>

namespace kachakacha::v2::kernel::detail {

// A chain of single-child containers describes the same solid with the
// accumulated orientation and location. Keep empty and mixed shapes intact.
[[nodiscard]] inline TopoDS_Shape UnwrapSingleSolid(const TopoDS_Shape& source)
{
    auto shape = source;
    while (shape.ShapeType() == TopAbs_COMPOUND || shape.ShapeType() == TopAbs_COMPSOLID) {
        TopoDS_Iterator children(shape, true, true);
        if (!children.More()) return source;
        const auto child = children.Value();
        children.Next();
        if (children.More()) return source;
        shape = child;
    }
    if (shape.ShapeType() != TopAbs_SOLID
        || (shape.Orientation() != TopAbs_FORWARD && shape.Orientation() != TopAbs_REVERSED)) {
        return source;
    }
    return shape;
}

} // namespace kachakacha::v2::kernel::detail
