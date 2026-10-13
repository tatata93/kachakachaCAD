#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/kernel/OcctOutput.h"

#include <algorithm>
#include <type_traits>
#include <cmath>
#include <string>

#ifdef KACHACAD_V2_WITH_OCCT

#include "kachakacha/kernel/OcctShapeCache.h"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <ShapeUpgrade_ShapeDivideContinuity.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeCustom.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <BRepGProp.hxx>
#include "kachakacha/kernel/OcctMassProperties.h"
#include <BRep_Builder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>

#include <Poly_Triangulation.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Compound.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <random>

#endif

namespace kachakacha::v2::kernel {

using base::MakeError;
using base::Result;
using modeling::KernelShapeHandle;

#ifdef KACHACAD_V2_WITH_OCCT

namespace {

template<class Function>
[[nodiscard]] auto Guarded(Function&& body, const char* what) -> decltype(body())
{
    using ResultType = decltype(body());
    try {
        return body();
    } catch (const Standard_Failure& failure) {
        return ResultType::Failure(MakeError(kExportFailed,
            "書き出しに失敗しました。",
            std::string(what) + ": " + std::string(failure.GetMessageString())));
    } catch (const std::exception& error) {
        return ResultType::Failure(MakeError(kExportFailed,
            "書き出しに失敗しました。", std::string(what) + ": " + error.what()));
    } catch (...) {
        return ResultType::Failure(MakeError(kExportFailed,
            "書き出しに失敗しました。", what));
    }
}

[[nodiscard]] double DiagonalOf(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) {
        return 0.0;
    }
    // OCCT は形の許容差ぶんだけ箱を膨らませて返す。
    // 三角形から測った箱と突き合わせるので、その膨らみを外す。
    box.SetGap(0.0);
    double x0 = 0.0;
    double y0 = 0.0;
    double z0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    double z1 = 0.0;
    box.Get(x0, y0, z0, x1, y1, z1);
    return std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)
        + (z1 - z0) * (z1 - z0));
}

//! 一時ファイルを使って STEP の本文を取り出す。
//! OCCT の STEP 書き出しはファイルにしか書けないので、
//! いったん人目に触れない場所へ書いてから読み戻す。
//! 利用者の指定した場所へは、検査を全部通ってから原子的に置く。
[[nodiscard]] std::filesystem::path ScratchPath(const char* suffix)
{
    static std::atomic<unsigned long long> counter{0};
    static const auto nonce = [] {std::random_device r;return
        (static_cast<unsigned long long>(r())<<32)^r();}();
    return std::filesystem::temp_directory_path()
        / ("kachakacha_v2_export_" + std::to_string(nonce)+"_"+std::to_string(++counter) + suffix);
}

[[nodiscard]] std::string ReadAndRemove(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::string content;
    if (stream) {
        content.assign((std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>());
    }
    stream.close();
    std::error_code code;
    std::filesystem::remove(path, code);
    return content;
}

//! OCCT の印字先を一時的に外す。抜けるときに元へ戻す。
//! 型の名前は版によって変わるので、書かずに推論させる。
class SilentMessenger {
public:
    SilentMessenger() : saved_(Message::DefaultMessenger()->Printers())
    {
        Message::DefaultMessenger()->ChangePrinters().Clear();
    }
    ~SilentMessenger()
    {
        Message::DefaultMessenger()->ChangePrinters() = saved_;
    }
    SilentMessenger(const SilentMessenger&) = delete;
    SilentMessenger& operator=(const SilentMessenger&) = delete;

private:
    std::decay_t<decltype(Message::DefaultMessenger()->Printers())> saved_;
};

bool ManifoldEdgeUse(const TopoDS_Shape& shape)
{
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape,TopAbs_EDGE,edges);
    std::vector<int> uses(static_cast<std::size_t>(edges.Extent()+1));
    std::vector<bool> internal(static_cast<std::size_t>(edges.Extent()+1));
    for(TopExp_Explorer face(shape,TopAbs_FACE);face.More();face.Next())
        for(TopExp_Explorer edge(face.Current(),TopAbs_EDGE);edge.More();edge.Next())
            if(!BRep_Tool::Degenerated(TopoDS::Edge(edge.Current()))) {
                const auto index=static_cast<std::size_t>(edges.FindIndex(edge.Current()));
                // INTERNAL edges lie inside a face; they do not bound material.
                // Boolean operations may retain such imprinted curves.
                if(edge.Current().Orientation()==TopAbs_INTERNAL)internal[index]=true;
                else ++uses[index];
            }
    for(int i=1;i<=edges.Extent();++i)
        if(!BRep_Tool::Degenerated(TopoDS::Edge(edges(i)))) {
            const auto index=static_cast<std::size_t>(i);
            if(uses[index]!=2&&!(uses[index]==0&&internal[index]))return false;
        }
    return true;
}

