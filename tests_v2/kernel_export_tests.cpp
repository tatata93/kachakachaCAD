// 部品の書き出し(AT-EXP-010 / 011)。
//
// STEP と STL を同じ形から出し、体積と外接箱が一致することを見る。
// 出せない形は断り、0バイトのファイルを残さない。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/kernel/OcctExtrude.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctSolidExport.h"
#include "kachakacha/kernel/OcctOutput.h"
#include "kachakacha/kernel/OcctTessellate.h"
#include "kachakacha/kernel/OcctMeshCache.h"
#include "kachakacha/kernel/OcctBoundaryDistance.h"
#include "kachakacha/kernel/OcctTrimmedExtents.h"
#include "kachakacha/kernel/OcctShapeTransfer.h"
#ifdef KACHACAD_V2_WITH_OCCT
#include "kachakacha/kernel/OcctShapeCache.h"
#include <BRep_Builder.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepLib.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_Curve.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec2d.hxx>
#include <gp_Dir2d.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS.hxx>
#include <BRep_Tool.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <STEPControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#endif

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>

using kachakacha::v2::geometry::CurveSegment;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::geometry::kPi;
using kachakacha::v2::kernel::BuildBinaryStl;
using kachakacha::v2::kernel::BuildExtrude;
using kachakacha::v2::kernel::BuildStepText;
using kachakacha::v2::kernel::CheckSolidForExport;
using kachakacha::v2::kernel::ClearShapeCache;
using kachakacha::v2::kernel::MeasureMesh;
using kachakacha::v2::modeling::AnalyzeExtrudeRequest;
using kachakacha::v2::modeling::ExtrudeDirectionMode;
using kachakacha::v2::modeling::ExtrudeExtentMode;
using kachakacha::v2::modeling::ExtrudeProfile;
using kachakacha::v2::modeling::ExtrudeRequest;
using kachakacha::v2::modeling::KernelShapeHandle;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[maybe_unused]] [[nodiscard]] GeometryTolerance Tolerance()
{
    GeometryTolerance tolerance;
    tolerance.modelLinearMm = 1.0e-6;
    return tolerance;
}

[[maybe_unused]] [[nodiscard]] CurveSegment Line(Vector3 a, Vector3 b)
{
    const auto made = CurveSegment::MakeLine(a, b);
    Require(made.HasValue(), "直線が作れること");
    return made.Value();
}

[[maybe_unused]] [[nodiscard]] ExtrudeProfile Rectangle(double x0, double y0, double x1,
    double y1)
{
    ExtrudeProfile profile;
    profile.closed = true;
    profile.segments = {
        Line({x0, y0, 0}, {x1, y0, 0}),
        Line({x1, y0, 0}, {x1, y1, 0}),
        Line({x1, y1, 0}, {x0, y1, 0}),
        Line({x0, y1, 0}, {x0, y0, 0}),
    };
    return profile;
}

[[maybe_unused]] [[nodiscard]] std::string FirstCode(
    const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

[[maybe_unused]] [[nodiscard]] KernelShapeHandle MakeBox(double width, double depth,
    double height)
{
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, width, depth)};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = height;
    request.outputs.part = true;
    auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "入力検査が通ること");
    auto built = BuildExtrude(request, analysis.Value(), Tolerance());
    Require(built.HasValue(), "箱が作れること");
    return built.Value().parts.front().handle;
}

[[maybe_unused]] [[nodiscard]] KernelShapeHandle MakeCylinder(double radius,
    double height)
{
    ExtrudeProfile profile;
    profile.closed = true;
    const auto circle =
        CurveSegment::MakeCircle({0, 0, 0}, {0, 0, 1}, {1, 0, 0}, radius);
    Require(circle.HasValue(), "円が作れること");
    profile.segments = {circle.Value()};
    ExtrudeRequest request;
    request.profiles = {profile};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = height;
    request.outputs.part = true;
    auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "入力検査が通ること");
    auto built = BuildExtrude(request, analysis.Value(), Tolerance());
    Require(built.HasValue(), "円柱が作れること");
    return built.Value().parts.front().handle;
}

} // namespace

#ifndef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(kernel_export_absent, カーネルが無い版は書き出さずに断る)
{
    const auto step = BuildStepText(KernelShapeHandle{1}, 1.0e-6);
    Require(!step.HasValue(), "書き出せたことにしない");
    RequireEqual(FirstCode(step.Diagnostics()), "EXP-014", "診断コード");
}

#else

