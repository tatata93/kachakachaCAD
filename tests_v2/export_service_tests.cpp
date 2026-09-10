// 書き出しの段取り(ui-workflows §11、WP-11)。
#include "kachakacha/app/ExportService.h"
#include "kachakacha/base/TestHarness.h"
#include "kachakacha/io/AtomicFile.h"

#include <filesystem>
#include <set>
#include <string>

using kachakacha::v2::app::AllowedFormatsFor;
using kachakacha::v2::app::ExportFormat;
using kachakacha::v2::app::ExportFormatAllowed;
using kachakacha::v2::app::ExportFormatExtension;
using kachakacha::v2::app::ExportFormatIsText;
using kachakacha::v2::app::ExportFormatNameJa;
using kachakacha::v2::app::ExportRequest;
using kachakacha::v2::app::ExportTarget;
using kachakacha::v2::app::ExportTargetNameJa;
using kachakacha::v2::app::ResolveExportPath;
using kachakacha::v2::app::RunExport;
using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

const ExportTarget kTargets[] = {ExportTarget::VisibleParts, ExportTarget::SelectedParts,
    ExportTarget::SelectedFabricationPanels, ExportTarget::CurrentPattern,
    ExportTarget::SelectedWires, ExportTarget::SelectedEntities, ExportTarget::Project};

const ExportFormat kFormats[] = {ExportFormat::Stl, ExportFormat::Step,
    ExportFormat::Svg, ExportFormat::Dxf, ExportFormat::Pdf, ExportFormat::Kcd2};

//! 試験用の作業場所。使い終わったら消す。
struct Scratch {
    std::string path;

    explicit Scratch(const std::string& name)
    {
        path = kachakacha::v2::io::FromPath(
            std::filesystem::temp_directory_path() / ("kacha_export_" + name));
    }
    ~Scratch()
    {
        std::error_code code;
        std::filesystem::remove(kachakacha::v2::io::MakePath(path), code);
        std::filesystem::remove(kachakacha::v2::io::MakePath(path + ".step"), code);
        std::filesystem::remove(kachakacha::v2::io::MakePath(path + ".svg"), code);
    }
};

} // namespace

KACHA_V2_TEST(export_service, 対象と形式に名前がそろっている)
{
    std::set<std::string> targets;
    for (ExportTarget target : kTargets) {
        const std::string name{ExportTargetNameJa(target)};
        Require(!name.empty() && name != "不明", "名前がある");
        Require(targets.insert(name).second, "重ならない");
    }
    std::set<std::string> formats;
    std::set<std::string> extensions;
    for (ExportFormat format : kFormats) {
        const std::string name{ExportFormatNameJa(format)};
        Require(!name.empty() && name != "不明", "名前がある");
        Require(formats.insert(name).second, "重ならない");
        const std::string extension{ExportFormatExtension(format)};
        Require(extension.size() > 1 && extension.front() == '.', "拡張子がある");
        Require(extensions.insert(extension).second, "拡張子も重ならない");
    }
}

KACHA_V2_TEST(export_service, 契約どおりの組合せだけ出せる)
{
    // Part は STL と STEP。Wire と型紙は SVG / DXF / PDF。文書は kcd2。
    Require(ExportFormatAllowed(ExportTarget::SelectedParts, ExportFormat::Stl), "部品STL");
    Require(ExportFormatAllowed(ExportTarget::SelectedParts, ExportFormat::Step), "部品STEP");
    Require(!ExportFormatAllowed(ExportTarget::SelectedParts, ExportFormat::Svg),
        "部品をSVGでは出さない");
    Require(ExportFormatAllowed(ExportTarget::CurrentPattern, ExportFormat::Svg), "型紙SVG");
    Require(ExportFormatAllowed(ExportTarget::CurrentPattern, ExportFormat::Pdf), "型紙PDF");
    Require(!ExportFormatAllowed(ExportTarget::CurrentPattern, ExportFormat::Stl),
        "型紙をSTLでは出さない");
    Require(ExportFormatAllowed(ExportTarget::Project, ExportFormat::Kcd2), "文書kcd2");
    Require(!ExportFormatAllowed(ExportTarget::Project, ExportFormat::Step),
        "文書をSTEPでは出さない");
}

