#include "kachakacha/kernel/OcctLoftSurface.h"

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctGuideSurface.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <GeomAbs_Shape.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_CompCurveToBSplineCurve.hxx>
#include <GeomFill_BSplineCurves.hxx>
#include <GeomFill_FillingStyle.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BoundedCurve.hxx>
#include <Geom_Curve.hxx>
#include <NCollection_Array1.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <string>
#include <vector>

namespace kachakacha::v2::kernel::detail {

using base::MakeError;
using base::Result;
using geometry::CurveSegment;
using geometry::GeometryTolerance;
using geometry::Vector3;
using modeling::ChainRole;
using modeling::GuideSurfaceAnalysis;
using modeling::GuideSurfaceRequest;
using modeling::LoftRailSide;

namespace {

template<class Function>
[[nodiscard]] auto Guarded(Function&& body, const char* what) -> decltype(body())
{
    using ResultType = decltype(body());
    try {
        return body();
    } catch (const Standard_Failure& failure) {
        return ResultType::Failure(MakeError(kSurfaceBuildFailed,
            "幾何カーネルが面を作れませんでした。",
            std::string(what) + ": " + std::string(failure.GetMessageString())));
    } catch (const std::exception& error) {
        return ResultType::Failure(MakeError(kSurfaceBuildFailed,
            "幾何カーネルが面を作れませんでした。", std::string(what) + ": " + error.what()));
    } catch (...) {
        return ResultType::Failure(MakeError(kSurfaceBuildFailed,
            "幾何カーネルが面を作れませんでした。", what));
    }
}

[[nodiscard]] double Tol3d(const GeometryTolerance& tolerance)
{
    return std::max(tolerance.modelLinearMm, Precision::Confusion());
}

//! 鎖を逆向きにする(曲線の種類は保つ)。
[[nodiscard]] Result<std::vector<CurveSegment>> Reversed(const std::vector<CurveSegment>& chain)
{
    std::vector<CurveSegment> out;
    for (auto item = chain.rbegin(); item != chain.rend(); ++item) {
        auto flipped = geometry::ReverseCurve(*item);
        if (!flipped.HasValue()) {
            return Result<std::vector<CurveSegment>>::Failure(flipped.Diagnostics());
        }
        out.push_back(flipped.Value());
    }
    return Result<std::vector<CurveSegment>>::Success(std::move(out));
}

//! 並べた順・向きの断面(analysis.sectionOrdering と loft.reverseSections のとおり)。
[[nodiscard]] Result<std::vector<std::vector<CurveSegment>>> OrientedSections(
    const GuideSurfaceRequest& request, const GuideSurfaceAnalysis& analysis)
{
    using Out = Result<std::vector<std::vector<CurveSegment>>>;
    std::vector<std::vector<CurveSegment>> out;
    const auto& order = analysis.sectionOrdering.chainIndices;
    for (std::size_t at = 0; at < order.size(); ++at) {
        const auto& segments = request.chains[order[at]].segments;
        const bool reverse = at < analysis.loft.reverseSections.size()
            && analysis.loft.reverseSections[at];
        if (!reverse) {
            out.push_back(segments);
            continue;
        }
        auto flipped = Reversed(segments);
        if (!flipped.HasValue()) {
            return Out::Failure(flipped.Diagnostics());
        }
        out.push_back(flipped.Value());
    }
    return Out::Success(std::move(out));
}

//! 断面をなめらかに通す面(張り直しの初期面にも使う)。
[[nodiscard]] Result<TopoDS_Shape> ThruSections(
    const std::vector<std::vector<CurveSegment>>& sections, const GeometryTolerance& tolerance)
{
    BRepOffsetAPI_ThruSections generator(Standard_False, Standard_False, Tol3d(tolerance));
    for (const auto& section : sections) {
        auto wire = ToWire(section, tolerance.modelLinearMm);
        if (!wire.HasValue()) {
            return Result<TopoDS_Shape>::Failure(wire.Diagnostics());
        }
        generator.AddWire(wire.Value());
    }
    generator.Build();
    if (!generator.IsDone()) {
        return Result<TopoDS_Shape>::Failure(MakeError(kSurfaceBuildFailed,
            "断面から面を作れませんでした。", "断面の向きか、辺の数の対応を確かめてください。"));
    }
    return Result<TopoDS_Shape>::Success(generator.Shape());
}

//! 中心線に沿って断面を運ぶ面。
[[nodiscard]] Result<TopoDS_Shape> AlongCenterline(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis,
    const std::vector<std::vector<CurveSegment>>& sections, const GeometryTolerance& tolerance)
{
    auto spine = ToWire(request.chains[analysis.loft.centerlineChainIndex].segments,
        tolerance.modelLinearMm);
    if (!spine.HasValue()) {
        return Result<TopoDS_Shape>::Failure(spine.Diagnostics());
    }
    BRepOffsetAPI_MakePipeShell shell(spine.Value());
    shell.SetMode(Standard_False);   // 修正フレネ(ねじれを抑える)
    for (const auto& section : sections) {
        auto wire = ToWire(section, tolerance.modelLinearMm);
        if (!wire.HasValue()) {
            return Result<TopoDS_Shape>::Failure(wire.Diagnostics());
        }
        // 断面はすでに正しい場所にある。動かさない・回さない(案内付きロフトと同じ理由)。
        shell.Add(wire.Value(), Standard_False, Standard_False);
    }
    shell.Build();
    if (!shell.IsDone()) {
        return Result<TopoDS_Shape>::Failure(MakeError(kSurfaceBuildFailed,
            "中心線に沿って断面を運べませんでした。",
            "中心線が断面を横切っているか、急に曲がっていないかを確かめてください。"));
    }
    return Result<TopoDS_Shape>::Success(shell.Shape());
}

//! 両端の 2 本のガイドで掃く(従来の案内付きロフトと同じ作り: 始点側が背骨、終点側が補助)。
[[nodiscard]] Result<TopoDS_Shape> TwoRailSweep(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis, const GeometryTolerance& tolerance)
{
    std::size_t spineChain = analysis.loft.rails.front().chainIndex;
    std::size_t auxiliaryChain = analysis.loft.rails.back().chainIndex;
    if (analysis.loft.rails.front().side != LoftRailSide::Start) {
        std::swap(spineChain, auxiliaryChain);
    }
    auto spine = ToWire(request.chains[spineChain].segments, tolerance.modelLinearMm);
    auto auxiliary = ToWire(request.chains[auxiliaryChain].segments, tolerance.modelLinearMm);
    if (!spine.HasValue() || !auxiliary.HasValue()) {
        return Result<TopoDS_Shape>::Failure(
            spine.HasValue() ? auxiliary.Diagnostics() : spine.Diagnostics());
    }
    BRepOffsetAPI_MakePipeShell shell(spine.Value());
    shell.SetMode(auxiliary.Value(), Standard_True);
    for (const std::size_t index : analysis.sectionOrdering.chainIndices) {
        auto wire = ToWire(request.chains[index].segments, tolerance.modelLinearMm);
        if (!wire.HasValue()) {
            return Result<TopoDS_Shape>::Failure(wire.Diagnostics());
        }
        shell.Add(wire.Value(), Standard_False, Standard_False);
    }
    shell.Build();
    if (!shell.IsDone()) {
        return Result<TopoDS_Shape>::Failure(MakeError(kSurfaceBuildFailed,
            "案内線に沿った面を作れませんでした。", "案内線と断面の交わり方を確かめてください。"));
    }
    return Result<TopoDS_Shape>::Success(shell.Shape());
}

//! 点を通る曲線の辺(脇の辺に使う。点は断面の端)。2 点なら直線。
[[nodiscard]] Result<TopoDS_Edge> EdgeThrough(const std::vector<Vector3>& points,
    const GeometryTolerance& tolerance)
{
    using Out = Result<TopoDS_Edge>;
    if (points.size() == 2) {
        BRepBuilderAPI_MakeEdge maker(ToPoint(points[0]), ToPoint(points[1]));
        if (!maker.IsDone()) {
            return Out::Failure(MakeError(kSurfaceBuildFailed, "脇の辺を作れませんでした。", {}));
        }
        return Out::Success(maker.Edge());
    }
    NCollection_Array1<gp_Pnt> array(1, static_cast<int>(points.size()));
    for (std::size_t at = 0; at < points.size(); ++at) {
        array.SetValue(static_cast<int>(at) + 1, ToPoint(points[at]));
    }
    const int degree = std::min(3, static_cast<int>(points.size()) - 1);
    GeomAPI_PointsToBSpline fit(array, 1, degree,
        points.size() >= 4 ? GeomAbs_C2 : GeomAbs_C1, Tol3d(tolerance));
    if (!fit.IsDone() || fit.Curve().IsNull()) {
        return Out::Failure(MakeError(kSurfaceBuildFailed, "脇の辺を作れませんでした。", {}));
    }
    BRepBuilderAPI_MakeEdge maker(occ::handle<Geom_Curve>(fit.Curve()));
    if (!maker.IsDone()) {
        return Out::Failure(MakeError(kSurfaceBuildFailed, "脇の辺を作れませんでした。", {}));
    }
    return Out::Success(maker.Edge());
}

//! 鎖の曲線を 1 本ずつ辺にして、境界(IsBound = 真)か通る線(偽)として足す。
[[nodiscard]] Result<bool> AddChain(BRepOffsetAPI_MakeFilling& filler,
    const std::vector<CurveSegment>& chain, bool bound)
{
    for (const CurveSegment& segment : chain) {
        auto edge = ToEdge(segment);
        if (!edge.HasValue()) {
            return Result<bool>::Failure(edge.Diagnostics());
        }
        filler.Add(edge.Value(), GeomAbs_C0, bound ? Standard_True : Standard_False);
    }
    return Result<bool>::Success(true);
}

[[nodiscard]] TopoDS_Face SingleFace(const TopoDS_Shape& shape)
{
    TopoDS_Face face;
    int count = 0;
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        face = TopoDS::Face(explorer.Current());
        ++count;
    }
    return count == 1 ? face : TopoDS_Face();
}

//! 断面の片側(始点側 or 終点側)の辺。外側のガイドがあればその部分、無ければ断面の端を通す曲線。
[[nodiscard]] Result<bool> AddSide(BRepOffsetAPI_MakeFilling& filler,
    const GuideSurfaceAnalysis& analysis,
    const std::vector<std::vector<CurveSegment>>& sections, LoftRailSide side,
    const GeometryTolerance& tolerance)
{
    for (const auto& rail : analysis.loft.rails) {
        if (rail.side == side) {
            return AddChain(filler, rail.span, true);
        }
    }
    std::vector<Vector3> ends;
    for (const auto& section : sections) {
        ends.push_back(side == LoftRailSide::Start ? section.front().StartPoint()
                                                   : section.back().EndPoint());
    }
    auto edge = EdgeThrough(ends, tolerance);
    if (!edge.HasValue()) {
        return Result<bool>::Failure(edge.Diagnostics());
    }
    filler.Add(edge.Value(), GeomAbs_C0, Standard_True);
    return Result<bool>::Success(true);
}

//! 断面とガイドを全部通るように張る。境界 = 最初と最後の断面と両脇、
//! 通る線 = 途中の断面と、境界に使っていない全部のガイド。
[[nodiscard]] Result<TopoDS_Shape> RailFilling(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis,
    const std::vector<std::vector<CurveSegment>>& sections, const GeometryTolerance& tolerance)
{
    using Out = Result<TopoDS_Shape>;
    BRepOffsetAPI_MakeFilling filler(3, 15, 3, Standard_False, 1.0e-5, Tol3d(tolerance), 0.01,
        0.1, 8, 12);
    const auto check = [](const Result<bool>& added) { return added.HasValue(); };
    for (const auto& step : {AddChain(filler, sections.front(), true),
             AddSide(filler, analysis, sections, LoftRailSide::End, tolerance),
             AddChain(filler, sections.back(), true),
             AddSide(filler, analysis, sections, LoftRailSide::Start, tolerance)}) {
        if (!check(step)) {
            return Out::Failure(step.Diagnostics());
        }
    }
    for (std::size_t at = 1; at + 1 < sections.size(); ++at) {
        const auto added = AddChain(filler, sections[at], false);
        if (!added.HasValue()) {
            return Out::Failure(added.Diagnostics());
        }
    }
    bool startUsed = false;
    bool endUsed = false;
    for (const auto& rail : analysis.loft.rails) {
        // 外側のガイドは片側に 1 本だけ境界にする。残りは全部「通る線」。
        bool& used = rail.side == LoftRailSide::Start ? startUsed : endUsed;
        if (rail.side != LoftRailSide::Interior && !used) {
            used = true;
            continue;
        }
        const auto added = AddChain(filler, rail.span, false);
        if (!added.HasValue()) {
            return Out::Failure(added.Diagnostics());
        }
    }
    // 初期面: 中心線があれば中心線に沿って運んだ面、無ければ断面をなめらかに通した面。
    const auto initial = analysis.loft.hasCenterline
        ? AlongCenterline(request, analysis, sections, tolerance)
        : ThruSections(sections, tolerance);
    if (initial.HasValue()) {
        const TopoDS_Face face = SingleFace(initial.Value());
        if (!face.IsNull()) {
            filler.LoadInitSurface(face);
        }
    }
    filler.Build();
    if (!filler.IsDone()) {
        return Out::Failure(MakeError(kSurfaceBuildFailed,
            "断面とガイドを通る面を張れませんでした。",
            "ガイドが断面を横切る位置と、ガイドどうしの間隔を確かめてください。"));
    }
    return Out::Success(filler.Shape());
}

//! 1 辺(1 本以上の曲線)を 1 本の B-spline にする。輪をたどる向きに合わせる。
[[nodiscard]] Result<occ::handle<Geom_BSplineCurve>> SideCurve(
    const std::vector<CurveSegment>& segments, bool reversed, const GeometryTolerance& tolerance)
{
    using Out = Result<occ::handle<Geom_BSplineCurve>>;
    occ::handle<Geom_BSplineCurve> joined;
    for (const CurveSegment& segment : segments) {
        auto curve = ToGeomCurve(segment);
        if (!curve.HasValue()) {
            return Out::Failure(curve.Diagnostics());
        }
        occ::handle<Geom_BSplineCurve> piece = GeomConvert::CurveToBSplineCurve(curve.Value());
        if (piece.IsNull()) {
            return Out::Failure(MakeError(kSurfaceBuildFailed,
                "四辺面の辺を B-spline にできませんでした。", {}));
        }
        if (joined.IsNull()) {
            joined = piece;
            continue;
        }
        GeomConvert_CompCurveToBSplineCurve concat(joined);
        if (!concat.Add(piece, std::max(tolerance.interactiveJoinMm, Tol3d(tolerance)),
                Standard_True)) {
            return Out::Failure(MakeError(kSurfaceBuildFailed,
                "四辺面の辺の曲線をつなげませんでした。", "辺の中の線どうしが離れています。"));
        }
        joined = concat.BSplineCurve();
    }
    if (joined.IsNull()) {
        return Out::Failure(MakeError(kSurfaceBuildFailed, "四辺面の辺が空です。", {}));
    }
    if (reversed) {
        joined->Reverse();
    }
    return Out::Success(joined);
}

[[nodiscard]] GeomFill_FillingStyle StyleOf(modeling::FourEdgeStyle style) noexcept
{
    switch (style) {
    case modeling::FourEdgeStyle::Coons:   return GeomFill_CoonsStyle;
    case modeling::FourEdgeStyle::Stretch: return GeomFill_StretchStyle;
    case modeling::FourEdgeStyle::Curved:  return GeomFill_CurvedStyle;
    }
    return GeomFill_CoonsStyle;
}

//! 4 辺の角をぴったり合わせる(許容差の内側のずれを、隣り合う端の中点へ寄せる)。
void SnapCorners(std::vector<occ::handle<Geom_BSplineCurve>>& curves)
{
    for (std::size_t k = 0; k < curves.size(); ++k) {
        occ::handle<Geom_BSplineCurve>& here = curves[k];
        occ::handle<Geom_BSplineCurve>& next = curves[(k + 1) % curves.size()];
        const gp_Pnt a = here->EndPoint();
        const gp_Pnt b = next->StartPoint();
        const gp_Pnt corner((a.X() + b.X()) * 0.5, (a.Y() + b.Y()) * 0.5, (a.Z() + b.Z()) * 0.5);
        here->SetPole(here->NbPoles(), corner);
        next->SetPole(1, corner);
    }
}

} // namespace

