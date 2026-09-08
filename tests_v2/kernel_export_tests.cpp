// 部品の書き出し(AT-EXP-010 / 011)。
//
// STEP と STL を同じ形から出し、体積と外接箱が一致することを見る。
// 出せない形は断り、0バイトのファイルを残さない。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/io/AtomicFile.h"
#include "kachakacha/kernel/OcctExtrude.h"
#include "kachakacha/kernel/OcctGuideSurface.h"
#include "kachakacha/kernel/OcctSolidExport.h"

#include <cmath>
#include <filesystem>
#include <string>
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

#endif // KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST_MAIN("kernel_export_tests")