KACHA_V2_TEST(kernel_export, stl_refuses_edge_touching_solids_but_keeps_independent_parts)
{
    using namespace kachakacha::v2::kernel;
    const TopoDS_Shape a=BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),10,10,10).Shape();
    const TopoDS_Shape b=BRepPrimAPI_MakeBox(gp_Pnt(10,10,0),10,10,10).Shape();
    TopoDS_Compound compound;BRep_Builder builder;
    builder.MakeCompound(compound);builder.Add(compound,a);builder.Add(compound,b);
    const auto joined=StoreShape(compound),first=StoreShape(a),second=StoreShape(b);
    Require(!OutputStl({joined},.03).HasValue(), "an edge-only material contact is not one printable boundary");
    Require(!BuildBinaryStl(joined,.03).HasValue(), "the other STL entry point applies the same check");
    Require(OutputStl({first,second},.03).HasValue(), "explicit independent parts remain separately validated");
    Require(OutputStl({first},.03).HasValue(), "a closed ordinary box exports");
    ReleaseShape(joined);ReleaseShape(first);ReleaseShape(second);
}

// =====================================================================
//  AT-EXP-010 STEP と STL が同じ形になる
// =====================================================================

KACHA_V2_TEST(kernel_export, 箱のSTEPが書き出せる)
{
    ClearShapeCache();
    const KernelShapeHandle box = MakeBox(40, 20, 30);
    const auto step = BuildStepText(box, 1.0e-6);
    Require(step.HasValue(), "書き出せること");
    Require(step.Value().rfind("ISO-10303-21", 0) == 0, "STEP の見出しで始まる");
    Require(step.Value().find("END-ISO-10303-21") != std::string::npos,
        "STEP の終わりがある");
    Require(step.Value().size() > 1000, "中身がある");
}

KACHA_V2_TEST(kernel_export, 箱のSTLが書き出せる)
{
    const KernelShapeHandle box = MakeBox(40, 20, 30);
    const auto stl = BuildBinaryStl(box, 0.05);
    Require(stl.HasValue(), "書き出せること");
    Require(stl.Value().size() >= 84, "見出しと個数がある");
    // 二進STL: 80バイトの見出し + 4バイトの個数 + 50バイト x 三角形。
    const unsigned char* bytes =
        reinterpret_cast<const unsigned char*>(stl.Value().data());
    const std::size_t count = static_cast<std::size_t>(bytes[80])
        | (static_cast<std::size_t>(bytes[81]) << 8)
        | (static_cast<std::size_t>(bytes[82]) << 16)
        | (static_cast<std::size_t>(bytes[83]) << 24);
    RequireEqual(std::to_string(stl.Value().size()), std::to_string(84 + count * 50),
        "長さが個数と合う");
    RequireEqual(std::to_string(count), "12", "箱は12枚の三角形");
}

KACHA_V2_TEST(kernel_export, 箱の三角形の体積が厳密に合う)
{
    const KernelShapeHandle box = MakeBox(40, 20, 30);
    const auto measure = MeasureMesh(box, 0.05);
    Require(measure.HasValue(), "測れること");
    RequireNear(measure.Value().volumeMm3, 24000.0, 1.0e-6, "体積");
    RequireNear(measure.Value().boundingDiagonalMm,
        std::sqrt(40.0 * 40.0 + 20.0 * 20.0 + 30.0 * 30.0), 1.0e-6, "外接箱の対角");
}

KACHA_V2_TEST(kernel_export, 円柱の三角形の体積が出力精度内で合う)
{
    ClearShapeCache();
    const KernelShapeHandle cylinder = MakeCylinder(10.0, 25.0);
    const auto check = CheckSolidForExport(cylinder, 1.0e-6);
    Require(check.HasValue(), "検査が通ること");
    const double exact = kPi * 100.0 * 25.0;
    RequireNear(check.Value().volumeMm3, exact, 1.0e-6, "B-Rep の体積は厳密");

    const auto measure = MeasureMesh(cylinder, 0.01);
    Require(measure.HasValue(), "測れること");
    // 三角形は内側に寄るので、厳密値より少し小さい。出力精度内であること。
    Require(measure.Value().volumeMm3 <= exact, "三角形は内側");
    Require(measure.Value().volumeMm3 >= exact * 0.999, "0.1%以内");
}

KACHA_V2_TEST(kernel_export, 精度を上げると三角形の体積が厳密値へ近づく)
{
    const KernelShapeHandle cylinder = MakeCylinder(10.0, 25.0);
    const double exact = kPi * 100.0 * 25.0;
    const auto coarse = MeasureMesh(cylinder, 0.2);
    const auto fine = MeasureMesh(cylinder, 0.005);
    Require(coarse.HasValue() && fine.HasValue(), "両方測れること");
    const double coarseError = std::abs(coarse.Value().volumeMm3 - exact);
    const double fineError = std::abs(fine.Value().volumeMm3 - exact);
    Require(fineError < coarseError, "細かくすると近づく");
    Require(fine.Value().triangleCount > coarse.Value().triangleCount,
        "細かくすると三角形が増える");
}

KACHA_V2_TEST(kernel_export, STEPとSTLの外接箱が一致する)
{
    const KernelShapeHandle box = MakeBox(40, 20, 30);
    const auto check = CheckSolidForExport(box, 1.0e-6);
    const auto measure = MeasureMesh(box, 0.01);
    Require(check.HasValue() && measure.HasValue(), "両方測れること");
    RequireNear(measure.Value().boundingDiagonalMm, check.Value().boundingDiagonalMm,
        1.0e-6, "外接箱の対角");
}