TopoDS_Shape IntersectionAnalysisShape(const TopoDS_Shape& shape,double toleranceMm)
{
    bool denseC0=false;
    for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()) {
        BRepAdaptor_Surface surface(TopoDS::Face(e.Current()));
        if(surface.GetType()==GeomAbs_BSplineSurface&&(surface.NbUIntervals(GeomAbs_C1)>1||surface.NbVIntervals(GeomAbs_C1)>1)) {
            const auto spline=surface.BSpline();
            denseC0=denseC0||spline->NbUPoles()>64||spline->NbVPoles()>64;
        }
    }
    if(!denseC0)return shape;
    try {
    // Subdivide an analysis copy at C0 joins. The stored solid and exact source
    // skin remain unchanged. This avoids solving a giant spline as one patch.
    BRepBuilderAPI_Copy copy(shape,true,false);
    ShapeUpgrade_ShapeDivideContinuity split(copy.Shape());
    split.SetSurfaceCriterion(GeomAbs_C1);split.SetBoundaryCriterion(GeomAbs_C1);
    split.SetPCurveCriterion(GeomAbs_C1);split.SetTolerance(1e-7);split.Perform();
    const double precision=std::clamp(toleranceMm,1e-7,1e-6);
    ShapeFix_Shape fix(split.Result());fix.SetPrecision(1e-7);fix.SetMaxTolerance(precision);fix.Perform();
    const auto analysis=fix.Shape();
    if(analysis.IsNull()||!BRepCheck_Analyzer(analysis).IsValid()||!ManifoldEdgeUse(analysis))return shape;
    GProp_GProps before,after;detail::VolumeProperties(shape,before);detail::VolumeProperties(analysis,after);
    if(std::abs(before.Mass()-after.Mass())>std::max(1e-7,std::abs(before.Mass())*1e-8))return shape;
    Bnd_Box a,b;BRepBndLib::AddOptimal(shape,a,false,false);BRepBndLib::AddOptimal(analysis,b,false,false);
    if(a.IsVoid()||b.IsVoid())return shape;
    double av[6],bv[6];a.Get(av[0],av[1],av[2],av[3],av[4],av[5]);b.Get(bv[0],bv[1],bv[2],bv[3],bv[4],bv[5]);
    for(int i=0;i<6;++i)if(std::abs(av[i]-bv[i])>precision)return shape;
    return analysis;
    } catch(const Standard_Failure&) {return shape;}
      catch(const std::exception&) {return shape;}
}