Result<TopoDS_Shape> BuildFourEdgeShape(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis, const GeometryTolerance& tolerance)
{
    using Out = Result<TopoDS_Shape>;
    return Guarded([&]() -> Out {
        const auto& plan = analysis.fourEdge;
        if (plan.sides.size() != 4 || plan.reversed.size() != 4) {
            return Out::Failure(MakeError(kSurfaceBuildFailed, "四辺面の辺が 4 本ではありません。", {}));
        }
        std::vector<occ::handle<Geom_BSplineCurve>> curves;
        for (std::size_t k = 0; k < 4; ++k) {
            auto curve = SideCurve(request.chains[plan.sides[k]].segments, plan.reversed[k],
                tolerance);
            if (!curve.HasValue()) {
                return Out::Failure(curve.Diagnostics());
            }
            curves.push_back(curve.Value());
        }
        SnapCorners(curves);
        GeomFill_BSplineCurves patch(curves[0], curves[1], curves[2], curves[3],
            StyleOf(request.fourEdgeStyle));
        const occ::handle<Geom_BSplineSurface> surface = patch.Surface();
        if (surface.IsNull()) {
            return Out::Failure(MakeError(kSurfaceBuildFailed,
                "4 辺から面を張れませんでした。", "辺の向きとつながりを確かめてください。"));
        }
        BRepBuilderAPI_MakeFace face(occ::handle<Geom_Surface>(surface), Precision::Confusion());
        if (!face.IsDone()) {
            return Out::Failure(MakeError(kSurfaceBuildFailed, "4 辺の面を作れませんでした。", {}));
        }
        if (!plan.hasInteriorConstraints) {
            return Out::Success(TopoDS_Shape(face.Face()));
        }
        // 内側の通る線がある: 4 辺を境界、通る線を拘束にして、4 辺の面から張り直す。
        BRepOffsetAPI_MakeFilling filler(3, 15, 3, Standard_False, 1.0e-5, Tol3d(tolerance),
            0.01, 0.1, 8, 12);
        for (const auto& curve : curves) {
            BRepBuilderAPI_MakeEdge edge(occ::handle<Geom_Curve>(curve));
            if (!edge.IsDone()) {
                return Out::Failure(MakeError(kSurfaceBuildFailed, "四辺面の辺を作れませんでした。", {}));
            }
            filler.Add(edge.Edge(), GeomAbs_C0, Standard_True);
        }
        for (std::size_t index = 0; index < request.chains.size(); ++index) {
            if (request.chains[index].role != ChainRole::GuideU) {
                continue;
            }
            const auto added = AddChain(filler, request.chains[index].segments, false);
            if (!added.HasValue()) {
                return Out::Failure(added.Diagnostics());
            }
        }
        filler.LoadInitSurface(face.Face());
        filler.Build();
        if (!filler.IsDone()) {
            return Out::Failure(MakeError(kSurfaceBuildFailed,
                "4 辺と通る線から面を張れませんでした。", "通る線が 4 辺の内側にあるかを確かめてください。"));
        }
        return Out::Success(filler.Shape());
    }, "四辺面");
}

Result<TopoDS_Shape> BuildLoftShape(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis, const GeometryTolerance& tolerance)
{
    using Out = Result<TopoDS_Shape>;
    return Guarded([&]() -> Out {
        auto sections = OrientedSections(request, analysis);
        if (!sections.HasValue()) {
            return Out::Failure(sections.Diagnostics());
        }
        if (sections.Value().empty()) {
            return Out::Failure(MakeError(kSurfaceBuildFailed, "断面がありません。", {}));
        }
        switch (analysis.loft.solver) {
        case modeling::LoftSolver::Sections:
            return ThruSections(sections.Value(), tolerance);
        case modeling::LoftSolver::Centerline:
            return AlongCenterline(request, analysis, sections.Value(), tolerance);
        case modeling::LoftSolver::TwoRailSweep:
            return TwoRailSweep(request, analysis, tolerance);
        case modeling::LoftSolver::RailFilling:
            return RailFilling(request, analysis, sections.Value(), tolerance);
        }
        return Out::Failure(MakeError(kSurfaceUnsupportedMethod, "知らないロフトの作り方です。", {}));
    }, "ロフト");
}

} // namespace kachakacha::v2::kernel::detail

#endif // KACHACAD_V2_WITH_OCCT
