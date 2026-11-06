#include "kachakacha/kernel/OcctSurfaceEdit.h"

#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctLoftSurface.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <Geom2d_Curve.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_ApproxSurface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_Surface.hxx>
#include <Precision.hxx>
#include <ShapeFix_ShapeTolerance.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace kachakacha::v2::kernel {
namespace {
using Spline = occ::handle<Geom_BSplineSurface>;
using Out = base::Result<SurfaceEditResult>;

[[nodiscard]] Out Failed(const std::string& summary, const std::string& detail = {})
{
    return Out::Failure(base::MakeError(kEditRefitFailed, summary, detail));
}

[[nodiscard]] std::string Mm(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
}

[[nodiscard]] int Poles(const Spline& surface)
{
    return surface->NbUPoles() * surface->NbVPoles();
}

[[nodiscard]] Spline Copy(const Spline& surface)
{
    return Spline::DownCast(surface->Copy());
}

// Knot removal keeps the native parameterization, including narrow knot spans.
// The final coefficient bound below checks the complete surface, not sampled points.
[[nodiscard]] Spline ReduceKnots(const Spline& source, double tolerance)
{
    Spline reduced = Copy(source);
    const double stepTolerance = tolerance / (source->NbUKnots() + source->NbVKnots());
    for (bool alongU : {true, false}) {
        const int first = alongU ? reduced->FirstUKnotIndex() : reduced->FirstVKnotIndex();
        const int last = alongU ? reduced->LastUKnotIndex() : reduced->LastVKnotIndex();
        for (int index = last - 1; index > first; --index) {
            const int multiplicity = alongU ? reduced->UMultiplicity(index)
                                           : reduced->VMultiplicity(index);
            for (int target = 0; target < multiplicity; ++target) {
                if (alongU ? reduced->RemoveUKnot(index, target, stepTolerance)
                           : reduced->RemoveVKnot(index, target, stepTolerance)) {
                    break;
                }
            }
        }
    }
    return reduced;
}

struct Knot {
    double parameter;
    int multiplicity;
};

[[nodiscard]] std::vector<Knot> CommonKnots(const Spline& a, const Spline& b, bool u)
{
    std::vector<Knot> knots;
    for (const auto& surface : {a, b}) {
        const int count = u ? surface->NbUKnots() : surface->NbVKnots();
        for (int index = 1; index <= count; ++index) {
            knots.push_back({u ? surface->UKnot(index) : surface->VKnot(index),
                u ? surface->UMultiplicity(index) : surface->VMultiplicity(index)});
        }
    }
    std::sort(knots.begin(), knots.end(), [](const Knot& x, const Knot& y) {
        return x.parameter < y.parameter;
    });
    std::vector<Knot> common;
    for (const auto& knot : knots) {
        if (!common.empty() && common.back().parameter == knot.parameter) {
            common.back().multiplicity = std::max(common.back().multiplicity, knot.multiplicity);
        } else {
            common.push_back(knot);
        }
    }
    return common;
}

// Equal positive B-spline basis functions form a partition of unity. For polynomial
// surfaces max |Pa-Pb| therefore bounds the distance at EVERY parameter. For rational
// surfaces use homogeneous numerator/weight differences, with a positive denominator.
// This is conservative, unlike sparse closest-point samples that can miss a whole ridge.
[[nodiscard]] double DeviationBound(const Spline& source, const Spline& candidate)
{
    constexpr double infinity = std::numeric_limits<double>::infinity();
    Spline a = Copy(source), b = Copy(candidate);
    const int degreeU = std::max(a->UDegree(), b->UDegree());
    const int degreeV = std::max(a->VDegree(), b->VDegree());
    a->IncreaseDegree(degreeU, degreeV);
    b->IncreaseDegree(degreeU, degreeV);
    const auto uKnots = CommonKnots(a, b, true);
    const auto vKnots = CommonKnots(a, b, false);
    // Bound temporary tensor refinement before allocating it.
    std::size_t uSize = 0, vSize = 0;
    for (const auto& knot : uKnots) uSize += static_cast<std::size_t>(knot.multiplicity);
    for (const auto& knot : vKnots) vSize += static_cast<std::size_t>(knot.multiplicity);
    if (uSize > 262144 || vSize > 262144 || uSize * vSize > 262144) return infinity;
    for (const auto& surface : {a, b}) {
        for (const auto& knot : uKnots) surface->InsertUKnot(knot.parameter, knot.multiplicity, 0, false);
        for (const auto& knot : vKnots) surface->InsertVKnot(knot.parameter, knot.multiplicity, 0, false);
    }
    if (a->NbUPoles() != b->NbUPoles() || a->NbVPoles() != b->NbVPoles()
        || a->NbUKnots() != b->NbUKnots() || a->NbVKnots() != b->NbVKnots()) return infinity;
    for (int i = 1; i <= a->NbUKnots(); ++i) {
        if (a->UKnot(i) != b->UKnot(i) || a->UMultiplicity(i) != b->UMultiplicity(i)) return infinity;
    }
    for (int j = 1; j <= a->NbVKnots(); ++j) {
        if (a->VKnot(j) != b->VKnot(j) || a->VMultiplicity(j) != b->VMultiplicity(j)) return infinity;
    }
    double scaleA = 0, scaleB = 0;
    for (int i = 1; i <= a->NbUPoles(); ++i) for (int j = 1; j <= a->NbVPoles(); ++j) {
        scaleA = std::max(scaleA, a->Weight(i, j));
        scaleB = std::max(scaleB, b->Weight(i, j));
    }
    if (!(scaleA > 0) || !(scaleB > 0)) return infinity;
    const gp_Pnt anchor = a->Pole(1, 1);
    double numerator = 0, weightDifference = 0, radius = 0, magnitude = 0, minWeight = 1;
    for (int i = 1; i <= a->NbUPoles(); ++i) for (int j = 1; j <= a->NbVPoles(); ++j) {
        const double wa = a->Weight(i, j) / scaleA, wb = b->Weight(i, j) / scaleB;
        if (!std::isfinite(wa) || !std::isfinite(wb) || !(wa > 0) || !(wb > 0)) return infinity;
        const gp_Vec pa(anchor, a->Pole(i, j)), pb(anchor, b->Pole(i, j));
        numerator = std::max(numerator, (wa * pa - wb * pb).Magnitude());
        weightDifference = std::max(weightDifference, std::abs(wa - wb));
        radius = std::max(radius, pb.Magnitude());
        for (const auto& point : {a->Pole(i, j), b->Pole(i, j)}) {
            if (!std::isfinite(point.X()) || !std::isfinite(point.Y())
                || !std::isfinite(point.Z())) return infinity;
            magnitude = std::max({magnitude, std::abs(point.X()), std::abs(point.Y()),
                std::abs(point.Z())});
        }
        minWeight = std::min(minWeight, wa);
    }
    const double bound = (numerator + radius * weightDifference) / minWeight;
    // Numerical margin for the exact degree elevation / knot insertion above.
    return bound + 64 * std::numeric_limits<double>::epsilon()
        * std::max(1.0, magnitude) * (uSize + vSize);
}

[[nodiscard]] int WireCount(const TopoDS_Face& face)
{
    int count = 0;
    for (TopExp_Explorer at(face, TopAbs_WIRE); at.More(); at.Next()) ++count;
    return count;
}

[[nodiscard]] TopoDS_Face TrimLike(const TopoDS_Face& original, const Spline& surface,
    double edgeTolerance)
{
    TopoDS_Face made;
    if (BRep_Tool::NaturalRestriction(original) && WireCount(original) == 1) {
        BRepBuilderAPI_MakeFace maker(surface, Precision::Confusion());
        if (maker.IsDone()) made = maker.Face();
    } else {
        // Updating boundary pcurves must never share the source's
        // topology or geometry, even when construction eventually fails.
        BRepBuilderAPI_Copy copy(original, true, false);
        if (!copy.IsDone()) return {};
        TopoDS_Face privateFace = TopoDS::Face(copy.Shape());
        privateFace.Orientation(TopAbs_FORWARD);
        ShapeFix_ShapeTolerance().LimitTolerance(privateFace, edgeTolerance);
        // The parameterization is unchanged. Keep the actual 2D trims instead of
        // reprojecting their 3D curves (which can pick a different branch on a fold).
        BRep_Builder builder;
        builder.MakeFace(made, surface, std::max(Precision::Confusion(), BRep_Tool::Tolerance(original)));
        for (TopExp_Explorer at(privateFace, TopAbs_WIRE); at.More(); at.Next()) {
            const TopoDS_Wire wire = TopoDS::Wire(at.Current());
            for (TopExp_Explorer edges(wire, TopAbs_EDGE); edges.More(); edges.Next()) {
                const TopoDS_Edge edge = TopoDS::Edge(edges.Current());
                double first = 0, last = 0;
                const auto curve = BRep_Tool::CurveOnSurface(edge, privateFace, first, last);
                if (curve.IsNull()) return {};
                if (BRep_Tool::IsClosed(edge, privateFace)) {
                    const auto forward = BRep_Tool::CurveOnSurface(
                        TopoDS::Edge(edge.Oriented(TopAbs_FORWARD)), privateFace, first, last);
                    const auto reversed = BRep_Tool::CurveOnSurface(
                        TopoDS::Edge(edge.Oriented(TopAbs_REVERSED)), privateFace, first, last);
                    if (forward.IsNull() || reversed.IsNull()) return {};
                    builder.UpdateEdge(edge, forward, reversed, made, edgeTolerance);
                } else {
                    builder.UpdateEdge(edge, curve, made, edgeTolerance);
                }
                builder.Range(edge, made, first, last);
            }
            builder.Add(made, wire);
        }
        builder.NaturalRestriction(made, BRep_Tool::NaturalRestriction(original));
    }
    if (!made.IsNull()) made.Orientation(original.Orientation());
    if (made.IsNull() || WireCount(made) != WireCount(original)
        || !BRepCheck_Analyzer(made).IsValid()) return {};
    return made;
}

[[nodiscard]] Out Refit(const modeling::KernelShapeHandle& source, double toleranceMm,
    const geometry::GeometryTolerance& tolerance)
{
    if (!std::isfinite(toleranceMm) || !(toleranceMm > 0)) {
        return Failed("許容は 0 より大きい有限の値にしてください。");
    }
    TopoDS_Shape native;
    if (!source.Valid() || !LookupShape(source, native)) {
        return Out::Failure(base::MakeError(kEditSourceMissing, "面の形が見つかりません。", {}));
    }
    TopoDS_Face face;
    for (TopExp_Explorer at(native, TopAbs_FACE); at.More(); at.Next()) {
        if (!face.IsNull()) return Failed("複数の面を含む形は、一度に整えられません。",
            "先頭の面だけを残す操作は行いません。元の形は変更していません。");
        face = TopoDS::Face(at.Current());
    }
    if (face.IsNull()) return Failed("選んだものに面がありません。");
    if (!BRepCheck_Analyzer(face).IsValid()) return Failed("元の面に不正な境界があります。",
        "整える前に境界を修正してください。元の形は変更していません。");
    switch (BRepAdaptor_Surface(face).GetType()) {
    case GeomAbs_Plane: case GeomAbs_Cylinder: case GeomAbs_Cone:
    case GeomAbs_Sphere: case GeomAbs_Torus:
        return Failed("平面・円筒・円錐・球などの面は、すでにいちばん簡単な形です。",
            "整え直す必要はありません(形を変えずに制御点を減らす余地がありません)。");
    default: break;
    }
    const auto type = BRepAdaptor_Surface(face).GetType();
    if (type != GeomAbs_BSplineSurface && type != GeomAbs_BezierSurface) {
        // Exact conic/swept conversions can change parameterization. Convert the
        // complete private face, including pcurves, rather than just its support.
        const int loops = WireCount(face);
        BRepBuilderAPI_Copy privateCopy(face, true, false);
        if (!privateCopy.IsDone()) return Failed("元の面を保護するコピーを作れませんでした。");
        BRepBuilderAPI_NurbsConvert conversion(privateCopy.Shape(), true);
        if (!conversion.IsDone()) return Failed("境界を保つ形式へ変換できませんでした。");
        TopExp_Explorer faces(conversion.Shape(), TopAbs_FACE);
        if (!faces.More()) return Failed("形式変換後の面がありません。");
        face = TopoDS::Face(faces.Current());
        faces.Next();
        if (faces.More() || WireCount(face) != loops || !BRepCheck_Analyzer(face).IsValid()) {
            return Failed("形式変換で元の境界を保てませんでした。");
        }
    }
    Spline original = Copy(GeomConvert::SurfaceToBSplineSurface(BRep_Tool::Surface(face)));
    if (original->IsUPeriodic() || original->IsVPeriodic()) {
        double u0, u1, v0, v1;
        original->Bounds(u0, u1, v0, v1);
        original->Segment(u0, u1, v0, v1);
    }
    const int before = Poles(original);
    if (before > 32768) return Failed("制御点が多く、一度に整えられません。", "面を分けて整えてください。");
    Spline best = ReduceKnots(original, toleranceMm * .25);
    double bound = DeviationBound(original, best);
    if (!std::isfinite(bound) || bound > toleranceMm) {
        best = Copy(original);
        bound = 0;
    }
    // The native approximator knows the original continuity intervals. A result that
    // merely HasResult() is NOT sufficient; it must also pass our independent bound.
    try {
        GeomConvert_ApproxSurface approximation(original, toleranceMm * .5,
            GeomAbs_C2, GeomAbs_C2, 8, 8, 128, 1);
        if (approximation.IsDone() && !approximation.Surface().IsNull()
            && std::isfinite(approximation.MaxError()) && approximation.MaxError() <= toleranceMm
            && Poles(approximation.Surface()) < Poles(best)) {
            const double measured = DeviationBound(original, approximation.Surface());
            if (std::isfinite(measured) && measured <= toleranceMm) {
                best = approximation.Surface();
                bound = measured;
            }
        }
    } catch (const Standard_Failure&) {
        // A certified native knot reduction can still be used if global refitting fails.
    }
    const int after = Poles(best);
    if (after >= before) return Failed("指定した許容内では制御点を減らせませんでした。",
        "元の形は変更していません。この面はそのまま使うか、許容を見直してください。");
    if (!std::isfinite(bound) || bound > toleranceMm) {
        return Failed("元の面からの偏差が許容内であることを確認できませんでした。");
    }
    const TopoDS_Face made = TrimLike(face, best, std::max(Precision::Confusion(),
        bound + tolerance.modelLinearMm));
    if (made.IsNull()) return Failed("整えた面の境界や穴を保てませんでした。", "元の形は変更していません。");
    auto finished = detail::FinishSurfaceResult(made, tolerance);
    if (!finished.HasValue()) return Out::Failure(finished.Diagnostics());
    SurfaceEditResult result;
    result.surface = finished.Value();
    result.deviationMm = bound;
    result.polesBefore = before;
    result.polesAfter = after;
    result.noteJa = "制御点 " + std::to_string(before) + " → " + std::to_string(after)
        + "、元の面からの偏差の上限 " + Mm(bound) + " mm(許容 " + Mm(toleranceMm) + " mm)。";
    return Out::Success(std::move(result), finished.Diagnostics());
}
} // namespace

base::Result<SurfaceEditResult> RefitSurface(const modeling::KernelShapeHandle& source,
    double toleranceMm, const geometry::GeometryTolerance& tolerance)
{
    try {
        return Refit(source, toleranceMm, tolerance);
    } catch (const Standard_Failure& failure) {
        return Failed("面を整えられませんでした。", failure.what());
    } catch (const std::exception& failure) {
        return Failed("面を整えられませんでした。", failure.what());
    } catch (...) {
        return Failed("面を整えられませんでした。");
    }
}
} // namespace kachakacha::v2::kernel
#endif
