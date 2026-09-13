#include "kachakacha/kernel/OcctFaceQuery.h"

#include <string>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctCurveConversion.h"
#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <Geom_Surface.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include <exception>

#endif

namespace kachakacha::v2::kernel {
namespace {

using base::MakeError;
using base::Result;

} // namespace

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

//! 面を番号で引く。数え方は OcctTessellate と同じ順にする。
//! 順が食い違うと、押した面と別の面が動く。
[[nodiscard]] bool FindFace(const TopoDS_Shape& shape, std::size_t faceIndex,
    TopoDS_Face& out)
{
    std::size_t index = 0;
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next(),
        ++index) {
        if (index == faceIndex) {
            out = TopoDS::Face(explorer.Current());
            return true;
        }
    }
    return false;
}

} // namespace

Result<std::size_t> ShapeFaceCount(modeling::KernelShapeHandle handle)
{
    using Out = Result<std::size_t>;
    TopoDS_Shape shape;
    if (!LookupShape(handle, shape) || shape.IsNull()) {
        return Out::Failure(MakeError(kFaceSourceMissing, "元になる立体がありません。",
            "先に立体を作ってから、面を選んでください。"));
    }
    std::size_t count = 0;
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        ++count;
    }
    return Out::Success(count);
}

Result<FaceSamples> FaceSamplesOf(modeling::KernelShapeHandle handle,
    std::size_t faceIndex, std::size_t rowCount, std::size_t columnCount)
{
    using Out = Result<FaceSamples>;
    if (rowCount < 2 || columnCount < 2) {
        return Out::Failure(MakeError(kFaceBoundaryUnsupported,
            "標本の数が足りません。", "縦横それぞれ2点以上にしてください。"));
    }
    TopoDS_Shape shape;
    if (!LookupShape(handle, shape) || shape.IsNull()) {
        return Out::Failure(MakeError(kFaceSourceMissing, "元になる立体がありません。",
            "先に立体か面を作ってから、曲がり方を調べてください。"));
    }
    TopoDS_Face face;
    if (!FindFace(shape, faceIndex, face) || face.IsNull()) {
        return Out::Failure(MakeError(kFaceIndexOutOfRange, "その番号の面がありません。",
            {}));
    }
    try {
        occ::handle<Geom_Surface> surface = BRep_Tool::Surface(face);
        if (surface.IsNull()) {
            return Out::Failure(MakeError(kFaceBoundaryUnsupported,
                "面の中身が取れませんでした。", {}));
        }
        double u0 = 0.0;
        double u1 = 0.0;
        double v0 = 0.0;
        double v1 = 0.0;
        BRepTools::UVBounds(face, u0, u1, v0, v1);
        FaceSamples out;
        out.samples.rowCount = rowCount;
        out.samples.columnCount = columnCount;
        out.samples.points.reserve(rowCount * columnCount);
        for (std::size_t row = 0; row < rowCount; ++row) {
            const double u = u0 + (u1 - u0) * static_cast<double>(row)
                / static_cast<double>(rowCount - 1);
            for (std::size_t column = 0; column < columnCount; ++column) {
                const double v = v0 + (v1 - v0) * static_cast<double>(column)
                    / static_cast<double>(columnCount - 1);
                out.samples.points.push_back(FromPoint(surface->Value(u, v)));
            }
        }
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(face, properties);
        out.areaMm2 = properties.Mass();
        return Out::Success(std::move(out));
    } catch (const std::exception& error) {
        return Out::Failure(MakeError(kFaceBoundaryUnsupported,
            std::string("面を標本化できませんでした: ") + error.what(), {}));
    } catch (...) {
        return Out::Failure(MakeError(kFaceBoundaryUnsupported,
            "面を標本化できませんでした。", {}));
    }
}