KACHA_V2_TEST(kernel_export, 穴つきの部品でも体積が一致する)
{
    ClearShapeCache();
    ExtrudeProfile hole;
    hole.closed = true;
    const auto circle = CurveSegment::MakeCircle({20, 10, 0}, {0, 0, 1}, {1, 0, 0}, 5.0);
    Require(circle.HasValue(), "円が作れること");
    hole.segments = {circle.Value()};
    ExtrudeRequest request;
    request.profiles = {Rectangle(0, 0, 40, 20), hole};
    request.directionMode = ExtrudeDirectionMode::WorldZ;
    request.extent = ExtrudeExtentMode::Distance;
    request.distanceMm = 30.0;
    request.outputs.part = true;
    auto analysis = AnalyzeExtrudeRequest(request, Tolerance());
    Require(analysis.HasValue(), "入力検査が通ること");
    auto built = BuildExtrude(request, analysis.Value(), Tolerance());
    Require(built.HasValue(), "作れること");
    const KernelShapeHandle handle = built.Value().parts.front().handle;

    const double exact = (800.0 - kPi * 25.0) * 30.0;
    const auto measure = MeasureMesh(handle, 0.005);
    Require(measure.HasValue(), "測れること");
    Require(std::abs(measure.Value().volumeMm3 - exact) / exact < 1.0e-3,
        "三角形の体積が0.1%以内");
    Require(BuildStepText(handle, 1.0e-6).HasValue(), "STEP も出せる");
}

// =====================================================================
//  AT-EXP-011 出せない形
// =====================================================================

KACHA_V2_TEST(kernel_export, 開いた殻は断る)
{
    ClearShapeCache();
    // 形状ガイド(面1枚)は閉じていない。部品として出してはならない。
    using kachakacha::v2::modeling::AnalyzeGuideSurfaceRequest;
    using kachakacha::v2::modeling::ChainRole;
    using kachakacha::v2::modeling::GuideChain;
    using kachakacha::v2::modeling::GuideSurfaceMethod;
    using kachakacha::v2::modeling::GuideSurfaceRequest;

    GuideSurfaceRequest request;
    request.method = GuideSurfaceMethod::PlanarBoundary;
    GuideChain chain;
    chain.role = ChainRole::OuterBoundary;
    chain.index = 1;
    chain.closed = true;
    chain.segments = {
        Line({0, 0, 0}, {40, 0, 0}),
        Line({40, 0, 0}, {40, 20, 0}),
        Line({40, 20, 0}, {0, 20, 0}),
        Line({0, 20, 0}, {0, 0, 0}),
    };
    request.chains.push_back(chain);
    auto analysis = AnalyzeGuideSurfaceRequest(request, Tolerance());
    Require(analysis.HasValue(), "入力検査が通ること");
    auto surface = kachakacha::v2::kernel::BuildGuideSurface(request, analysis.Value(),
        Tolerance());
    Require(surface.HasValue(), "面が作れること");

    const auto step = BuildStepText(surface.Value().handle, 1.0e-6);
    Require(!step.HasValue(), "書き出せたことにしない");
    RequireEqual(FirstCode(step.Diagnostics()), "EXP-010", "開いた殻の診断コード");

    const auto stl = BuildBinaryStl(surface.Value().handle, 0.05);
    Require(!stl.HasValue(), "STL も断る");
    RequireEqual(FirstCode(stl.Diagnostics()), "EXP-010", "同じ診断コード");
}

KACHA_V2_TEST(kernel_export, 表に無い番号は断る)
{
    const auto step = BuildStepText(KernelShapeHandle{123456789}, 1.0e-6);
    Require(!step.HasValue(), "書き出せたことにしない");
    RequireEqual(FirstCode(step.Diagnostics()), "EXP-013", "診断コード");
}

KACHA_V2_TEST(kernel_export, 検査の内訳が取れる)
{
    ClearShapeCache();
    const KernelShapeHandle box = MakeBox(40, 20, 30);
    const auto check = CheckSolidForExport(box, 1.0e-6);
    Require(check.HasValue(), "検査できること");
    Require(check.Value().closed, "閉じている");
    Require(check.Value().positiveVolume, "体積がある");
    Require(check.Value().selfIntersectionFree, "自己交差が無い");
    Require(check.Value().Ok(), "総合で合格");
}

KACHA_V2_TEST(kernel_export, とても小さい部品でも体積があると判定する)
{
    ClearShapeCache();
    const KernelShapeHandle small = MakeBox(0.5, 0.2, 0.1);
    const auto check = CheckSolidForExport(small, 1.0e-6);
    Require(check.HasValue(), "検査できること");
    Require(check.Value().positiveVolume, "体積がある");
    RequireNear(check.Value().volumeMm3, 0.01, 1.0e-9, "体積");
    Require(BuildStepText(small, 1.0e-6).HasValue(), "書き出せる");
}

// =====================================================================
//  0バイトのファイルを残さない
// =====================================================================

