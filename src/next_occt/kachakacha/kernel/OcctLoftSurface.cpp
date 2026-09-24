#include "kachakacha/kernel/OcctLoftSurface.h"

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/geometry/WireEdit.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/geometry/CurveSampling.h"
#include "kachakacha/geometry/Units.h"
#include "kachakacha/modeling/GordonGrid.h"
#include "kachakacha/modeling/GuideSurfaceTable.h"
#include "kachakacha/modeling/LoftInput.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepOffsetAPI_MakeFilling.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomLProp_SLProps.hxx>
#include <GeomAPI_PointsToBSplineSurface.hxx>
#include <GeomAbs_Shape.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_CompCurveToBSplineCurve.hxx>
#include <GeomFill_BSplineCurves.hxx>
#include <GeomFill_FillingStyle.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BoundedCurve.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Surface.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
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
            std::string(what) + ": " + std::string(failure.what())));
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
    BRepOffsetAPI_ThruSections generator(false, false, Tol3d(tolerance));
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
    shell.SetMode(false);   // 修正フレネ(ねじれを抑える)
    for (const auto& section : sections) {
        auto wire = ToWire(section, tolerance.modelLinearMm);
        if (!wire.HasValue()) {
            return Result<TopoDS_Shape>::Failure(wire.Diagnostics());
        }
        // 断面はすでに正しい場所にある。動かさない・回さない(案内付きロフトと同じ理由)。
        shell.Add(wire.Value(), false, false);
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
    shell.SetMode(auxiliary.Value(), true);
    for (const std::size_t index : analysis.sectionOrdering.chainIndices) {
        auto wire = ToWire(request.chains[index].segments, tolerance.modelLinearMm);
        if (!wire.HasValue()) {
            return Result<TopoDS_Shape>::Failure(wire.Diagnostics());
        }
        shell.Add(wire.Value(), false, false);
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
        filler.Add(edge.Value(), GeomAbs_C0, bound ? true : false);
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
    filler.Add(edge.Value(), GeomAbs_C0, true);
    return Result<bool>::Success(true);
}

//! 断面とガイドを全部通るように張る。境界 = 最初と最後の断面と両脇、
//! 通る線 = 途中の断面と、境界に使っていない全部のガイド。
[[nodiscard]] Result<TopoDS_Shape> RailFilling(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis,
    const std::vector<std::vector<CurveSegment>>& sections, const GeometryTolerance& tolerance)
{
    using Out = Result<TopoDS_Shape>;
    BRepOffsetAPI_MakeFilling filler(3, 15, 3, false, 1.0e-5, Tol3d(tolerance), 0.01,
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
        // 掃引が負の円弧(逆向きにした円弧)は、ToGeomCurve が区間を入れ替えて渡すので曲線の
        // 向きが線の向きと逆になる(ToEdge は辺を反転して合わせる)。ここは曲線のまま使うので
        // 自分で反転する。合わせないと 2 本目がつながらず「辺の曲線をつなげませんでした」になり、
        // 1 本だけの辺でも角が逆に合う(オーナーの atama.kcd2、HP-LF-10、2026-09-24)。
        if (segment.Kind() == geometry::CurveKind::CircularArc && segment.SweepAngleRad() < 0.0) {
            piece->Reverse();
        }
        if (joined.IsNull()) {
            joined = piece;
            continue;
        }
        GeomConvert_CompCurveToBSplineCurve concat(joined);
        if (!concat.Add(piece, std::max(tolerance.interactiveJoinMm, Tol3d(tolerance)),
                true)) {
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


//! 輪の鎖を B-spline にし、端点が合う向きにそろえる(最初の鎖は 2 本目に近い端を終点にする)。
[[nodiscard]] std::vector<occ::handle<Geom_BSplineCurve>> RingCurves(
    const GuideSurfaceRequest& request, const std::vector<std::size_t>& ring,
    const GeometryTolerance& tolerance)
{
    std::vector<occ::handle<Geom_BSplineCurve>> curves;
    for (const std::size_t index : ring) {
        if (index >= request.chains.size() || request.chains[index].segments.empty()) {
            return {};
        }
        auto curve = SideCurve(request.chains[index].segments, false, tolerance);
        if (!curve.HasValue() || curve.Value().IsNull()) {
            return {};
        }
        curves.push_back(curve.Value());
    }
    if (curves.size() < 3) {
        return {};
    }
    // 1 本目の向き: 終点が 2 本目のどちらかの端に近くなるように。
    {
        const gp_Pnt s2 = curves[1]->StartPoint();
        const gp_Pnt e2 = curves[1]->EndPoint();
        const double endGap = std::min(curves[0]->EndPoint().Distance(s2), curves[0]->EndPoint().Distance(e2));
        const double startGap = std::min(curves[0]->StartPoint().Distance(s2), curves[0]->StartPoint().Distance(e2));
        if (startGap < endGap) {
            curves[0]->Reverse();
        }
    }
    for (std::size_t k = 1; k < curves.size(); ++k) {
        const gp_Pnt tail = curves[k - 1]->EndPoint();
        if (curves[k]->StartPoint().Distance(tail) > curves[k]->EndPoint().Distance(tail)) {
            curves[k]->Reverse();
        }
    }
    return curves;
}

//! 隣り合う 2 本のつなぎ目の折れ角(度)。
[[nodiscard]] double TurnDegBetween(const occ::handle<Geom_BSplineCurve>& a,
    const occ::handle<Geom_BSplineCurve>& b)
{
    gp_Pnt p;
    gp_Vec out;
    gp_Vec in;
    a->D1(a->LastParameter(), p, out);
    b->D1(b->FirstParameter(), p, in);
    if (out.Magnitude() < 1.0e-12 || in.Magnitude() < 1.0e-12) {
        return 0.0;
    }
    return out.Angle(in) * 180.0 / kachakacha::v2::geometry::kPi;
}

//! 折れの小さいつなぎ目から順に束ねて、side 本にする。つなげなかったら空。
[[nodiscard]] std::vector<occ::handle<Geom_BSplineCurve>> MergeToSides(
    std::vector<occ::handle<Geom_BSplineCurve>> curves, std::size_t sides,
    const GeometryTolerance& tolerance)
{
    while (curves.size() > sides) {
        std::size_t best = 0;
        double bestDeg = 1.0e9;
        for (std::size_t k = 0; k < curves.size(); ++k) {
            const double deg = TurnDegBetween(curves[k], curves[(k + 1) % curves.size()]);
            if (deg < bestDeg) {
                bestDeg = deg;
                best = k;
            }
        }
        const std::size_t next = (best + 1) % curves.size();
        GeomConvert_CompCurveToBSplineCurve concat(curves[best]);
        if (!concat.Add(curves[next], std::max(tolerance.interactiveJoinMm, Tol3d(tolerance)), true)) {
            return {};
        }
        curves[best] = concat.BSplineCurve();
        curves.erase(curves.begin() + static_cast<std::ptrdiff_t>(next));
    }
    return curves;
}

//! G1 の許容(度)と G2 の許容(曲率の差)。核の MakeFilling に渡す目標よりゆるく、
//! 目で見て折れ目が分からない程度。超えたら「滑らかにできなかった」と言って断る。
constexpr double kG1LimitDeg = kContinuityG1LimitDeg;
constexpr double kG2Limit = kContinuityG2Limit;

[[nodiscard]] std::string Mm3(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    return buffer;
}

[[nodiscard]] std::string ChainName(const GuideSurfaceRequest& request, std::size_t index)
{
    const auto& chain = request.chains[index];
    return modeling::ChainRoleLabelJa(request.method, chain.role) + " " + std::to_string(chain.index);
}

//! その辺が乗っている支持面の面。乗っていなければ、どれだけ離れているかを言って断る。
[[nodiscard]] Result<TopoDS_Face> SupportFaceFor(const GuideSurfaceRequest& request,
    std::size_t chainIndex, const GeometryTolerance& tolerance)
{
    using Out = Result<TopoDS_Face>;
    const auto& chain = request.chains[chainIndex];
    TopoDS_Shape support;
    if (chain.supportShapeHandle == 0
        || !LookupShape(modeling::KernelShapeHandle{chain.supportShapeHandle}, support)) {
        return Out::Failure(MakeError(kSurfaceSourceMissing,
            ChainName(request, chainIndex) + "の支持面の形が見つかりません。",
            "支持面を作り直すか、選び直してください。"));
    }
    const std::vector<Vector3> dense = geometry::SampleChain(chain.segments,
        std::max(tolerance.modelLinearMm * 10.0, 1.0e-4));
    std::vector<Vector3> probes;
    for (int step = 0; step < 7 && !dense.empty(); ++step) {
        probes.push_back(dense[static_cast<std::size_t>(
            step * static_cast<double>(dense.size() - 1) / 6.0 + 0.5)]);
    }
    TopoDS_Face best;
    double bestWorst = std::numeric_limits<double>::infinity();
    for (TopExp_Explorer explorer(support, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const TopoDS_Face face = TopoDS::Face(explorer.Current());
        double worst = 0.0;
        for (const Vector3& point : probes) {
            const TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(ToPoint(point));
            BRepExtrema_DistShapeShape measure(vertex, face);
            worst = std::max(worst, measure.IsDone() ? measure.Value()
                                                     : std::numeric_limits<double>::infinity());
        }
        if (worst < bestWorst) {
            bestWorst = worst;
            best = face;
        }
    }
    const double limit = std::max(tolerance.interactiveJoinMm * 2.0, 1.0e-3);
    if (best.IsNull() || bestWorst > limit) {
        return Out::Failure(MakeError(kSurfaceBuildFailed,
            ChainName(request, chainIndex) + "が支持面の縁に乗っていません。",
            "最大 " + Mm3(std::isfinite(bestWorst) ? bestWorst : 0.0) + " mm 離れています(許容 "
                + Mm3(limit) + " mm)。G1/G2 は、隣の面の縁と同じ線に対して指定します。"));
    }
    return Out::Success(best);
}

//! 面の上の 1 点の値(法線と 1・2 階の微分)。
struct SurfacePoint {
    bool ok = false;
    gp_Vec normal;
    gp_Vec du;
    gp_Vec dv;
    gp_Vec duu;
    gp_Vec duv;
    gp_Vec dvv;
};

//! 点に一番近い面の上の点で値を取る。
[[nodiscard]] SurfacePoint EvaluateNear(const occ::handle<Geom_Surface>& surface,
    const gp_Pnt& point)
{
    SurfacePoint out;
    if (surface.IsNull()) {
        return out;
    }
    GeomAPI_ProjectPointOnSurf projection(point, surface);
    if (!projection.IsDone() || projection.NbPoints() == 0) {
        return out;
    }
    double u = 0.0;
    double v = 0.0;
    projection.LowerDistanceParameters(u, v);
    GeomLProp_SLProps props(surface, u, v, 2, Precision::Confusion());
    if (!props.IsNormalDefined()) {
        return out;
    }
    out.normal = gp_Vec(props.Normal());
    out.du = props.D1U();
    out.dv = props.D1V();
    out.duu = props.D2U();
    out.duv = props.DUV();
    out.dvv = props.D2V();
    out.ok = true;
    return out;
}

//! 接平面の中の向き direction の法曲率 II(d,d) / I(d,d)(1/mm)。
[[nodiscard]] double NormalCurvature(const SurfacePoint& p, const gp_Vec& direction)
{
    const double e = p.du.Dot(p.du);
    const double f = p.du.Dot(p.dv);
    const double g = p.dv.Dot(p.dv);
    const double det = e * g - f * f;
    if (!(std::abs(det) > 1.0e-18)) {
        return 0.0;
    }
    const double a = p.du.Dot(direction);
    const double b = p.dv.Dot(direction);
    const double s = (a * g - b * f) / det;
    const double t = (b * e - a * f) / det;
    const double first = e * s * s + 2.0 * f * s * t + g * t * t;
    if (!(first > 1.0e-18)) {
        return 0.0;
    }
    const double second = p.duu.Dot(p.normal) * s * s + 2.0 * p.duv.Dot(p.normal) * s * t
        + p.dvv.Dot(p.normal) * t * t;
    return second / first;
}

//! 境界の辺の上で測る点と、そこでの辺の向き。角(辺の両端)そのものは外す。
//! 角は隣の辺でも決まる点で、そこだけの値は辺の滑らかさを表さない。
[[nodiscard]] std::vector<std::pair<gp_Pnt, gp_Vec>> BoundarySamples(
    const std::vector<CurveSegment>& segments)
{
    std::vector<std::pair<gp_Pnt, gp_Vec>> out;
    if (segments.empty()) {
        return out;
    }
    const int perSegment = std::max(3, 24 / static_cast<int>(segments.size()));
    for (const CurveSegment& segment : segments) {
        for (int k = 0; k < perSegment; ++k) {
            const double t = (k + 0.5) / perSegment;
            const Vector3 d = segment.FirstDerivative(t);
            const double length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (!(length > 1.0e-12)) {
                continue;
            }
            out.emplace_back(ToPoint(segment.Evaluate(t)),
                gp_Vec(d.x / length, d.y / length, d.z / length));
        }
    }
    return out;
}

struct SampleContinuity {
    std::size_t measured = 0;
    double worstAngleDeg = 0.0;
    double worstCurvature = 0.0;
};

//! 辺の上の点ごとに、2 つの面の法線の角度と、辺を横切る向きの法曲率の差を測る。
[[nodiscard]] SampleContinuity MeasureSamples(const occ::handle<Geom_Surface>& first,
    const occ::handle<Geom_Surface>& second,
    const std::vector<std::pair<gp_Pnt, gp_Vec>>& samples)
{
    SampleContinuity out;
    for (const auto& [point, tangent] : samples) {
        const SurfacePoint a = EvaluateNear(first, point);
        const SurfacePoint b = EvaluateNear(second, point);
        if (!a.ok || !b.ok) {
            continue;
        }
        ++out.measured;
        const double dot = a.normal.Dot(b.normal);
        out.worstAngleDeg = std::max(out.worstAngleDeg,
            std::acos(std::min(1.0, std::abs(dot))) * 180.0 / 3.14159265358979323846);
        // 辺を横切る向き(接平面の中で辺に直角)の法曲率。法線の向きをそろえて比べる。
        const double across = NormalCurvature(a, a.normal.Crossed(tangent));
        const double other = NormalCurvature(b, b.normal.Crossed(tangent));
        out.worstCurvature = std::max(out.worstCurvature,
            std::abs(across - (dot < 0.0 ? -other : other)));
    }
    return out;
}

//! 核の辺の上の点と向き(両端は外す)。
[[nodiscard]] std::vector<std::pair<gp_Pnt, gp_Vec>> EdgeSamples(const TopoDS_Edge& edge)
{
    std::vector<std::pair<gp_Pnt, gp_Vec>> out;
    BRepAdaptor_Curve curve(edge);
    const double first = curve.FirstParameter();
    const double last = curve.LastParameter();
    constexpr int kCount = 24;
    for (int k = 0; k < kCount; ++k) {
        const double u = first + (last - first) * (k + 0.5) / kCount;
        gp_Pnt point;
        gp_Vec tangent;
        curve.D1(u, point, tangent);
        if (!(tangent.Magnitude() > 1.0e-12)) {
            continue;
        }
        out.emplace_back(point, tangent.Normalized());
    }
    return out;
}

} // namespace

TopoDS_Face CoonsFromRing(const GuideSurfaceRequest& request, const std::vector<std::size_t>& ring,
    const GeometryTolerance& tolerance)
{
    const auto made = Guarded([&]() -> Result<TopoDS_Face> {
        using Out = Result<TopoDS_Face>;
        // G1/G2 の辺があるときは初期面を渡さない(PC 2026-09-24: 初期面を渡すと支持面への G1 が
        // 効かず、G0 と同じ形になった)。3 側の Coons も渡さない(PC: 3 本の GeomFill が落ちた)。
        for (const std::size_t index : ring) {
            if (index < request.chains.size()
                && request.chains[index].continuity != modeling::SurfaceContinuity::G0) {
                return Out::Success(TopoDS_Face());
            }
        }
        std::vector<occ::handle<Geom_BSplineCurve>> curves = RingCurves(request, ring, tolerance);
        if (curves.size() < 4) {
            return Out::Success(TopoDS_Face());
        }
        curves = MergeToSides(std::move(curves), 4, tolerance);
        if (curves.size() != 4) {
            return Out::Success(TopoDS_Face());
        }
        for (auto& curve : curves) {
            // Coons は各方向に制御点 4 つ以上を要る。直線・短い円弧は 3 次へ上げる。
            if (curve->Degree() < 3) {
                curve->IncreaseDegree(3);
            }
        }
        SnapCorners(curves);
        GeomFill_BSplineCurves patch(curves[0], curves[1], curves[2], curves[3], GeomFill_CoonsStyle);
        const occ::handle<Geom_BSplineSurface> surface = patch.Surface();
        if (surface.IsNull()) {
            return Out::Success(TopoDS_Face());
        }
        BRepBuilderAPI_MakeFace face(occ::handle<Geom_Surface>(surface), Precision::Confusion());
        return Out::Success(face.IsDone() ? face.Face() : TopoDS_Face());
    }, "境界面の初期面");
    return made.HasValue() ? made.Value() : TopoDS_Face();
}

Result<bool> AddBoundaryEdges(BRepOffsetAPI_MakeFilling& filler,
    const GuideSurfaceRequest& request, std::size_t chainIndex,
    const std::vector<TopoDS_Edge>& edges, const GeometryTolerance& tolerance,
    std::vector<ContinuityCheck>& checks)
{
    const auto& chain = request.chains[chainIndex];
    std::vector<TopoDS_Edge> use = edges;
    if (use.empty()) {
        for (const CurveSegment& segment : chain.segments) {
            auto edge = ToEdge(segment);
            if (!edge.HasValue()) {
                return Result<bool>::Failure(edge.Diagnostics());
            }
            use.push_back(edge.Value());
        }
    }
    if (chain.continuity == modeling::SurfaceContinuity::G0) {
        for (const TopoDS_Edge& edge : use) {
            filler.Add(edge, GeomAbs_C0, true);
        }
        return Result<bool>::Success(true);
    }
    const auto face = SupportFaceFor(request, chainIndex, tolerance);
    if (!face.HasValue()) {
        return Result<bool>::Failure(face.Diagnostics());
    }
    const GeomAbs_Shape order = FillingOrder(chain.continuity);
    for (const TopoDS_Edge& edge : use) {
        (void)filler.Add(edge, face.Value(), order, true);
    }
    checks.push_back(ContinuityCheck{chainIndex, chain.continuity, face.Value()});
    return Result<bool>::Success(true);
}

Result<ContinuityMeasure> MeasureContinuity(const TopoDS_Shape& built,
    const GuideSurfaceRequest& request, const std::vector<ContinuityCheck>& checks)
{
    using Out = Result<ContinuityMeasure>;
    ContinuityMeasure measure;
    if (checks.empty()) {
        return Out::Success(measure);
    }
    TopoDS_Face result;
    for (TopExp_Explorer explorer(built, TopAbs_FACE); explorer.More(); explorer.Next()) {
        result = TopoDS::Face(explorer.Current());
        break;
    }
    if (result.IsNull()) {
        return Out::Failure(MakeError(kSurfaceBuildFailed, "出来た面の滑らかさを測れませんでした。",
            "面が見つかりません。"));
    }
    const occ::handle<Geom_Surface> resultSurface = BRep_Tool::Surface(result);
    for (const ContinuityCheck& check : checks) {
        const occ::handle<Geom_Surface> supportSurface = BRep_Tool::Surface(check.support);
        const auto samples = BoundarySamples(request.chains[check.chainIndex].segments);
        const SampleContinuity along = MeasureSamples(resultSurface, supportSurface, samples);
        const std::size_t measured = along.measured;
        const double worstAngle = along.worstAngleDeg;
        const double worstCurvature = along.worstCurvature;
        if (measured * 2 < samples.size()) {
            return Out::Failure(MakeError(kSurfaceBuildFailed,
                ChainName(request, check.chainIndex) + "の滑らかさを測れませんでした。",
                "辺の上の点を面へ落とせませんでした(" + std::to_string(measured) + " / "
                    + std::to_string(samples.size()) + " 点)。"));
        }
        measure.g1ErrorDeg = std::max(measure.g1ErrorDeg, worstAngle);
        if (worstAngle > kG1LimitDeg) {
            return Out::Failure(MakeError(kSurfaceBuildFailed,
                std::string(modeling::SurfaceContinuityName(check.order)) + "を指定しましたが、"
                    + ChainName(request, check.chainIndex) + "で面が最大 " + Mm3(worstAngle)
                    + " 度折れています。",
                "許容は " + Mm3(kG1LimitDeg) + " 度です。支持面とのつながり方か、ほかの辺の形を見直してください。"));
        }
        if (check.order != modeling::SurfaceContinuity::G2) {
            continue;
        }
        measure.g2Error = std::max(measure.g2Error, worstCurvature);
        if (worstCurvature > kG2Limit) {
            return Out::Failure(MakeError(kSurfaceBuildFailed,
                "G2を指定しましたが、" + ChainName(request, check.chainIndex)
                    + "で曲率が最大 " + Mm3(worstCurvature) + " (1/mm) 食い違っています。",
                "許容は " + Mm3(kG2Limit) + " (1/mm) です。G1 にするか、辺の形を見直してください。"));
        }
    }
    return Out::Success(measure);
}

EdgeContinuity MeasureEdgeContinuity(const TopoDS_Face& result, const TopoDS_Face& support,
    const TopoDS_Edge& edge)
{
    EdgeContinuity out;
    if (result.IsNull() || support.IsNull() || edge.IsNull()) {
        return out;
    }
    const auto samples = EdgeSamples(edge);
    const SampleContinuity along = MeasureSamples(BRep_Tool::Surface(result),
        BRep_Tool::Surface(support), samples);
    out.samples = samples.size();
    out.measuredSamples = along.measured;
    out.measured = along.measured * 2 >= samples.size() && along.measured > 0;
    out.g1Deg = along.worstAngleDeg;
    out.g2 = along.worstCurvature;
    return out;
}

namespace {

} // namespace

Result<TopoDS_Shape> BuildFourEdgeShape(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis, const GeometryTolerance& tolerance,
    ContinuityMeasure& measure)
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
            // Coons は各方向に制御点 4 つ以上を要る(OCCT: 足りないと "invalid filling style")。
            // 直線(1 次・2 点)や短い円弧(2 次・3 点)は 3 次へ上げる。形は変わらない。
            if (curve.Value()->Degree() < 3) {
                curve.Value()->IncreaseDegree(3);
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
        if (!plan.refill) {
            return Out::Success(TopoDS_Shape(face.Face()));
        }
        // 内側の通る線か G1/G2 がある: 4 辺を境界(辺ごとの連続条件つき)、通る線を拘束にして、
        // 4 辺の面から張り直す。
        BRepOffsetAPI_MakeFilling filler(3, 15, 3, false, 1.0e-5, Tol3d(tolerance),
            0.01, 0.1, 8, 12);
        std::vector<ContinuityCheck> checks;
        for (std::size_t k = 0; k < curves.size(); ++k) {
            BRepBuilderAPI_MakeEdge edge{occ::handle<Geom_Curve>(curves[k])};
            if (!edge.IsDone()) {
                return Out::Failure(MakeError(kSurfaceBuildFailed, "四辺面の辺を作れませんでした。", {}));
            }
            const auto added = AddBoundaryEdges(filler, request, plan.sides[k], {edge.Edge()},
                tolerance, checks);
            if (!added.HasValue()) {
                return Out::Failure(added.Diagnostics());
            }
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
        const auto measured = MeasureContinuity(filler.Shape(), request, checks);
        if (!measured.HasValue()) {
            return Out::Failure(measured.Diagnostics());
        }
        measure = measured.Value();
        return Out::Success(filler.Shape());
    }, "四辺面");
}

namespace {

//! Gordon の格子点を B-spline 面へ写して、1 枚の面にする。
[[nodiscard]] Result<TopoDS_Shape> FaceFromGrid(const modeling::GordonGrid& g)
{
    using Out = Result<TopoDS_Shape>;
    NCollection_Array2<gp_Pnt> points(1, static_cast<int>(g.columns), 1,
        static_cast<int>(g.rows));
    for (std::size_t column = 0; column < g.columns; ++column) {
        for (std::size_t row = 0; row < g.rows; ++row) {
            points.SetValue(static_cast<int>(column) + 1, static_cast<int>(row) + 1,
                ToPoint(g.At(row, column)));
        }
    }
    // 写す誤差の目標は、曲線網の許容(0.02 mm)より十分小さく。
    GeomAPI_PointsToBSplineSurface fit(points, 3, 8, GeomAbs_C2, 0.002);
    if (!fit.IsDone() || fit.Surface().IsNull()) {
        return Out::Failure(MakeError(kSurfaceBuildFailed,
            "曲線網の形を面へ写せませんでした。", "線の数を減らすか、曲線網(近似)を使ってください。"));
    }
    const occ::handle<Geom_Surface> surface = fit.Surface();
    BRepBuilderAPI_MakeFace face{surface, Precision::Confusion()};
    if (!face.IsDone()) {
        return Out::Failure(MakeError(kSurfaceBuildFailed, "曲線網の面を作れませんでした。", {}));
    }
    return Out::Success(TopoDS_Shape(face.Face()));
}

//! 線が多いほど細かく。1 区間に 8 点、25〜97 点。
[[nodiscard]] std::size_t GridSamples(std::size_t lines)
{
    return std::clamp<std::size_t>(8 * (lines > 1 ? lines - 1 : 1) + 1, 25, 97);
}

//! 外側のガイドが両脇にある: 断面とガイドの網(Gordon)。網は検査が組んだ(仮想断面も含む)。
//! 2026-09-22 まで、外側の 2 本だけのときは MakePipeShell(2 本のレールで掃く)だった。
//! 断面が 3 本あると断面の間で面が波打った(はしご形の撮影で、山が 3 つのはずが 7 つ)。
//! 網は自然 3 次スプラインでつなぐので、断面の間はなめらかに移る。
[[nodiscard]] Result<TopoDS_Shape> RailNetwork(const GuideSurfaceRequest& request,
    const GuideSurfaceAnalysis& analysis, const GeometryTolerance& tolerance)
{
    const GuideSurfaceRequest network = modeling::LoftNetworkRequest(request, analysis);
    std::size_t us = 0;
    std::size_t vs = 0;
    for (const auto& chain : network.chains) {
        us += chain.role == ChainRole::GuideU ? 1 : 0;
        vs += chain.role == ChainRole::GuideV ? 1 : 0;
    }
    const auto grid =
        modeling::BuildGordonGrid(network, tolerance, GridSamples(std::max(us, vs)));
    if (!grid.HasValue()) {
        return Result<TopoDS_Shape>::Failure(grid.Diagnostics());
    }
    return FaceFromGrid(grid.Value());
}

} // namespace

Result<TopoDS_Shape> BuildNetworkShape(const GuideSurfaceRequest& request,
    const GeometryTolerance& tolerance)
{
    using Out = Result<TopoDS_Shape>;
    return Guarded([&]() -> Out {
        std::size_t lines = 0;
        for (const auto& chain : request.chains) {
            lines = std::max(lines, chain.index > 0 ? static_cast<std::size_t>(chain.index) : 0);
        }
        const auto grid = modeling::BuildGordonGrid(request, tolerance, GridSamples(lines));
        if (!grid.HasValue()) {
            return Out::Failure(grid.Diagnostics());
        }
        return FaceFromGrid(grid.Value());
    }, "曲線網(Gordon)");
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
        case modeling::LoftSolver::RailNetwork:
            return RailNetwork(request, analysis, tolerance);
        }
        return Out::Failure(MakeError(kSurfaceUnsupportedMethod, "知らないロフトの作り方です。", {}));
    }, "ロフト");
}

} // namespace kachakacha::v2::kernel::detail

#endif // KACHACAD_V2_WITH_OCCT