bool SelfIntersectionFree(const TopoDS_Shape& original,double toleranceMm)
{
    const auto shape=IntersectionAnalysisShape(original,toleranceMm);
    BOPAlgo_ArgumentAnalyzer check;check.SetShape1(shape);check.SelfInterMode()=true;
    check.SetRunParallel(true);check.StopOnFirstFaulty()=true;check.Perform();
    if(!check.HasFaulty()&&!check.HasErrors())return true;
    if(check.HasErrors()||check.GetCheckResult().IsEmpty())return false;
    for(const auto& fault:check.GetCheckResult())
        if(fault.GetCheckStatus()!=BOPAlgo_OperationAborted)return false;
    // Preserve edges and their parameters. Converting all edges to NURBS can
    // create near-coincident knots after a Boolean; only the swept surface
    // representation needs changing to avoid OCCT's extrusion intersection abort.
    const auto converted=ShapeCustom::ConvertToBSpline(shape,true,false,false,false);
    if(converted.IsNull()||!BRepCheck_Analyzer(converted).IsValid())return false;
    GProp_GProps before,after;detail::VolumeProperties(shape,before);
    detail::VolumeProperties(converted,after);
    if(std::abs(before.Mass()-after.Mass())>std::max(1e-7,std::abs(before.Mass())*1e-8))return false;
    Bnd_Box a,b;BRepBndLib::AddOptimal(shape,a,false,false);
    BRepBndLib::AddOptimal(converted,b,false,false);
    if(a.IsVoid()||b.IsVoid())return false;
    double av[6],bv[6];a.Get(av[0],av[1],av[2],av[3],av[4],av[5]);b.Get(bv[0],bv[1],bv[2],bv[3],bv[4],bv[5]);
    for(int i=0;i<6;++i)if(std::abs(av[i]-bv[i])>std::max(toleranceMm,1e-6))return false;
    BOPAlgo_ArgumentAnalyzer retry;retry.SetShape1(converted);retry.SelfInterMode()=true;
    retry.SetRunParallel(true);retry.StopOnFirstFaulty()=true;retry.Perform();
    return !retry.HasFaulty()&&!retry.HasErrors();
}
} // namespace

Result<SolidExportCheck> CheckSolidForExport(KernelShapeHandle handle, double toleranceMm)
{
    TopoDS_Shape shape;
    if (!LookupShape(handle, shape)) {
        return Result<SolidExportCheck>::Failure(MakeError(kExportFailed,
            "その番号の形は表にありません。", {}));
    }
    return Guarded([&]() -> Result<SolidExportCheck> {
        SolidExportCheck check;

        // 1. 閉じているか。開いた殻は中身の無い形なので、部品として出さない。
        std::size_t solids = 0;
        for (TopExp_Explorer explorer(shape, TopAbs_SOLID); explorer.More();
            explorer.Next()) {
            ++solids;
        }
        check.closed = solids > 0 && ManifoldEdgeUse(shape);
        BRepCheck_Analyzer analyzer(shape);
        check.selfIntersectionFree=analyzer.IsValid()==Standard_True;
        if(!check.closed||!check.selfIntersectionFree)return Result<SolidExportCheck>::Success(check);

        // 2. 体積が正か。
        GProp_GProps properties;
        detail::VolumeProperties(shape, properties);
        check.volumeMm3 = properties.Mass();
        const double diagonal = DiagonalOf(shape);
        check.boundingDiagonalMm = diagonal;
        // 大きさに応じた下限。小さい部品でも判定できるようにする。
        const double floorVolume =
            std::max(std::pow(std::max(toleranceMm, 1.0e-6) * 10.0, 3.0), 1.0e-12);
        check.positiveVolume = check.volumeMm3 > floorVolume;

        // 3. 自己交差などの不正が無いか。
        check.selfIntersectionFree = analyzer.IsValid() == Standard_True;
        // BRepCheck validates topology and parameter consistency; it does not
        // perform a geometric self-intersection test. Check every solid itself,
        // leaving contacts between separate assembly parts to assembly checks.
        for (TopExp_Explorer e(shape, TopAbs_SOLID); e.More(); e.Next()) {
            GProp_GProps component;
            if(shape.ShapeType()==TopAbs_SOLID)component=properties;
            else detail::VolumeProperties(e.Current(), component);
            check.positiveVolume = check.positiveVolume && component.Mass() > floorVolume;
            check.selfIntersectionFree = check.selfIntersectionFree
                && SelfIntersectionFree(e.Current(),toleranceMm);
        }

        return Result<SolidExportCheck>::Success(check);
    }, "書き出し前の検査");
}

