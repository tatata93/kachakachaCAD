#include "kachakacha/io/PlanarExport.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using kachakacha::geometry::Vector3;
using kachakacha::io::WireLiesOnWorkPlane;
using kachakacha::io::WritePlanarDxf;
using kachakacha::io::WritePlanarSvg;
using kachakacha::model::NamedWire;
using kachakacha::model::Wire;
using kachakacha::model::WorkPlane;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main()
{
    const WorkPlane plane = WorkPlane::FromPointNormal({10.0, 20.0, 30.0}, {0.0, 0.0, 1.0});
    NamedWire construction{"centerline", Wire::Line(
        plane.ToWorld(-100.0, 5.0), plane.ToWorld(100.0, 5.0))};
    construction.metadata.construction = true;
    const std::vector<NamedWire> wires = {
        {"outline", Wire::Polyline({
             plane.ToWorld(0.0, 0.0),
             plane.ToWorld(20.0, 0.0),
             plane.ToWorld(20.0, 10.0),
             plane.ToWorld(0.0, 10.0),
             plane.ToWorld(0.0, 0.0),
         })},
        {"window", Wire::Circle(plane.ToWorld(10.0, 5.0), plane.UAxis(), plane.VAxis(), 2.0)},
        {"curve", Wire::CubicBezier(
             plane.ToWorld(2.0, 2.0),
             plane.ToWorld(5.0, 8.0),
             plane.ToWorld(15.0, 8.0),
             plane.ToWorld(18.0, 2.0))},
        {"spline", Wire::InterpolatingCubicBSpline({
             plane.ToWorld(2.0, 4.0),
             plane.ToWorld(6.0, 7.0),
             plane.ToWorld(12.0, 3.0),
             plane.ToWorld(18.0, 6.0)})},
        construction,
    };

    std::ostringstream svg;
    WritePlanarSvg(svg, plane, wires);
    Require(svg.str().find("width=\"30.000000mm\"") != std::string::npos, "SVG preserves millimeter width");
    Require(svg.str().find("height=\"20.000000mm\"") != std::string::npos, "SVG preserves millimeter height");
    Require(svg.str().find("<polyline") != std::string::npos, "SVG contains cutting paths");

    std::ostringstream dxf;
    WritePlanarDxf(dxf, plane, wires);
    Require(dxf.str().find("$INSUNITS\n70\n4") != std::string::npos, "DXF declares millimeter units");
    Require(dxf.str().find("POLYLINE") != std::string::npos, "DXF contains polylines");
    Require(dxf.str().find("VERTEX") != std::string::npos, "DXF contains vertices");
    Require(WireLiesOnWorkPlane(wires[3].wire, plane), "B-spline remains on export plane");

    bool constructionOnlyRejected = false;
    try {
        std::ostringstream constructionOnly;
        WritePlanarSvg(constructionOnly, plane, {construction});
    } catch (const std::invalid_argument&) {
        constructionOnlyRejected = true;
    }
    Require(constructionOnlyRejected, "construction-only export is rejected");

    const Wire offPlane = Wire::Line(plane.ToWorld(0.0, 0.0), plane.ToWorld(1.0, 1.0, 0.1));
    Require(!WireLiesOnWorkPlane(offPlane, plane), "off-plane wire is rejected");
    bool threw = false;
    try {
        std::ostringstream invalid;
        WritePlanarSvg(invalid, plane, {{"off", offPlane}});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Require(threw, "export rejects an off-plane wire");

    std::cout << "planar export tests passed\n";
    return EXIT_SUCCESS;
}
