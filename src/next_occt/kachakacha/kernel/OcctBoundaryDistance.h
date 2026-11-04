#pragma once
#ifdef KACHACAD_V2_WITH_OCCT
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <array>
#include <optional>
#include <vector>

namespace kachakacha::v2::kernel::detail {
// A fast upper bound, accepted only for numerical coincidence on a finite native
// edge or trimmed face. Failed local searches always require full BRep distance.
class NativeBoundaryDistance {
public:
    explicit NativeBoundaryDistance(const TopoDS_Shape& shape);
    [[nodiscard]] std::optional<double> Coincident(const gp_Pnt& point, double precision) const;
private:
    struct Edge {
        BRepAdaptor_Curve curve;
        std::array<gp_Pnt,17> samples;
        std::array<double,17> parameters;
    };
    std::vector<Edge> edges_;
    struct Face {
        TopoDS_Face face;
        BRepAdaptor_Surface surface;
        std::array<gp_Pnt,81> samples;
        std::array<gp_Pnt2d,81> parameters;
    };
    std::vector<Face> faces_;
};
}
#endif