namespace {

[[nodiscard]] Result<TopoDS_Shape> Checked(KernelShapeHandle handle, double toleranceMm)
{
    TopoDS_Shape shape;
    if (!LookupShape(handle, shape)) {
        return Result<TopoDS_Shape>::Failure(MakeError(kExportFailed,
            "その番号の形は表にありません。", {}));
    }
    auto check = CheckSolidForExport(handle, toleranceMm);
    if (!check.HasValue()) {
        return Result<TopoDS_Shape>::Failure(check.Diagnostics());
    }
    if (!check.Value().closed) {
        return Result<TopoDS_Shape>::Failure(MakeError(kExportOpenShell,
            "閉じていない形は部品として書き出せません。",
            "面の集まりのままです。厚みを付けるか、閉じた形にしてください。"));
    }
    if (!check.Value().positiveVolume) {
        return Result<TopoDS_Shape>::Failure(MakeError(kExportZeroVolume,
            "体積がありません。",
            "体積 " + std::to_string(check.Value().volumeMm3) + " mm3。"));
    }
    if (!check.Value().selfIntersectionFree) {
        return Result<TopoDS_Shape>::Failure(MakeError(kExportSelfIntersecting,
            "形が自分自身と交わっています。",
            "このまま書き出すと、受け取った側で開けません。"));
    }
    return Result<TopoDS_Shape>::Success(shape);
}

} // namespace

Result<std::string> BuildStepText(KernelShapeHandle handle, double toleranceMm)
{
    auto shape = Checked(handle, toleranceMm);
    if (!shape.HasValue()) {
        return Result<std::string>::Failure(shape.Diagnostics());
    }
    return Guarded([&]() -> Result<std::string> {
        // OCCT の STEP 書き出しは標準出力へ統計を吐く。
        // 試験の出力に混ざって読めなくなるので、書いているあいだは黙らせる。
        SilentMessenger silence;
        STEPControl_Writer writer;
        const IFSelect_ReturnStatus transferred =
            writer.Transfer(shape.Value(), STEPControl_AsIs);
        if (transferred != IFSelect_RetDone) {
            return Result<std::string>::Failure(MakeError(kExportFailed,
                "STEP へ移せませんでした。", {}));
        }
        const std::filesystem::path scratch = ScratchPath(".step");
        const IFSelect_ReturnStatus written =
            writer.Write(scratch.string().c_str());
        if (written != IFSelect_RetDone) {
            std::error_code code;
            std::filesystem::remove(scratch, code);
            return Result<std::string>::Failure(MakeError(kExportFailed,
                "STEP を書けませんでした。", {}));
        }
        std::string content = ReadAndRemove(scratch);
        if (content.empty()) {
            return Result<std::string>::Failure(MakeError(kExportFailed,
                "STEP の中身が空でした。", "0バイトのファイルは残しません。"));
        }
        return Result<std::string>::Success(std::move(content));
    }, "STEP の作成");
}

namespace {

//! 三角形を集める。STEP と同じ形から取るので、両者は食い違わない。
struct Triangle {
    gp_Pnt a;
    gp_Pnt b;
    gp_Pnt c;
};

[[nodiscard]] std::vector<Triangle> Triangulate(const TopoDS_Shape& shape,
    double deflectionMm)
{
    BRepMesh_IncrementalMesh mesh(shape, deflectionMm, Standard_False, 0.5,
        Standard_True);
    (void)mesh;
    std::vector<Triangle> triangles;
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
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
                std::swap(second, third);
            }
            Triangle triangle{facets->Node(first).Transformed(transform),
                facets->Node(second).Transformed(transform),
                facets->Node(third).Transformed(transform)};
            triangles.push_back(triangle);
        }
    }
    return triangles;
}

} // namespace

Result<std::string> BuildBinaryStl(KernelShapeHandle handle, double deflectionMm)
{
    auto shape = Checked(handle, std::max(deflectionMm * 0.001, 1.0e-6));
    if (!shape.HasValue()) {
        return Result<std::string>::Failure(shape.Diagnostics());
    }
    // All STL entry points share float-coordinate topology validation.
    return OutputStl({handle}, deflectionMm);
}