KACHA_V2_TEST(kernel_export, 断ったときにファイルを作らない)
{
    using kachakacha::v2::io::WriteFileAtomically;

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "kachakacha_v2_export_refuse";
    std::error_code code;
    std::filesystem::remove_all(directory, code);
    std::filesystem::create_directories(directory, code);
    const std::string path = (directory / "part.step").string();

    // 中身を作る段で断るので、ファイルを開くところまで行かない。
    const auto step = BuildStepText(KernelShapeHandle{999999}, 1.0e-6);
    Require(!step.HasValue(), "断ること");
    Require(!std::filesystem::exists(std::filesystem::path(path)),
        "ファイルが作られていない");

    // 通ったときだけ、原子的に置く。
    ClearShapeCache();
    const KernelShapeHandle box = MakeBox(10, 10, 10);
    const auto good = BuildStepText(box, 1.0e-6);
    Require(good.HasValue(), "書き出せること");
    Require(WriteFileAtomically(path, good.Value()).HasValue(), "置けること");
    Require(std::filesystem::file_size(std::filesystem::path(path)) > 0,
        "0バイトではない");
    std::filesystem::remove_all(directory, code);
    ClearShapeCache();
}

KACHA_V2_TEST(kernel_export, 同じ部品からは毎回同じSTLが出る)
{
    ClearShapeCache();
    const KernelShapeHandle box = MakeBox(40, 20, 30);
    const auto first = BuildBinaryStl(box, 0.05);
    Require(first.HasValue(), "1回目");
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto again = BuildBinaryStl(box, 0.05);
        Require(again.HasValue(), "くり返し");
        Require(again.Value() == first.Value(), "1バイトも変わらない");
    }
    ClearShapeCache();
}


// ---- 選んだ部材だけを出す(AT-FAB-012) ----

KACHA_V2_TEST(kernel_export, 10部材のうち2つを選ぶと2つだけ出る)
{
    using kachakacha::v2::kernel::BuildStepForSelection;
    using kachakacha::v2::kernel::CountSolidComponents;
    ClearShapeCache();
    // 10個の箱を作る。大きさを変えて、どれが出たか見分けられるようにする。
    std::vector<KernelShapeHandle> all;
    for (int index = 0; index < 10; ++index) {
        all.push_back(MakeBox(10.0 + index, 10.0, 10.0));
    }
    RequireEqual(std::to_string(all.size()), "10", "10部材");

    // 2つだけ選ぶ。
    const std::vector<KernelShapeHandle> chosen{all[2], all[7]};
    const auto counted = CountSolidComponents(chosen);
    Require(counted.HasValue(), "数えられる");
    RequireEqual(std::to_string(counted.Value()), "2", "塊は2つだけ");

    const auto exported = BuildStepForSelection(chosen, Tolerance().modelLinearMm);
    Require(exported.HasValue(), "出せる");
    RequireEqual(std::to_string(exported.Value().componentCount), "2", "2つ");
    Require(!exported.Value().content.empty(), "中身がある");
    STEPControl_Reader reader;std::istringstream input(exported.Value().content);
    Require(reader.ReadStream("selected.step",input)==IFSelect_RetDone&&reader.TransferRoots(),"one STEP model rereads");
    std::size_t restored=0;for(TopExp_Explorer e(reader.OneShape(),TopAbs_SOLID);e.More();e.Next())++restored;
    Require(restored==2,"STEP reader restores both selected solids, not just the first document");
    // 体積は選んだ2つぶんだけ。選ばなかった8つが混ざっていない。
    const double expected = (10.0 + 2) * 10.0 * 10.0 + (10.0 + 7) * 10.0 * 10.0;
    RequireNear(exported.Value().totalVolumeMm3, expected, 1e-6,
        "選んだ2つぶんの体積だけ");
}

KACHA_V2_TEST(kernel_export, 選んだ部材だけをSTLへ出す)
{
    using kachakacha::v2::kernel::BuildBinaryStlForSelection;
    ClearShapeCache();
    std::vector<KernelShapeHandle> all;
    for (int index = 0; index < 5; ++index) {
        all.push_back(MakeBox(10.0 + index, 10.0, 10.0));
    }
    const std::vector<KernelShapeHandle> chosen{all[0], all[3]};
    const auto exported = BuildBinaryStlForSelection(chosen, 0.05);
    Require(exported.HasValue(), "出せる");
    RequireEqual(std::to_string(exported.Value().componentCount), "2", "2つ");
    Require(exported.Value().content.size() > 84, "中身がある");
    const double expected = 10.0 * 10.0 * 10.0 + 13.0 * 10.0 * 10.0;
    RequireNear(exported.Value().totalVolumeMm3, expected, 1e-6, "選んだ2つぶん");
}

