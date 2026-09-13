#include "kachakacha/kernel/OcctFaceAdjacency.h"

#include <algorithm>
#include <cmath>
#include <string>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <Geom2d_Curve.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>

#include <exception>
#include <map>
#include <utility>

#endif

namespace kachakacha::v2::kernel {
namespace {

using base::MakeError;
using base::Result;

} // namespace

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

//! 辺の長さ(mm)。取れなければ 0。
[[nodiscard]] double EdgeLengthMm(const TopoDS_Edge& edge)
{
    try {
        BRepAdaptor_Curve curve(edge);
        return GCPnts_AbscissaPoint::Length(curve);
    } catch (...) {
        return 0.0;
    }
}

//! 面の、その辺の上での外向き法線。取れなければ値を返さない。
[[nodiscard]] bool NormalAtEdge(const TopoDS_Face& face, const TopoDS_Edge& edge,
    gp_Dir& out)
{
    try {
        Standard_Real first = 0.0;
        Standard_Real last = 0.0;
        const auto curve2d = BRep_Tool::CurveOnSurface(edge, face, first, last);
        if (curve2d.IsNull()) {
            return false;
        }
        const Standard_Real middle = 0.5 * (first + last);
        gp_Pnt2d uv;
        curve2d->D0(middle, uv);
        BRepAdaptor_Surface surface(face, Standard_True);
        BRepLProp_SLProps properties(surface, uv.X(), uv.Y(), 1, 1.0e-7);
        if (!properties.IsNormalDefined()) {
            return false;
        }
        gp_Dir normal = properties.Normal();
        // 面が裏返っていれば法線も裏返す。裏返さないと折り角の符号が揃わない。
        if (face.Orientation() == TopAbs_REVERSED) {
            normal.Reverse();
        }
        out = normal;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

Result<std::vector<fabrication::PanelAdjacency>> FaceAdjacenciesOf(
    modeling::KernelShapeHandle handle, const geometry::GeometryTolerance& tolerance)
{
    using Out = Result<std::vector<fabrication::PanelAdjacency>>;
    (void)tolerance;
    TopoDS_Shape shape;
    if (!LookupShape(handle, shape) || shape.IsNull()) {
        return Out::Failure(MakeError(kAdjacencySourceMissing, "元になる形がありません。",
            "先に立体か面を作ってから、部材の分け方を調べてください。"));
    }
    try {
        // 面の番号を、他の層と同じ順(TopExp_Explorer の順)で決める。
        // TopExp::MapShapes は並びが違うので、番号付けには使わない。
        std::vector<TopoDS_Face> faces;
        for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
            faces.push_back(TopoDS::Face(explorer.Current()));
        }
        if (faces.size() < 2) {
            return Out::Success({});   // 隣り合わせは無い。断る理由ではない。
        }
        TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);

        // 面の対ごとに、共有辺の長さを足し、折り角は一番長い辺のものを採る。
        struct Pair {
            double lengthMm = 0.0;
            double longestEdgeMm = 0.0;
            double angleRad = 0.0;
        };
        std::map<std::pair<std::size_t, std::size_t>, Pair> pairs;
        std::size_t skippedNonManifold = 0;
        for (Standard_Integer index = 1; index <= edgeToFaces.Extent(); ++index) {
            const TopTools_ListOfShape& owners = edgeToFaces.FindFromIndex(index);
            if (owners.Extent() != 2) {
                if (owners.Extent() > 2) {
                    ++skippedNonManifold;
                }
                continue;
            }
            const TopoDS_Edge edge = TopoDS::Edge(edgeToFaces.FindKey(index));
            std::vector<std::size_t> found;
            for (TopTools_ListOfShape::Iterator owner(owners); owner.More(); owner.Next()) {
                for (std::size_t face = 0; face < faces.size(); ++face) {
                    if (faces[face].IsSame(owner.Value())) {
                        found.push_back(face);
                        break;
                    }
                }
            }
            if (found.size() != 2 || found[0] == found[1]) {
                continue;
            }
            const std::size_t first = std::min(found[0], found[1]);
            const std::size_t second = std::max(found[0], found[1]);
            const double lengthMm = EdgeLengthMm(edge);
            Pair& pair = pairs[{first, second}];
            pair.lengthMm += lengthMm;
            if (lengthMm >= pair.longestEdgeMm) {
                pair.longestEdgeMm = lengthMm;
                gp_Dir firstNormal;
                gp_Dir secondNormal;
                if (NormalAtEdge(faces[first], edge, firstNormal)
                    && NormalAtEdge(faces[second], edge, secondNormal)) {
                    pair.angleRad = firstNormal.Angle(secondNormal);
                }
            }
        }
        std::vector<fabrication::PanelAdjacency> result;
        result.reserve(pairs.size());
        for (const auto& entry : pairs) {
            fabrication::PanelAdjacency adjacency;
            adjacency.firstIndex = entry.first.first;
            adjacency.secondIndex = entry.first.second;
            adjacency.sharedEdgeLengthMm = entry.second.lengthMm;
            adjacency.dihedralAngleRad = entry.second.angleRad;
            result.push_back(adjacency);
        }
        // std::map なので、すでに(小さい番号、大きい番号)の昇順である。
        if (skippedNonManifold > 0) {
            // 飛ばしたことは黙らない。黙ると、繋がっていない部材が黙って出来る。
            const auto note = base::MakeWarning(kAdjacencyFailed,
                std::string("3枚以上の面が同じ辺を共有している所が ")
                    + std::to_string(skippedNonManifold) + " か所あります。",
                "どちらへ折るのかが決められないので、その辺は隣り合わせに数えていません。");
            return Out::Success(std::move(result), {note});
        }
        return Out::Success(std::move(result));
    } catch (const std::exception& error) {
        return Out::Failure(MakeError(kAdjacencyFailed,
            std::string("隣り合わせを数えられませんでした: ") + error.what(), {}));
    } catch (...) {
        return Out::Failure(MakeError(kAdjacencyFailed,
            "隣り合わせを数えられませんでした。", {}));
    }
}

#else

Result<std::vector<fabrication::PanelAdjacency>> FaceAdjacenciesOf(
    modeling::KernelShapeHandle, const geometry::GeometryTolerance&)
{
    return Result<std::vector<fabrication::PanelAdjacency>>::Failure(
        MakeError(kAdjacencySourceMissing, "隣り合わせを数えられませんでした。",
            "この組み立てには幾何カーネルが入っていません。"));
}

#endif

} // namespace kachakacha::v2::kernel