Result<MeshMeasure> MeasureMesh(KernelShapeHandle handle, double deflectionMm)
{
    TopoDS_Shape shape;
    if (!LookupShape(handle, shape)) {
        return Result<MeshMeasure>::Failure(MakeError(kExportFailed,
            "その番号の形は表にありません。", {}));
    }
    return Guarded([&]() -> Result<MeshMeasure> {
        const std::vector<Triangle> triangles = Triangulate(shape, deflectionMm);
        MeshMeasure measure;
        measure.triangleCount = triangles.size();
        // 発散定理。原点を頂点とする四面体の符号つき体積を足す。
        double sum = 0.0;
        double minimum[3] = {0.0, 0.0, 0.0};
        double maximum[3] = {0.0, 0.0, 0.0};
        bool first = true;
        for (const Triangle& triangle : triangles) {
            const gp_Vec a(triangle.a.X(), triangle.a.Y(), triangle.a.Z());
            const gp_Vec b(triangle.b.X(), triangle.b.Y(), triangle.b.Z());
            const gp_Vec c(triangle.c.X(), triangle.c.Y(), triangle.c.Z());
            sum += a.Dot(b.Crossed(c)) / 6.0;
            for (const gp_Pnt& point : {triangle.a, triangle.b, triangle.c}) {
                const double values[3] = {point.X(), point.Y(), point.Z()};
                for (int axis = 0; axis < 3; ++axis) {
                    if (first) {
                        minimum[axis] = values[axis];
                        maximum[axis] = values[axis];
                    } else {
                        minimum[axis] = std::min(minimum[axis], values[axis]);
                        maximum[axis] = std::max(maximum[axis], values[axis]);
                    }
                }
                first = false;
            }
        }
        measure.volumeMm3 = std::abs(sum);
        double squared = 0.0;
        for (int axis = 0; axis < 3; ++axis) {
            const double span = maximum[axis] - minimum[axis];
            squared += span * span;
        }
        measure.boundingDiagonalMm = std::sqrt(squared);
        return Result<MeshMeasure>::Success(measure);
    }, "三角形の測定");
}


// ---- 選んだ部材だけを出す(AT-FAB-012) ----

namespace {

//! 選んだ形を1つの集まりへまとめる。数はここで数える。
[[nodiscard]] Result<TopoDS_Compound> GatherSelection(
    const std::vector<KernelShapeHandle>& selected, std::size_t& componentCount,
    double& totalVolume)
{
    using Out = Result<TopoDS_Compound>;
    if (selected.empty()) {
        return Out::Failure(MakeError(kExportFailed,
            "書き出せませんでした。", "出す部材が1つも選ばれていません。"));
    }
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    componentCount = 0;
    totalVolume = 0.0;
    std::vector<std::uint64_t> seen;
    for (const KernelShapeHandle& handle : selected) {
        // 同じ形を2度選んでいたら、2つに数えない。
        if (std::find(seen.begin(), seen.end(), handle.value) != seen.end()) {
            return Out::Failure(MakeError(kExportFailed, "書き出せませんでした。",
                "同じ部材が2度選ばれています。"));
        }
        seen.push_back(handle.value);
        TopoDS_Shape shape;
        if (!LookupShape(handle, shape)) {
            return Out::Failure(MakeError(kExportFailed, "書き出せませんでした。",
                "選ばれた部材が表にありません。"));
        }
        // 選ばれていないものを混ぜない。渡された形だけを足す。
        builder.Add(compound, shape);
        GProp_GProps properties;
        detail::VolumeProperties(shape, properties);
        totalVolume += std::abs(properties.Mass());
        ++componentCount;
    }
    return Out::Success(compound);
}

} // namespace