KACHA_V2_TEST(kernel_export, 1つも選ばなければ出さずに断る)
{
    using kachakacha::v2::kernel::BuildStepForSelection;
    using kachakacha::v2::kernel::CountSolidComponents;
    const auto refusedCount = CountSolidComponents({});
    Require(!refusedCount.HasValue(), "断る");
    RequireEqual(FirstCode(refusedCount.Diagnostics()), "EXP-013", "書き出せない");
    const auto refused = BuildStepForSelection({}, Tolerance().modelLinearMm);
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "EXP-013", "書き出せない");
}

KACHA_V2_TEST(kernel_export, 同じ部材を2度選んだら断る)
{
    using kachakacha::v2::kernel::BuildStepForSelection;
    ClearShapeCache();
    const KernelShapeHandle box = MakeBox(10.0, 10.0, 10.0);
    const auto refused = BuildStepForSelection({box, box}, Tolerance().modelLinearMm);
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "EXP-013", "2度選んだ");
}

KACHA_V2_TEST(kernel_export, 表にない部材を選んだら断る)
{
    using kachakacha::v2::kernel::BuildStepForSelection;
    ClearShapeCache();
    const KernelShapeHandle box = MakeBox(10.0, 10.0, 10.0);
    const auto refused = BuildStepForSelection({box, KernelShapeHandle{}},
        Tolerance().modelLinearMm);
    Require(!refused.HasValue(), "断る");
}

KACHA_V2_TEST(kernel_export, 選んだ数を変えれば出る数も変わる)
{
    using kachakacha::v2::kernel::CountSolidComponents;
    ClearShapeCache();
    std::vector<KernelShapeHandle> all;
    for (int index = 0; index < 6; ++index) {
        all.push_back(MakeBox(10.0 + index, 10.0, 10.0));
    }
    for (std::size_t howMany = 1; howMany <= all.size(); ++howMany) {
        const std::vector<KernelShapeHandle> chosen(all.begin(),
            all.begin() + static_cast<std::ptrdiff_t>(howMany));
        const auto counted = CountSolidComponents(chosen);
        Require(counted.HasValue(), "数えられる");
        RequireEqual(std::to_string(counted.Value()), std::to_string(howMany),
            "選んだ数と同じ");
    }
}

KACHA_V2_TEST(kernel_export, internal_face_imprint_is_not_an_open_boundary)
{
    // The real six-part can union retains an INTERNAL face imprint. Only its
    // cached triangulation was removed; geometry and topology are unchanged.
    std::ifstream input(std::filesystem::path(KACHACAD_TEST_FIXTURE_DIR)/"can_internal_imprint.brep");
    Require(input.good(),"Regression fixture exists");
    TopoDS_Shape box;BRep_Builder builder;BRepTools::Read(box,input,builder);
    Require(!box.IsNull(),"Regression fixture reads");
    bool imprint=false;
    for(TopExp_Explorer f(box,TopAbs_FACE);f.More();f.Next())
        for(TopExp_Explorer e(f.Current(),TopAbs_EDGE);e.More();e.Next())
            imprint=imprint||e.Current().Orientation()==TopAbs_INTERNAL;
    Require(imprint,"Can union contains an internal curve on the outer face");
    Require(BRepCheck_Analyzer(box).IsValid(),"Internal face curve is valid BRep input");
    const auto handle=kachakacha::v2::kernel::StoreShape(box);
    const auto check=CheckSolidForExport(handle,1e-6);
    Require(check.HasValue()&&check.Value().Ok(),"Internal imprint does not open solid boundary");
    RequireNear(check.Value().volumeMm3,6282.177935961462,1e-7,"Internal curve does not change material volume");
    Require(BuildBinaryStl(handle,.01).HasValue(),"Internal imprint preserves closed STL output");
}

KACHA_V2_TEST(kernel_export, validated_cache_detects_native_mutation_and_selected_shape_changes)
{
    auto box=BRepPrimAPI_MakeBox(2,3,4).Shape();
    const auto handle=kachakacha::v2::kernel::StoreShape(box);
    const auto before=kachakacha::v2::kernel::CaptureOutputShape(handle);
    const auto first=kachakacha::v2::kernel::OutputStep({handle});
    const auto after=kachakacha::v2::kernel::CaptureOutputShape(handle);
    Require(before.HasValue()&&after.HasValue()&&before.Value()==after.Value(),"STEP preserves native input bytes");
    const auto repeated=kachakacha::v2::kernel::OutputStep({handle});
    Require(first.HasValue(),"first STEP: "+first.FirstSummaryJa());
    Require(repeated.HasValue(),"repeated STEP: "+repeated.FirstSummaryJa());
    Require(first.Value()==repeated.Value(),"same validated native shape is reusable");
    const auto other=kachakacha::v2::kernel::StoreShape(BRepPrimAPI_MakeBox(5,3,4).Shape());
    const auto changed=kachakacha::v2::kernel::OutputStep({other});
    Require(changed.HasValue()&&changed.Value()!=first.Value(),"different geometry never reuses the old STEP");
    TopExp_Explorer vertices(box,TopAbs_VERTEX); Require(vertices.More(),"box has a vertex");
    const auto vertex=TopoDS::Vertex(vertices.Current());
    BRep_Builder builder;builder.UpdateVertex(vertex,gp_Pnt(100,100,100),1e-6);
    const auto check=CheckSolidForExport(handle,1e-6);
    Require(!check.HasValue()||!check.Value().Ok(),"mutating a shared native shape invalidates the validation cache");
    Require(!kachakacha::v2::kernel::OutputStep({handle}).HasValue(),"a cached good STEP never hides new invalid geometry");
    kachakacha::v2::kernel::ReleaseShape(handle);kachakacha::v2::kernel::ReleaseShape(other);
}

