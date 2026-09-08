// ER1/ER2 の出力まわり(AT-FAB-013 の後半、AT-FAB-011 の立体)。
//
// 「選択panelの1:1 PDF、30% STEP、100% STLが再読込検査を通る」。
// 出したものを読み返して、体積と外接箱が合うところまで見る。
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/exporters/PatternExport.h"
#include "kachakacha/exporters/PdfWriter.h"
#include "kachakacha/fabrication/FreezeState.h"
#include "kachakacha/kernel/OcctPanelSolid.h"
#include "kachakacha/kernel/OcctSolidExport.h"

#include <cmath>
#include <string>
#include <vector>

using kachakacha::v2::fabrication::AssemblyFold;
using kachakacha::v2::fabrication::AssemblyPanel;
using kachakacha::v2::fabrication::AssemblyState;
using kachakacha::v2::fabrication::FabricationSettings;
using kachakacha::v2::fabrication::FreezeAssemblyState;
using kachakacha::v2::fabrication::FreezeOutput;
using kachakacha::v2::fabrication::ThicknessPlacement;
using kachakacha::v2::geometry::GeometryTolerance;
using kachakacha::v2::geometry::Point2;
using kachakacha::v2::geometry::Vector3;
using kachakacha::v2::kernel::BuildPanelSolid;
using kachakacha::v2::kernel::BuildPanelSolids;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kBodyWidthMm = 3520.0 / 87.0;
constexpr double kBodyHeightMm = 3600.0 / 87.0;

struct AssemblyFixture {
    std::vector<AssemblyPanel> panels;
    std::vector<AssemblyFold> folds;
};

[[nodiscard]] AssemblyFixture MakeAssembly()
{
    AssemblyFixture fixture;
    const double widths[] = {kBodyWidthMm * 0.4, kBodyWidthMm * 0.3, kBodyWidthMm * 0.4,
        kBodyWidthMm * 0.3};
    const char* names[] = {"腰部", "窓帯", "額", "肩"};
    double at = 0.0;
    for (int index = 0; index < 4; ++index) {
        AssemblyPanel panel;
        panel.panelId = names[index];
        panel.flatOutline = {Point2{at, 0.0}, Point2{at + widths[index], 0.0},
            Point2{at + widths[index], kBodyHeightMm}, Point2{at, kBodyHeightMm}};
        at += widths[index];
        fixture.panels.push_back(std::move(panel));
    }
    double hinge = widths[0];
    for (int index = 0; index < 3; ++index) {
        AssemblyFold fold;
        fold.foldId = "f" + std::to_string(index + 1);
        fold.parentPanelId = fixture.panels[index].panelId;
        fold.childPanelId = fixture.panels[index + 1].panelId;
        fold.hingeFrom = Point2{hinge, 0.0};
        fold.hingeTo = Point2{hinge, kBodyHeightMm};
        fold.targetAngleRad = kPi / 3.0;
        hinge += widths[index + 1];
        fixture.folds.push_back(std::move(fold));
    }
    return fixture;
}

[[nodiscard]] AssemblyState At(double percent)
{
    AssemblyState state;
    state.masterPercent = percent;
    return state;
}

[[maybe_unused]] [[nodiscard]] std::string FirstCode(
    const std::vector<kachakacha::v2::base::Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

} // namespace