KACHA_V2_TEST(export_service, どの対象にも出せる形式が1つはある)
{
    for (ExportTarget target : kTargets) {
        const auto formats = AllowedFormatsFor(target);
        Require(!formats.empty(),
            std::string("出せる形式がある: ") + std::string(ExportTargetNameJa(target)));
        // 並びは毎回同じ。
        const auto again = AllowedFormatsFor(target);
        RequireEqual(std::to_string(formats.size()), std::to_string(again.size()),
            "同じ数");
        for (std::size_t at = 0; at < formats.size(); ++at) {
            Require(formats[at] == again[at], "同じ並び");
        }
    }
}

KACHA_V2_TEST(export_service, 製作部材はどちらの形式でも出せる)
{
    // 立体としても型紙としても出す。契約が両方を挙げている。
    const auto formats = AllowedFormatsFor(ExportTarget::SelectedFabricationPanels);
    Require(formats.size() >= 5, "5通り以上");
    bool hasSolid = false;
    bool hasFlat = false;
    for (ExportFormat format : formats) {
        hasSolid = hasSolid || format == ExportFormat::Step;
        hasFlat = hasFlat || format == ExportFormat::Svg;
    }
    Require(hasSolid && hasFlat, "立体も型紙も");
}

KACHA_V2_TEST(export_service, 拡張子を足すが勝手に直さない)
{
    RequireEqual(ResolveExportPath("a/b/model", ExportFormat::Step).Value(),
        "a/b/model.step", "無ければ足す");
    RequireEqual(ResolveExportPath("a/b/model.step", ExportFormat::Step).Value(),
        "a/b/model.step", "あればそのまま");
    RequireEqual(ResolveExportPath("a/b/model.STEP", ExportFormat::Step).Value(),
        "a/b/model.STEP", "大文字でもそのまま");
    // .stp を .step へ勝手に直さない。足すだけ。
    RequireEqual(ResolveExportPath("a/b/model.stp", ExportFormat::Step).Value(),
        "a/b/model.stp.step", "直さずに足す");
}

KACHA_V2_TEST(export_service, 名前が空なら断る)
{
    const auto refused = ResolveExportPath("", ExportFormat::Step);
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "EXP-016", "先が決まっていない");
}

KACHA_V2_TEST(export_service, 対象0なら中身を作らせずに断る)
{
    Scratch scratch("empty");
    bool made = false;
    ExportRequest request;
    request.target = ExportTarget::SelectedParts;
    request.format = ExportFormat::Step;
    request.targetCount = 0;
    request.path = scratch.path;
    const auto refused = RunExport(request, [&]() {
        made = true;
        return kachakacha::v2::base::Result<std::string>::Success("なにか");
    });
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "EXP-015", "選ばれていない");
    Require(!made, "中身を作らせない");
    Require(!kachakacha::v2::io::PathExists(scratch.path + ".step"), "ファイルも作らない");
}

KACHA_V2_TEST(export_service, 出せない組合せは中身を作らせずに断る)
{
    Scratch scratch("wrong");
    bool made = false;
    ExportRequest request;
    request.target = ExportTarget::CurrentPattern;
    request.format = ExportFormat::Stl;
    request.targetCount = 3;
    request.path = scratch.path;
    const auto refused = RunExport(request, [&]() {
        made = true;
        return kachakacha::v2::base::Result<std::string>::Success("なにか");
    });
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "EXP-017", "その形式では出せない");
    Require(!made, "中身を作らせない");
}

