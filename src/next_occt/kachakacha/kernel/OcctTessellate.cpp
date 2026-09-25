#include "kachakacha/kernel/OcctTessellate.h"

#include <algorithm>
#include <cmath>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <Geom_Surface.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopAbs_State.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>

#endif

namespace kachakacha::v2::kernel {

using base::MakeError;
using base::Result;
using modeling::MeshTriangle;
using modeling::ShapeMesh;
using geometry::Vector3;

double DeflectionForSize(double boundingDiagonalMm) noexcept
{
    // 対角の 1/500 を狙う。0.02mm より細かくしても画面では見えず、
    // 5mm より粗いと円が多角形に見える。その間に収める。
    if (!std::isfinite(boundingDiagonalMm) || boundingDiagonalMm <= 0.0) {
        return 0.1;
    }
    return std::clamp(boundingDiagonalMm / 500.0, 0.02, 5.0);
}

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

[[nodiscard]] Vector3 ToVector(const gp_Pnt& point)
{
    return Vector3{point.X(), point.Y(), point.Z()};
}

//! 外接箱の対角。粗さを決めるのに使う。
[[nodiscard]] double DiagonalOf(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) {
        return 0.0;
    }
    double minX = 0.0;
    double minY = 0.0;
    double minZ = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    double maxZ = 0.0;
    box.Get(minX, minY, minZ, maxX, maxY, maxZ);
    return std::sqrt((maxX - minX) * (maxX - minX) + (maxY - minY) * (maxY - minY)
        + (maxZ - minZ) * (maxZ - minZ));
}

//! 面を三角形にする。STL と同じ道なので、画面と出力が食い違わない。
void CollectTriangles(const TopoDS_Shape& shape, ShapeMesh& mesh)
{
    // 面ごとに番号を振る。三角形の番号では面にならない ── 1枚の面が
    // 何十枚もの三角形になるので、押すたびに違うものが選ばれてしまう。
    // 番号は辿った順。**組み立て直すと変わりうる** ので、文書には残さない。
    std::size_t faceIndex = 0;
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next(),
        ++faceIndex) {
        const TopoDS_Face face = TopoDS::Face(explorer.Current());
        TopLoc_Location location;
        const occ::handle<Poly_Triangulation> facets =
            BRep_Tool::Triangulation(face, location);
        if (facets.IsNull()) {
            continue;
        }
        const gp_Trsf transform = location.Transformation();
        const bool reversed = face.Orientation() == TopAbs_REVERSED;
        for (int index = 1; index <= facets->NbTriangles(); ++index) {
            int first = 0;
            int second = 0;
            int third = 0;
            facets->Triangle(index).Get(first, second, third);
            if (reversed) {
                // 裏返しの面は頂点の順を入れ替える。入れ替えないと法線が内向きになり、
                // 閉じた立体の裏表が逆に塗られる。
                std::swap(second, third);
            }
            MeshTriangle triangle;
            triangle.faceIndex = faceIndex;
            triangle.points[0] = ToVector(facets->Node(first).Transformed(transform));
            triangle.points[1] = ToVector(facets->Node(second).Transformed(transform));
            triangle.points[2] = ToVector(facets->Node(third).Transformed(transform));
            mesh.triangles.push_back(triangle);
        }
    }
    mesh.faceCount = faceIndex;
}

//! 辺を折れ線にする。直線は2点、曲線は粗さに合わせて刻む。
void CollectEdges(const TopoDS_Shape& shape, double deflectionMm, ShapeMesh& mesh)
{
    for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
        BRepAdaptor_Curve curve(edge);
        std::vector<Vector3> line;
        try {
            GCPnts_QuasiUniformDeflection walk(curve, deflectionMm);
            if (walk.IsDone() && walk.NbPoints() >= 2) {
                for (int index = 1; index <= walk.NbPoints(); ++index) {
                    line.push_back(ToVector(walk.Value(index)));
                }
            }
        } catch (const Standard_Failure&) {
            line.clear();
        }
        if (line.size() < 2) {
            // 刻めない辺は両端だけでも出す。出さないと形の縁が消える。
            const double first = curve.FirstParameter();
            const double last = curve.LastParameter();
            if (!std::isfinite(first) || !std::isfinite(last)) {
                continue;
            }
            line = {ToVector(curve.Value(first)), ToVector(curve.Value(last))};
        }
        mesh.edges.push_back(std::move(line));
    }
}