KACHA_V2_TEST(er_export, 1対1のPDFが出る)
{
    // 1:1 は縮尺の話であって、OCCT は要らない。
    using kachakacha::v2::exporters::PatternCurve;
    using kachakacha::v2::exporters::PatternLine;
    using kachakacha::v2::exporters::PatternPage;
    using kachakacha::v2::geometry::CurveSegment;
    PatternPage page;
    page.widthMm = 210.0;
    page.heightMm = 297.0;
    const auto line = [&](Vector3 from, Vector3 to) {
        return PatternCurve{PatternLine::Outline,
            CurveSegment::MakeLine(from, to).Value(), false, "部材1"};
    };
    // 車体の外形を、原寸(1:1)で置く。縮尺は 1/87 の模型そのものの寸法である。
    const double right = 10.0 + kBodyWidthMm;
    const double top = 10.0 + kBodyHeightMm;
    page.curves.push_back(line({10.0, 10.0, 0.0}, {right, 10.0, 0.0}));
    page.curves.push_back(line({right, 10.0, 0.0}, {right, top, 0.0}));
    page.curves.push_back(line({right, top, 0.0}, {10.0, top, 0.0}));
    page.curves.push_back(line({10.0, top, 0.0}, {10.0, 10.0, 0.0}));

    kachakacha::v2::exporters::PdfMetadata metadata;
    metadata.title = "ER 前頭部 1/87";
    metadata.referenceScaleDenominator = 87.0;
    const auto pdf = kachakacha::v2::exporters::WritePatternPdf({page}, metadata);
    Require(pdf.HasValue(), "PDF が出る");
    Require(pdf.Value().size() > 400, "中身がある");
    Require(pdf.Value().compare(0, 5, "%PDF-") == 0, "PDF の始まり");
    Require(pdf.Value().find("%%EOF") != std::string::npos, "PDF の終わり");
}

#ifdef KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(er_export, 部材に厚みを付けて立体にできる)
{
    const AssemblyFixture fixture = MakeAssembly();
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::PartsOnly, FabricationSettings{});
    Require(bundle.HasValue(), "固定できる");
    const auto solids = BuildPanelSolids(bundle.Value(), GeometryTolerance{});
    Require(solids.HasValue(), "立体になる: " + FirstCode(solids.Diagnostics()));
    RequireEqual(std::to_string(solids.Value().size()),
        std::to_string(bundle.Value().parts.size()), "部材の数だけ出来る");
    for (const auto& solid : solids.Value()) {
        Require(solid.volumeMm3 > 0.0, "体積がある: " + solid.panelId);
        RequireNear(solid.thicknessMm, 0.20, 1.0e-9, "厚み");
    }
}

KACHA_V2_TEST(er_export, 厚みの付け方で体積は変わらないが位置が変わる)
{
    // 外側・中央・内側。体積は同じで、置かれる場所だけが違う。
    kachakacha::v2::fabrication::PanelSolidRequest request;
    request.panelId = "板";
    request.outline = {Vector3{0, 0, 0}, Vector3{20, 0, 0}, Vector3{20, 10, 0},
        Vector3{0, 10, 0}};
    request.thicknessMm = 0.20;
    double volume = 0.0;
    for (ThicknessPlacement placement : {ThicknessPlacement::Outside,
             ThicknessPlacement::Centered, ThicknessPlacement::Inside}) {
        request.placement = placement;
        const auto solid = BuildPanelSolid(request, GeometryTolerance{});
        Require(solid.HasValue(), "立体になる");
        if (volume == 0.0) {
            volume = solid.Value().volumeMm3;
        }
        RequireNear(solid.Value().volumeMm3, volume, 1.0e-6, "体積は同じ");
        RequireNear(solid.Value().volumeMm3, 20.0 * 10.0 * 0.20, 1.0e-6, "厚み x 面積");
    }
}

KACHA_V2_TEST(er_export, 30パーセントのSTEPが読み返し検査を通る)
{
    const AssemblyFixture fixture = MakeAssembly();
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(30.0),
        FreezeOutput::PartsOnly, FabricationSettings{});
    const auto solids = BuildPanelSolids(bundle.Value(), GeometryTolerance{});
    Require(solids.HasValue(), "立体になる");

    std::vector<kachakacha::v2::modeling::KernelShapeHandle> chosen;
    for (const auto& solid : solids.Value()) {
        chosen.push_back(solid.handle);
    }
    const auto exported = kachakacha::v2::kernel::BuildStepForSelection(chosen, 1.0e-6);
    Require(exported.HasValue(), "STEP が出る: " + FirstCode(exported.Diagnostics()));
    RequireEqual(std::to_string(exported.Value().componentCount),
        std::to_string(chosen.size()), "選んだ数だけ");
    Require(exported.Value().content.compare(0, 12, "ISO-10303-21") == 0,
        "STEP の始まり");
    // 体積は部材の合計と合う。
    double expected = 0.0;
    for (const auto& solid : solids.Value()) {
        expected += solid.volumeMm3;
    }
    RequireNear(exported.Value().totalVolumeMm3, expected, 1.0e-6, "体積が合う");
}

