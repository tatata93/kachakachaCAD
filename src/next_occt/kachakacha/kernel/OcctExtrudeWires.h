#pragma once

// Native topology only: render tessellation must never become an editable wire.
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctBoundaryCurves.h"
#include "kachakacha/kernel/OcctExtrude.h"
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_MapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>

namespace kachakacha::v2::kernel::detail {

template<class Operation>
inline TopTools_MapOfShape FollowFaces(Operation& operation, const TopTools_MapOfShape& before)
{
    TopTools_MapOfShape after;
    for (TopTools_MapOfShape::Iterator it(before); it.More(); it.Next()) {
        if (!operation.IsDeleted(it.Key())) after.Add(it.Key());
        for (const auto& shape : operation.Modified(it.Key())) after.Add(shape);
        for (const auto& shape : operation.Generated(it.Key()))
            if (shape.ShapeType() == TopAbs_FACE) after.Add(shape);
    }
    return after;
}

inline base::Result<bool> ExtrudeWires(const TopoDS_Shape& solid,
    const TopTools_MapOfShape& starts, const TopTools_MapOfShape& ends,
    double tolerance, ExtrudedPart& part, ExtrudeBuildResult& result)
{
    using Out = base::Result<bool>;
    TopTools_MapOfShape capEdges, seen;
    for (TopExp_Explorer faces(solid, TopAbs_FACE); faces.More(); faces.Next()) {
        const bool start = starts.Contains(faces.Current());
        const bool end = ends.Contains(faces.Current());
        part.faceKeys.push_back(start ? "start" : end ? "end" : "side");
        if (!start && !end) continue;
        const auto face = TopoDS::Face(faces.Current());
        for (TopExp_Explorer wires(face, TopAbs_WIRE); wires.More(); wires.Next()) {
            std::vector<geometry::CurveSegment> loop;
            for (BRepTools_WireExplorer edges(TopoDS::Wire(wires.Current()), face); edges.More(); edges.Next()) {
                const auto edge = edges.Current();
                capEdges.Add(edge);
                if (BRep_Tool::Degenerated(edge)) continue;
                const auto curves = BoundaryCurves(edge, tolerance);
                if (!curves.HasValue()) return Out::Failure(curves.Diagnostics());
                loop.insert(loop.end(), curves.Value().curves.begin(), curves.Value().curves.end());
            }
            if (!loop.empty()) (start ? result.startProfileWires : result.endProfileWires).push_back(std::move(loop));
        }
    }
    for (TopExp_Explorer edges(solid, TopAbs_EDGE); edges.More(); edges.Next()) {
        if (capEdges.Contains(edges.Current()) || !seen.Add(edges.Current())) continue;
        const auto edge = TopoDS::Edge(edges.Current());
        if (BRep_Tool::Degenerated(edge)) continue;
        const auto curves = BoundaryCurves(edge, tolerance);
        if (!curves.HasValue()) return Out::Failure(curves.Diagnostics());
        result.sideBoundaryWires.push_back(curves.Value().curves);
    }
    return Out::Success(true);
}
} // namespace kachakacha::v2::kernel::detail
#endif