KACHA_V2_TEST(kernel_export, rigid_output_copy_preserves_geometry_and_ownership)
{
    using namespace kachakacha::v2;
    const auto source=kernel::StoreShape(BRepPrimAPI_MakeBox(2,3,4).Shape());
    const auto before=kernel::CaptureOutputShape(source);Require(before.HasValue(),"native box captured");
    geometry::OutputPlacement placement;placement.keepPosition=false;
    placement.source.origin={1,2,3};placement.destination={{30,20,10},{0,1,0},{1,0,0}};
    const auto moved=kernel::PlaceOutputShape(source,placement);Require(moved.HasValue(),"rigid copy placed");
    TopoDS_Shape shape;Require(kernel::LookupShape(moved.Value(),shape),"placed native shape exists");
    GProp_GProps facts;BRepGProp::VolumeProperties(shape,facts);
    RequireNear(facts.Mass(),24,1e-8,"rotation and translation preserve volume");
    const auto center=placement.Point({1,1.5,2});
    RequireNear(facts.CentreOfMass().Distance(gp_Pnt(center.x,center.y,center.z)),0,1e-8,"native centroid follows frame placement");
    Require(kernel::OutputStep({moved.Value()}).HasValue(),"placed native copy survives STEP readback");
    TopExp_Explorer vertices(shape,TopAbs_VERTEX);Require(vertices.More(),"copy has a vertex");
    BRep_Builder builder;builder.UpdateVertex(TopoDS::Vertex(vertices.Current()),gp_Pnt(100,100,100),1e-6);
    const auto after=kernel::CaptureOutputShape(source);
    Require(after.HasValue()&&before.Value()==after.Value(),"mutating output copy never changes source topology or geometry");
    kernel::ReleaseShape(source);kernel::ReleaseShape(moved.Value());
}

KACHA_V2_TEST(kernel_export, mesh_reuse_checks_geometry_quality_and_handle_lifetime)
{
    using namespace kachakacha::v2;
    kernel::ClearShapeCache();
    auto shape=BRepPrimAPI_MakeCylinder(4,9).Shape();
    const auto source=kernel::StoreShape(shape);
    const auto original=kernel::CaptureOutputShape(source);
    const auto first=kernel::BuildShapeMesh(source,.02);
    Require(first.HasValue(),"native mesh is built");
    BRepBuilderAPI_Copy copy(shape,true,false);
    const auto duplicate=kernel::StoreShape(copy.Shape());
    const auto repeated=kernel::BuildShapeMesh(duplicate,.02);
    Require(repeated.HasValue()&&kernel::detail::MeshCacheHits()==1,"equivalent private copy reuses exact mesh");
    Require(first.Value().triangles.size()==repeated.Value().triangles.size(),"same native face indexing and triangles");
    const auto fine=kernel::BuildShapeMesh(source,.002);
    Require(fine.HasValue()&&fine.Value().triangles.size()>first.Value().triangles.size(),"finer quality is remeshed");
    const auto iso=kernel::BuildShapeMesh(source,.02,2);
    Require(iso.HasValue()&&!iso.Value().isoLines.empty(),"isoparametric lines are a separate quality key");
    Require(kernel::detail::MeshCacheHits()==1,"quality changes never hit an old mesh");
    const auto after=kernel::CaptureOutputShape(source);
    Require(original.HasValue()&&after.HasValue()&&original.Value()==after.Value(),"meshing never mutates native geometry");
    TopExp_Explorer vertices(shape,TopAbs_VERTEX);Require(vertices.More(),"cylinder has a vertex");
    BRep_Builder builder;builder.UpdateVertex(TopoDS::Vertex(vertices.Current()),gp_Pnt(100,100,100),1e-6);
    (void)kernel::BuildShapeMesh(source,.02);
    Require(kernel::detail::MeshCacheHits()==1,"shared native mutation invalidates content key");
    kernel::ReleaseShape(duplicate);
    Require(!kernel::BuildShapeMesh(duplicate,.02).HasValue(),"released handle cannot retrieve a cached mesh");
    kernel::ClearShapeCache();
    Require(kernel::detail::MeshCacheHits()==0,"document cache reset also releases meshes");
}

