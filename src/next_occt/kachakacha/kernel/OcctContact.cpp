#include "kachakacha/kernel/OcctContact.h"
#include <algorithm>
#include <cmath>
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include "kachakacha/kernel/OcctCurveConversion.h"
#include <BRepAlgoAPI_Section.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Shape.hxx>
#include <Standard_Failure.hxx>
#endif

namespace kachakacha::v2::kernel {
using base::MakeError;
using base::Result;
using modeling::KernelShapeHandle;
#ifdef KACHACAD_V2_WITH_OCCT
namespace {
using Wires = std::vector<std::vector<geometry::CurveSegment>>;

Result<Wires> BoundaryWires(const TopoDS_Shape& a, const TopoDS_Shape& b, double tolerance)
{
    BRepAlgoAPI_Section section(a, b, false);
    section.Approximation(false);
    section.Build();
    if (!section.IsDone()) return Result<Wires>::Failure(MakeError("KER-CT01", "交線を計算できません。", {}));
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(section.Shape(), TopAbs_EDGE, edges);
    // Coincident face patches have an area, whose perimeter is the contact wire.
    for (TopExp_Explorer fa(a, TopAbs_FACE); fa.More(); fa.Next()) {
        for (TopExp_Explorer fb(b, TopAbs_FACE); fb.More(); fb.Next()) {
            BRepAlgoAPI_Common common(fa.Current(), fb.Current());
            if (!common.IsDone()) return Result<Wires>::Failure(MakeError("KER-CT01", "接触面を計算できません。", {}));
            for (TopExp_Explorer face(common.Shape(), TopAbs_FACE); face.More(); face.Next()) {
                TopExp::MapShapes(face.Current(), TopAbs_EDGE, edges);
            }
        }
    }
    auto unique = new TopTools_HSequenceOfShape;
    occ::handle<TopTools_HSequenceOfShape> sequence(unique);
    std::vector<geometry::CurveSegment> accepted;
    for (int i = 1; i <= edges.Extent(); ++i) {
        const auto curve = FromEdge(TopoDS::Edge(edges(i)), tolerance);
        if (!curve.HasValue()) return Result<Wires>::Failure(curve.Diagnostics());
        const auto& c = curve.Value();
        const auto duplicate = std::any_of(accepted.begin(), accepted.end(), [&](const auto& old) {
            bool forward = true, reverse = true;
            for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
                forward = forward && (c.Evaluate(t) - old.Evaluate(t)).Length() <= tolerance;
                reverse = reverse && (c.Evaluate(t) - old.Evaluate(1.0-t)).Length() <= tolerance;
            }
            return forward || reverse;
        });
        if (!duplicate) { sequence->Append(edges(i)); accepted.push_back(c); }
    }
    if (sequence->IsEmpty()) return Result<Wires>::Failure(MakeError("KER-CT02",
        "線になる接触・交差がありません。", "離れている、完全に内部にある、または点だけで接しています。"));
    occ::handle<TopTools_HSequenceOfShape> joined = new TopTools_HSequenceOfShape;
    ShapeAnalysis_FreeBounds::ConnectEdgesToWires(sequence, tolerance, false, joined);
    Wires wires;
    for (int i = 1; i <= joined->Length(); ++i) {
        const auto wire = FromWire(TopoDS::Wire(joined->Value(i)), tolerance);
        if (!wire.HasValue()) return Result<Wires>::Failure(wire.Diagnostics());
        wires.push_back(wire.Value());
    }
    return Result<Wires>::Success(std::move(wires));
}
}
#endif

Result<std::vector<ContactPiece>> ContactSolidPieces(KernelShapeHandle handle, double tolerance)
{
    using Out = Result<std::vector<ContactPiece>>;
#ifdef KACHACAD_V2_WITH_OCCT
    TopoDS_Shape shape;
    if (!LookupShape(handle, shape) || shape.IsNull() || !BRepCheck_Analyzer(shape).IsValid())
        return Out::Failure(MakeError("KER-CT03", "閉じた正しい立体を作れません。", {}));
    std::vector<ContactPiece> result;
    for (TopExp_Explorer solid(shape, TopAbs_SOLID); solid.More(); solid.Next()) {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(solid.Current(), properties);
        const double volume = std::abs(properties.Mass());
        if (volume <= std::pow(tolerance, 3))
            return Out::Failure(MakeError("KER-CT03", "許容差より小さい領域があります。", "形状または許容差を確認してください。"));
        result.push_back({StoreShape(solid.Current()), volume, false, 0, 0, FromPoint(properties.CentreOfMass())});
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.center.x != b.center.x) return a.center.x < b.center.x;
        if (a.center.y != b.center.y) return a.center.y < b.center.y;
        return a.center.z < b.center.z;
    });
    for (std::size_t i = 0; i < result.size(); ++i) result[i].fragmentIndex = static_cast<int>(i);
    return Out::Success(std::move(result));
#else
    (void)handle; (void)tolerance;
    return Out::Failure(MakeError("KER-CT00", "幾何カーネルが必要です。", {}));
#endif
}

Result<ContactResult> BuildContact(KernelShapeHandle target, KernelShapeHandle other,
    bool wires, bool outside, bool inside, double tolerance)
{
    using Out = Result<ContactResult>;
#ifdef KACHACAD_V2_WITH_OCCT
    TopoDS_Shape a, b;
    if (!LookupShape(target, a) || !LookupShape(other, b))
        return Out::Failure(MakeError("KER-CT00", "対象と相手の立体を選んでください。", {}));
    try {
        ContactResult result;
        if (wires) {
            auto boundary = BoundaryWires(a, b, tolerance);
            if (!boundary.HasValue()) return Out::Failure(boundary.Diagnostics());
            result.wires = boundary.Value();
        }
        if (outside || inside) {
            BRepAlgoAPI_Common common(a, b);
            if (!common.IsDone()) return Out::Failure(MakeError("KER-CT03", "重なりを計算できません。", {}));
            const auto overlap = ContactSolidPieces(StoreShape(common.Shape()), tolerance);
            if (!overlap.HasValue()) return Out::Failure(overlap.Diagnostics());
            if (overlap.Value().empty()) return Out::Failure(MakeError("KER-CT04", "削除・分割する重なり体積がありません。", "接するだけの場合は「交わりにワイヤー」を使ってください。"));
            if (outside) {
                BRepAlgoAPI_Cut cut(a, b);
                if (!cut.IsDone()) return Out::Failure(MakeError("KER-CT03", "めり込みを削れません。", {}));
                const auto pieces = ContactSolidPieces(StoreShape(cut.Shape()), tolerance);
                if (!pieces.HasValue()) return Out::Failure(pieces.Diagnostics());
                result.pieces = pieces.Value();
            }
            if (inside) for (auto piece : overlap.Value()) { piece.inside = true; result.pieces.push_back(piece); }
            if (result.pieces.empty()) return Out::Failure(MakeError("KER-CT04", "削ると対象がすべて無くなります。", "元の部品は変更していません。"));
        }
        return Out::Success(std::move(result));
    } catch (const Standard_Failure& failure) {
        return Out::Failure(MakeError("KER-CT03", "接触部分を処理できません。", failure.GetMessageString()));
    } catch (...) { return Out::Failure(MakeError("KER-CT03", "接触部分を処理できません。", {})); }
#else
    (void)target; (void)other; (void)wires; (void)outside; (void)inside; (void)tolerance;
    return Out::Failure(MakeError("KER-CT00", "幾何カーネルが必要です。", {}));
#endif
}
}