//! 閉じた立体か。閉じていれば裏を向いた三角形は描かなくてよい。
[[nodiscard]] bool IsClosedSolid(const TopoDS_Shape& shape)
{
    for (TopExp_Explorer explorer(shape, TopAbs_SOLID); explorer.More(); explorer.Next()) {
        return true;
    }
    return false;
}

} // namespace

std::vector<std::vector<Vector3>> FaceIsoLines(const TopoDS_Face& face, int count)
{
    std::vector<std::vector<Vector3>> lines;
    if (count <= 0 || face.IsNull()) {
        return lines;
    }
    double u0 = 0.0;
    double u1 = 0.0;
    double v0 = 0.0;
    double v1 = 0.0;
    BRepTools::UVBounds(face, u0, u1, v0, v1);
    const occ::handle<Geom_Surface> surface = BRep_Tool::Surface(face);
    if (surface.IsNull() || !(u1 > u0) || !(v1 > v0)) {
        return lines;
    }
    constexpr int kSteps = 64;
    // 1 本の線を面の内側だけの折れ線にする(トリムで外に出たところで切る)。
    const auto add = [&](bool alongU, double fixed, double from, double to) {
        std::vector<Vector3> run;
        for (int k = 0; k <= kSteps; ++k) {
            const double at = from + (to - from) * k / kSteps;
            const double u = alongU ? at : fixed;
            const double v = alongU ? fixed : at;
            const bool inside =
                BRepClass_FaceClassifier(face, gp_Pnt2d(u, v), 1.0e-7).State() != TopAbs_OUT;
            if (inside) {
                run.push_back(ToVector(surface->Value(u, v)));
                continue;
            }
            if (run.size() >= 2) {
                lines.push_back(run);
            }
            run.clear();
        }
        if (run.size() >= 2) {
            lines.push_back(run);
        }
    };
    for (int k = 1; k <= count; ++k) {
        add(true, v0 + (v1 - v0) * k / (count + 1), u0, u1);
        add(false, u0 + (u1 - u0) * k / (count + 1), v0, v1);
    }
    return lines;
}

Result<ShapeMesh> BuildShapeMesh(modeling::KernelShapeHandle handle, double deflectionMm,
    int isoLinesPerDirection)
{
    TopoDS_Shape shape;
    if (!LookupShape(handle, shape)) {
        return Result<ShapeMesh>::Failure(MakeError(kTessellateUnknownShape,
            "画面に出す形が見つかりません。",
            "作り直しの途中かもしれません。もう一度出し直してください。"));
    }
    try {
        const double chosen = deflectionMm > 0.0 ? deflectionMm
                                                 : DeflectionForSize(DiagonalOf(shape));
        BRepMesh_IncrementalMesh mesh(shape, chosen, Standard_False, 0.5, Standard_True);
        (void)mesh;
        ShapeMesh made;
        made.closed = IsClosedSolid(shape);
        CollectTriangles(shape, made);
        CollectEdges(shape, chosen, made);
        if (isoLinesPerDirection > 0) {
            for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next()) {
                auto lines = FaceIsoLines(TopoDS::Face(faces.Current()), isoLinesPerDirection);
                made.isoLines.insert(made.isoLines.end(), lines.begin(), lines.end());
            }
        }
        if (made.Empty()) {
            return Result<ShapeMesh>::Failure(MakeError(kTessellateFailed,
                "その形は画面に出せませんでした。",
                "三角形も稜線も取れませんでした。"));
        }
        modeling::RefreshNormals(made);
        modeling::RefreshBounds(made);
        return Result<ShapeMesh>::Success(std::move(made));
    } catch (const Standard_Failure& failure) {
        return Result<ShapeMesh>::Failure(MakeError(kTessellateFailed,
            "その形は画面に出せませんでした。", failure.GetMessageString()));
    }
}

#else

Result<ShapeMesh> BuildShapeMesh(modeling::KernelShapeHandle, double, int)
{
    return Result<ShapeMesh>::Failure(MakeError(kTessellateFailed,
        "その形は画面に出せませんでした。",
        "この組み立てには幾何カーネルが入っていません。"));
}

#endif

} // namespace kachakacha::v2::kernel