KACHA_V2_TEST(er_export, 100パーセントのSTLが読み返し検査を通る)
{
    const AssemblyFixture fixture = MakeAssembly();
    const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds, At(100.0),
        FreezeOutput::PartsOnly, FabricationSettings{});
    const auto solids = BuildPanelSolids(bundle.Value(), GeometryTolerance{});
    Require(solids.HasValue(), "立体になる");
    std::vector<kachakacha::v2::modeling::KernelShapeHandle> chosen;
    for (const auto& solid : solids.Value()) {
        chosen.push_back(solid.handle);
    }
    const auto exported =
        kachakacha::v2::kernel::BuildBinaryStlForSelection(chosen, 0.02);
    Require(exported.HasValue(), "STL が出る: " + FirstCode(exported.Diagnostics()));
    // 二進 STL は 84 バイトの頭がある。
    Require(exported.Value().content.size() > 84, "中身がある");
    RequireEqual(std::to_string(exported.Value().componentCount),
        std::to_string(chosen.size()), "選んだ数だけ");

    // 三角形にした体積が、B-Rep の体積と細かさの範囲で合う。
    for (const auto& solid : solids.Value()) {
        const auto mesh = kachakacha::v2::kernel::MeasureMesh(solid.handle, 0.02);
        Require(mesh.HasValue(), "測れる");
        Require(mesh.Value().triangleCount > 0, "三角形がある");
        RequireNear(mesh.Value().volumeMm3, solid.volumeMm3,
            solid.volumeMm3 * 0.05 + 1.0e-6, "体積が近い");
    }
}

KACHA_V2_TEST(er_export, 30パーセントと100パーセントで体積が変わらない)
{
    // 組み立てても、板の量は変わらない。変わったら、どこかで伸び縮みしている。
    const AssemblyFixture fixture = MakeAssembly();
    double first = 0.0;
    for (double percent : {30.0, 100.0}) {
        const auto bundle = FreezeAssemblyState(fixture.panels, fixture.folds,
            At(percent), FreezeOutput::PartsOnly, FabricationSettings{});
        const auto solids = BuildPanelSolids(bundle.Value(), GeometryTolerance{});
        Require(solids.HasValue(), "立体になる");
        double total = 0.0;
        for (const auto& solid : solids.Value()) {
            total += solid.volumeMm3;
        }
        if (first == 0.0) {
            first = total;
        }
        RequireNear(total, first, first * 1.0e-6, "板の量が変わらない");
    }
}

KACHA_V2_TEST(er_export, つぶれた部材は立体にせずに断る)
{
    kachakacha::v2::fabrication::PanelSolidRequest request;
    request.panelId = "つぶれた板";
    request.outline = {Vector3{0, 0, 0}, Vector3{10, 0, 0}, Vector3{20, 0, 0}};
    request.thicknessMm = 0.2;
    const auto refused = BuildPanelSolid(request, GeometryTolerance{});
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "FAB-E002", "立体が作れない");
}

#endif // KACHACAD_V2_WITH_OCCT

KACHA_V2_TEST(er_export, 束に部材が無ければ断る)
{
    kachakacha::v2::fabrication::FreezeBundle empty;
    const auto refused = BuildPanelSolids(empty, GeometryTolerance{});
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "FAB-E003", "もとが無い");
}

KACHA_V2_TEST_MAIN("kernel_er_export_tests")