KACHA_V2_TEST(kernel_export, boundary_coincidence_is_only_a_finite_trim_upper_bound)
{
    using namespace kachakacha::v2;
    const auto cylinder=BRepPrimAPI_MakeCylinder(4,9).Shape();
    const kernel::detail::NativeBoundaryDistance probe(cylinder);
    for(double a:{.03,.3,1.4,2.8,4.9,6.2}) {
        const auto distance=probe.Coincident(gp_Pnt(4*std::cos(a),4*std::sin(a),9),1e-9);
        Require(distance&&*distance<=1e-9,"curved native trim is located");
    }
    Require(probe.Coincident(gp_Pnt(4*std::cos(.3),4*std::sin(.3),4.5),1e-9).has_value(),
        "coincident point is classified inside the finite face");
    Require(!probe.Coincident(gp_Pnt(4.01*std::cos(.3),4.01*std::sin(.3),9),1e-9),
        "displaced boundary cannot pass as coincidence");
    Require(!probe.Coincident(gp_Pnt(4,0,12),1e-9),"infinite continuation of seam is outside finite trim");
    modeling::GuideSurfaceRequest request;request.method=modeling::GuideSurfaceMethod::PlanarBoundary;
    modeling::GuideChain outer;outer.role=modeling::ChainRole::OuterBoundary;outer.index=1;outer.closed=true;
    outer.segments=Rectangle(0,0,10,10).segments;
    modeling::GuideChain hole;hole.role=modeling::ChainRole::HoleBoundary;hole.index=1;hole.closed=true;
    hole.segments={CurveSegment::MakeCircle({5,5,0},{0,0,1},{1,0,0},2).Value()};
    request.chains={outer,hole};
    const auto analysis=modeling::AnalyzeGuideSurfaceRequest(request,Tolerance());Require(analysis.HasValue(),"hole input accepted");
    const auto made=kernel::BuildGuideSurface(request,analysis.Value(),Tolerance());Require(made.HasValue(),"finite perforated face");
    TopoDS_Shape face;Require(kernel::LookupShape(made.Value().handle,face),"native trimmed face exists");
    const kernel::detail::NativeBoundaryDistance trimmed(face);
    Require(!trimmed.Coincident(gp_Pnt(5,5,0),1e-9),"infinite supporting surface does not fill the hole");
    Require(!trimmed.Coincident(gp_Pnt(6.9999,5,0),1e-9),"point just inside the hole is not material");
    Require(trimmed.Coincident(gp_Pnt(7.0001,5,0),1e-9).has_value(),"material just outside the hole is classified");
    Require(!trimmed.Coincident(gp_Pnt(11,5,0),1e-9),"supporting plane beyond outer trim is excluded");
    kernel::ReleaseShape(made.Value().handle);
    NCollection_Array2<gp_Pnt> poles(1,2,1,2);
    for(int i=1;i<=2;++i)for(int j=1;j<=2;++j)poles(i,j)=gp_Pnt((i-1)*1e6,(j-1)*1e6,0);
    NCollection_Array1<double> knots(1,2);knots(1)=0;knots(2)=1e-6;
    NCollection_Array1<int> mults(1,2);mults(1)=mults(2)=2;
    const occ::handle<Geom_Surface> scaled=new Geom_BSplineSurface(poles,knots,knots,mults,mults,1,1);
    const std::array<gp_Pnt2d,3> points{{{0,0},{1e-6,0},{0,1e-6}}};BRepBuilderAPI_MakeWire triangle;
    for(std::size_t i=0;i<3;++i) {
        const auto a=points[i],b=points[(i+1)%3];const gp_Vec2d d(a,b);
        const occ::handle<Geom2d_Curve> line=new Geom2d_Line(a,gp_Dir2d(d));
        triangle.Add(BRepBuilderAPI_MakeEdge(line,scaled,0,d.Magnitude()).Edge());
    }
    BRepBuilderAPI_MakeFace scaledFace(scaled,triangle.Wire(),true);
    Require(scaledFace.IsDone()&&BRepLib::BuildCurves3d(scaledFace.Face()),"tiny UV domain on large physical face");
    const kernel::detail::NativeBoundaryDistance scaledProbe(scaledFace.Face());
    Require(!scaledProbe.Coincident(gp_Pnt(5e5,5e5+.0001,0),1e-7),
        "UV ON tolerance cannot admit an outside point farther than physical precision");
}