Result<FaceBoundary> FaceBoundaryOf(modeling::KernelShapeHandle handle,
    std::size_t faceIndex, const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<FaceBoundary>;
    TopoDS_Shape shape;
    if (!LookupShape(handle, shape) || shape.IsNull()) {
        return Out::Failure(MakeError(kFaceSourceMissing, "元になる立体がありません。",
            "先に立体を作ってから、面を選んでください。"));
    }
    TopoDS_Face face;
    if (!FindFace(shape, faceIndex, face) || face.IsNull()) {
        return Out::Failure(MakeError(kFaceIndexOutOfRange,
            "その番号の面がありません。",
            "立体を選び直してから、もう一度面を押してください。"));
    }
    try {
        BRepAdaptor_Surface surface(face, Standard_True);
        if (surface.GetType() != GeomAbs_Plane) {
            return Out::Failure(MakeError(kFaceNotPlanar,
                "平らでない面は押し引きできません。",
                "曲がった面をまっすぐ押すと、元の面と辻褄が合いません。"
                "平らな面を選ぶか、面に厚みを付ける道を使ってください。"));
        }
        const gp_Pln plane = surface.Plane();
        FaceBoundary boundary;
        boundary.origin = FromPoint(plane.Location());
        const gp_Dir normal = plane.Axis().Direction();
        geometry::Vector3 outward{normal.X(), normal.Y(), normal.Z()};
        // 面の向きが裏返っていれば法線も裏返す。
        // 裏返さないと、押したつもりが立体の中へ潜る。
        if (face.Orientation() == TopAbs_REVERSED) {
            outward = outward * -1.0;
        }
        boundary.outwardNormal = outward;

        const TopoDS_Wire outer = BRepTools::OuterWire(face);
        if (outer.IsNull()) {
            return Out::Failure(MakeError(kFaceBoundaryUnsupported,
                "面の外周が取れませんでした。", {}));
        }
        const auto outerCurves = FromWire(outer, tolerance.modelLinearMm);
        if (!outerCurves.HasValue()) {
            return Out::Failure(outerCurves.Diagnostics());
        }
        boundary.outerLoop = outerCurves.Value();
        for (TopExp_Explorer explorer(face, TopAbs_WIRE); explorer.More(); explorer.Next()) {
            const TopoDS_Wire wire = TopoDS::Wire(explorer.Current());
            if (wire.IsSame(outer)) {
                continue;
            }
            const auto hole = FromWire(wire, tolerance.modelLinearMm);
            if (!hole.HasValue()) {
                // 穴が1つでも戻せないなら、黙って落とさずに断る。
                // 落とすと、開いているはずの窓が塞がった形が作られる。
                return Out::Failure(hole.Diagnostics());
            }
            boundary.holeLoops.push_back(hole.Value());
        }
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(face, properties);
        boundary.areaMm2 = properties.Mass();
        return Out::Success(std::move(boundary));
    } catch (const std::exception& error) {
        return Out::Failure(MakeError(kFaceBoundaryUnsupported,
            std::string("面を取り出せませんでした: ") + error.what(), {}));
    } catch (...) {
        return Out::Failure(MakeError(kFaceBoundaryUnsupported,
            "面を取り出せませんでした。", {}));
    }
}

#else

Result<FaceSamples> FaceSamplesOf(modeling::KernelShapeHandle, std::size_t, std::size_t,
    std::size_t)
{
    return Result<FaceSamples>::Failure(MakeError(kFaceSourceMissing,
        "面を標本化できませんでした。",
        "この組み立てには幾何カーネルが入っていません。"));
}

Result<std::size_t> ShapeFaceCount(modeling::KernelShapeHandle)
{
    return Result<std::size_t>::Failure(MakeError(kFaceSourceMissing,
        "面を数えられませんでした。",
        "この組み立てには幾何カーネルが入っていません。"));
}

Result<FaceBoundary> FaceBoundaryOf(modeling::KernelShapeHandle, std::size_t,
    const geometry::GeometryTolerance&)
{
    return Result<FaceBoundary>::Failure(MakeError(kFaceSourceMissing,
        "面を取り出せませんでした。",
        "この組み立てには幾何カーネルが入っていません。"));
}

#endif

} // namespace kachakacha::v2::kernel