KACHA_V2_TEST(export_service, 通れば書ける)
{
    Scratch scratch("ok");
    ExportRequest request;
    request.target = ExportTarget::SelectedParts;
    request.format = ExportFormat::Step;
    request.targetCount = 2;
    request.path = scratch.path;
    const auto written = RunExport(request, []() {
        return kachakacha::v2::base::Result<std::string>::Success(
            "ISO-10303-21;\nHEADER;\n");
    });
    Require(written.HasValue(), "書ける");
    RequireEqual(written.Value().path, scratch.path + ".step", "拡張子が付く");
    Require(written.Value().byteCount > 0, "中身がある");
    Require(kachakacha::v2::io::PathExists(written.Value().path), "ファイルがある");
    const auto read = kachakacha::v2::io::ReadWholeFile(written.Value().path);
    Require(read.HasValue(), "読み返せる");
    RequireEqual(read.Value(), "ISO-10303-21;\nHEADER;\n", "同じ中身");
}

KACHA_V2_TEST(export_service, 上書きは確かめてから)
{
    Scratch scratch("overwrite");
    ExportRequest request;
    request.target = ExportTarget::SelectedParts;
    request.format = ExportFormat::Step;
    request.targetCount = 1;
    request.path = scratch.path;
    const auto first = RunExport(request,
        []() { return kachakacha::v2::base::Result<std::string>::Success("1回目"); });
    Require(first.HasValue(), "1回目は書ける");

    bool made = false;
    const auto refused = RunExport(request, [&]() {
        made = true;
        return kachakacha::v2::base::Result<std::string>::Success("2回目");
    });
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "EXP-018", "既にある");
    Require(!made, "中身も作らせない");
    // 1回目の中身は残っている。
    RequireEqual(kachakacha::v2::io::ReadWholeFile(first.Value().path).Value(), "1回目",
        "壊されていない");

    request.overwrite = true;
    const auto second = RunExport(request,
        []() { return kachakacha::v2::base::Result<std::string>::Success("2回目"); });
    Require(second.HasValue(), "許せば書ける");
    RequireEqual(kachakacha::v2::io::ReadWholeFile(second.Value().path).Value(), "2回目",
        "書き換わる");
}

KACHA_V2_TEST(export_service, 中身が空ならファイルを作らない)
{
    Scratch scratch("blank");
    ExportRequest request;
    request.target = ExportTarget::SelectedParts;
    request.format = ExportFormat::Step;
    request.targetCount = 1;
    request.path = scratch.path;
    const auto refused = RunExport(request,
        []() { return kachakacha::v2::base::Result<std::string>::Success(""); });
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "EXP-013", "書き出せない");
    Require(!kachakacha::v2::io::PathExists(scratch.path + ".step"),
        "0バイトのファイルを残さない");
}

KACHA_V2_TEST(export_service, 中身を作るところで断られたらそのまま伝える)
{
    Scratch scratch("inner");
    ExportRequest request;
    request.target = ExportTarget::SelectedParts;
    request.format = ExportFormat::Step;
    request.targetCount = 1;
    request.path = scratch.path;
    const auto refused = RunExport(request, []() {
        return kachakacha::v2::base::Result<std::string>::Failure(
            kachakacha::v2::base::MakeError("EXP-011", "体積がありません。", {}));
    });
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "EXP-011", "中の理由をそのまま");
    Require(!kachakacha::v2::io::PathExists(scratch.path + ".step"), "ファイルも無い");
}

KACHA_V2_TEST(export_service, 中身を作る手立てが無ければ断る)
{
    Scratch scratch("nomaker");
    ExportRequest request;
    request.target = ExportTarget::SelectedParts;
    request.format = ExportFormat::Step;
    request.targetCount = 1;
    request.path = scratch.path;
    const auto refused = RunExport(request, nullptr);
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "EXP-013", "書き出せない");
}

KACHA_V2_TEST(export_service, 文字の形式と並びの形式を分けている)
{
    Require(ExportFormatIsText(ExportFormat::Svg), "SVGは文字");
    Require(ExportFormatIsText(ExportFormat::Dxf), "DXFは文字");
    Require(!ExportFormatIsText(ExportFormat::Stl), "STLは並び");
    Require(!ExportFormatIsText(ExportFormat::Pdf), "PDFは並び");
    Require(!ExportFormatIsText(ExportFormat::Kcd2), "kcd2は並び");
}

KACHA_V2_TEST_MAIN("export_service_tests")