Result<std::size_t> CountSolidComponents(const std::vector<KernelShapeHandle>& selected)
{
    std::size_t count = 0;
    double volume = 0.0;
    const auto gathered = GatherSelection(selected, count, volume);
    if (!gathered.HasValue()) {
        return Result<std::size_t>::Failure(gathered.Diagnostics());
    }
    // 集まりの中の立体を数え直す。渡した数と合っていることを、読み返して確かめる。
    std::size_t solids = 0;
    for (TopExp_Explorer explorer(gathered.Value(), TopAbs_SOLID); explorer.More();
        explorer.Next()) {
        ++solids;
    }
    return Result<std::size_t>::Success(solids);
}

Result<SelectedExport> BuildStepForSelection(
    const std::vector<KernelShapeHandle>& selected, double toleranceMm)
{
    using Out = Result<SelectedExport>;
    // 1つずつ検査してから出す。壊れたものが混ざったまま出さない。
    for (const KernelShapeHandle& handle : selected) {
        const auto check = CheckSolidForExport(handle, toleranceMm);
        if (!check.HasValue()) {
            return Out::Failure(check.Diagnostics());
        }
    }
    SelectedExport result;
    const auto gathered = GatherSelection(selected, result.componentCount,
        result.totalVolumeMm3);
    if (!gathered.HasValue()) {
        return Out::Failure(gathered.Diagnostics());
    }
    std::string text;
    for (const KernelShapeHandle& handle : selected) {
        const auto part = BuildStepText(handle, toleranceMm);
        if (!part.HasValue()) {
            return Out::Failure(part.Diagnostics());
        }
        text += part.Value();
    }
    result.content = std::move(text);
    return Out::Success(std::move(result));
}

Result<SelectedExport> BuildBinaryStlForSelection(
    const std::vector<KernelShapeHandle>& selected, double deflectionMm)
{
    using Out = Result<SelectedExport>;
    SelectedExport result;
    const auto gathered = GatherSelection(selected, result.componentCount,
        result.totalVolumeMm3);
    if (!gathered.HasValue()) {
        return Out::Failure(gathered.Diagnostics());
    }
    const auto stl = OutputStl(selected, deflectionMm);
    if (!stl.HasValue()) {
        return Out::Failure(stl.Diagnostics());
    }
    result.content = stl.Value();
    return Out::Success(std::move(result));
}

#else // KACHACAD_V2_WITH_OCCT

Result<SolidExportCheck> CheckSolidForExport(KernelShapeHandle handle, double toleranceMm)
{
    (void)handle;
    (void)toleranceMm;
    return Result<SolidExportCheck>::Failure(MakeError(kExportUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

Result<std::string> BuildStepText(KernelShapeHandle handle, double toleranceMm)
{
    (void)handle;
    (void)toleranceMm;
    return Result<std::string>::Failure(MakeError(kExportUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

Result<std::string> BuildBinaryStl(KernelShapeHandle handle, double deflectionMm)
{
    (void)handle;
    (void)deflectionMm;
    return Result<std::string>::Failure(MakeError(kExportUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

Result<MeshMeasure> MeasureMesh(KernelShapeHandle handle, double deflectionMm)
{
    (void)handle;
    (void)deflectionMm;
    return Result<MeshMeasure>::Failure(MakeError(kExportUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

Result<std::size_t> CountSolidComponents(const std::vector<KernelShapeHandle>& selected)
{
    (void)selected;
    return Result<std::size_t>::Failure(MakeError(kExportUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

Result<SelectedExport> BuildStepForSelection(
    const std::vector<KernelShapeHandle>& selected, double toleranceMm)
{
    (void)selected;
    (void)toleranceMm;
    return Result<SelectedExport>::Failure(MakeError(kExportUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

Result<SelectedExport> BuildBinaryStlForSelection(
    const std::vector<KernelShapeHandle>& selected, double deflectionMm)
{
    (void)selected;
    (void)deflectionMm;
    return Result<SelectedExport>::Failure(MakeError(kExportUnsupported,
        "この実行ファイルには幾何カーネルが入っていません。", {}));
}

#endif // KACHACAD_V2_WITH_OCCT

} // namespace kachakacha::v2::kernel