KACHA_V2_TEST(kernel_export, trimmed_native_extents_keep_holes_and_interior_surface_extrema)
{
    namespace kernel=kachakacha::v2::kernel;
    // A radius-five sphere bored by a radius-three cylinder is an eight-high
    // napkin ring. Z extrema lie on the hole boundary; Y extrema lie inside
    // the curved face. Neither full UV rectangles nor edges alone are enough.
    const auto sphere=BRepPrimAPI_MakeSphere(gp_Pnt(20,-13,7),5).Shape();
    const auto bore=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20,-13,1),gp_Dir(0,0,1)),3,12).Shape();
    BRepAlgoAPI_Cut cut(sphere,bore);Require(cut.IsDone(),"analytic holed sphere can be built");
    const auto handle=kernel::StoreShape(cut.Shape());
    const auto before=kernel::CaptureOutputShape(handle);Require(before.HasValue(),"analytic source captured");
    const double expected[6]={15,-18,3,25,-8,11};
    for(int component=0;component<6;++component) {
        const auto extent=kernel::detail::TrimmedCoordinateExtent(cut.Shape(),component);
        Require(extent.has_value(),"trimmed sphere extent exists");
        RequireNear(*extent,expected[component],1e-6,"trimmed spherical material matches analytic bounds");
    }
    Require(!kernel::detail::TrimmedCoordinateExtent(TopoDS_Shape{},0),"null native shape is rejected safely");
    Require(!kernel::detail::TrimmedCoordinateExtent(cut.Shape(),6),"invalid axis is rejected safely");
    const auto exported=kernel::OutputStep({handle});Require(exported.HasValue(),exported.FirstSummaryJa());
    RequireEqual(kernel::CaptureOutputShape(handle).Value(),before.Value(),"native bounds and STEP leave source bytes unchanged");
    kernel::ReleaseShape(handle);
}

KACHA_V2_TEST(kernel_export, legacy_offset_refusal_preserves_original)
{
    using namespace kachakacha::v2;
    std::ifstream input(std::filesystem::path(KACHACAD_TEST_FIXTURE_DIR)/"offset-panel-c0.brep");
    Require(input.good(),"C0 offset regression fixture exists");
    TopoDS_Shape shape;BRep_Builder builder;BRepTools::Read(shape,input,builder);
    Require(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"original C0 panel is valid");
    const auto handle=kernel::StoreShape(shape);
    const auto before=kernel::CaptureOutputShape(handle);Require(before.HasValue(),"source captured");
    const auto step=kernel::OutputStep({handle});
    Require(!step.HasValue()&&step.FirstCode()=="EXP-P003","old offset remains a precise refusal until rebuilt");
    const auto after=kernel::CaptureOutputShape(handle);
    Require(after.HasValue()&&before.Value()==after.Value(),"export copy preserves original surface, trims and IDs");
    kernel::ReleaseShape(handle);
}

KACHA_V2_TEST(kernel_export, binary_ipc_preserves_native_geometry_and_rejects_corruption_before_restore)
{
    using namespace kachakacha::v2;
    auto source=BRepPrimAPI_MakeBox(10,20,30).Shape();
    gp_Trsf place;place.SetTranslation(gp_Vec(1234.56789,-987.654321,.123456789));source.Move(place);
    source.Reverse();const auto handle=kernel::StoreShape(source);
    const auto text=kernel::CaptureOutputShape(handle),bytes=kernel::CaptureTransferShape(handle);
    Require(text.HasValue()&&bytes.HasValue(),"native text and disposable binary capture");
    const auto restored=kernel::RestoreTransferShape(bytes.Value());Require(restored.HasValue(),"complete binary restore");
    const auto exact=kernel::CaptureOutputShape(restored.Value());
    Require(exact.HasValue()&&exact.Value()==text.Value(),"position orientation tolerances curves and topology retain exact native text");
    kernel::ReleaseShape(restored.Value());const auto count=kernel::CachedShapeCount();
    Require(!kernel::RestoreTransferShape(bytes.Value()+"extra").HasValue(),"extra bytes refused");
    for(std::size_t n:{0u,1u,16u,40u})Require(!kernel::RestoreTransferShape(bytes.Value().substr(0,n)).HasValue(),"truncated header refused");
    for(std::size_t i=0;i<bytes.Value().size();++i) {
        auto corrupt=bytes.Value();corrupt[i]^=1;
        Require(!kernel::RestoreTransferShape(corrupt).HasValue(),"every single-byte corruption rejected before native reader");
    }
    Require(kernel::CachedShapeCount()==count,"failed restore never leaks a native handle");
    Require(kernel::CaptureOutputShape(handle).Value()==text.Value(),"source native geometry unchanged");
    kernel::ReleaseShape(handle);
    Require(!kernel::CaptureTransferShape(handle).HasValue(),"released handles are not restored from a stale cache");
    std::ifstream input(std::filesystem::path(KACHACAD_TEST_FIXTURE_DIR)/"offset-panel-c0.brep");
    TopoDS_Shape curved;BRep_Builder builder;BRepTools::Read(curved,input,builder);
    Require(!curved.IsNull(),"complex C0 trimmed native solid fixture");
    const auto complex=kernel::StoreShape(curved);const auto original=kernel::CaptureOutputShape(complex);
    const auto binary=kernel::CaptureTransferShape(complex);Require(binary.HasValue(),"offset carrier binary capture");
    const auto recovered=kernel::RestoreTransferShape(binary.Value());Require(recovered.HasValue(),"offset carrier exact restore");
    Require(original.HasValue()&&kernel::CaptureOutputShape(recovered.Value()).Value()==original.Value(),
        "complex curved skins and all native trims survive without STEP healing or approximation");
    kernel::ReleaseShape(complex);kernel::ReleaseShape(recovered.Value());
}

#endif // KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST_MAIN("kernel_export_tests")
