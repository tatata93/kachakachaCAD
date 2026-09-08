#include "kachakacha/app/ExportService.h"

#include <algorithm>

namespace kachakacha::v2::app {
namespace {

using base::MakeError;
using base::Result;

//! 対象と形式の組合せ(§11.2)。ここが唯一の表である。
struct AllowedPair {
    ExportTarget target;
    ExportFormat format;
};

const AllowedPair kAllowed[] = {
    {ExportTarget::VisibleParts, ExportFormat::Stl},
    {ExportTarget::VisibleParts, ExportFormat::Step},
    {ExportTarget::SelectedParts, ExportFormat::Stl},
    {ExportTarget::SelectedParts, ExportFormat::Step},
    {ExportTarget::SelectedFabricationPanels, ExportFormat::Stl},
    {ExportTarget::SelectedFabricationPanels, ExportFormat::Step},
    {ExportTarget::SelectedFabricationPanels, ExportFormat::Svg},
    {ExportTarget::SelectedFabricationPanels, ExportFormat::Dxf},
    {ExportTarget::SelectedFabricationPanels, ExportFormat::Pdf},
    {ExportTarget::CurrentPattern, ExportFormat::Svg},
    {ExportTarget::CurrentPattern, ExportFormat::Dxf},
    {ExportTarget::CurrentPattern, ExportFormat::Pdf},
    {ExportTarget::SelectedWires, ExportFormat::Svg},
    {ExportTarget::SelectedWires, ExportFormat::Dxf},
    {ExportTarget::SelectedWires, ExportFormat::Pdf},
    {ExportTarget::Project, ExportFormat::Kcd2},
};

//! 形式の並び。どの対象でもこの順で出す。毎回同じ順になる。
const ExportFormat kFormatOrder[] = {ExportFormat::Stl, ExportFormat::Step,
    ExportFormat::Svg, ExportFormat::Dxf, ExportFormat::Pdf, ExportFormat::Kcd2};

[[nodiscard]] bool EndsWith(const std::string& text, std::string_view suffix)
{
    if (text.size() < suffix.size()) {
        return false;
    }
    std::string tail = text.substr(text.size() - suffix.size());
    std::transform(tail.begin(), tail.end(), tail.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return tail == suffix;
}

} // namespace

std::string_view ExportTargetNameJa(ExportTarget target) noexcept
{
    switch (target) {
    case ExportTarget::VisibleParts:              return "表示している部品";
    case ExportTarget::SelectedParts:             return "選んだ部品";
    case ExportTarget::SelectedFabricationPanels: return "選んだ製作部材";
    case ExportTarget::CurrentPattern:            return "いまの型紙";
    case ExportTarget::SelectedWires:             return "選んだワイヤー";
    case ExportTarget::Project:                   return "この文書";
    }
    return "不明";
}

std::string_view ExportFormatNameJa(ExportFormat format) noexcept
{
    switch (format) {
    case ExportFormat::Stl:  return "STL";
    case ExportFormat::Step: return "STEP";
    case ExportFormat::Svg:  return "SVG";
    case ExportFormat::Dxf:  return "DXF";
    case ExportFormat::Pdf:  return "1:1 PDF";
    case ExportFormat::Kcd2: return "kcd2";
    }
    return "不明";
}

std::string_view ExportFormatExtension(ExportFormat format) noexcept
{
    switch (format) {
    case ExportFormat::Stl:  return ".stl";
    case ExportFormat::Step: return ".step";
    case ExportFormat::Svg:  return ".svg";
    case ExportFormat::Dxf:  return ".dxf";
    case ExportFormat::Pdf:  return ".pdf";
    case ExportFormat::Kcd2: return ".kcd2";
    }
    return "";
}

bool ExportFormatIsText(ExportFormat format) noexcept
{
    return format == ExportFormat::Svg || format == ExportFormat::Dxf;
}

bool ExportFormatAllowed(ExportTarget target, ExportFormat format) noexcept
{
    for (const AllowedPair& pair : kAllowed) {
        if (pair.target == target && pair.format == format) {
            return true;
        }
    }
    return false;
}

std::vector<ExportFormat> AllowedFormatsFor(ExportTarget target)
{
    std::vector<ExportFormat> formats;
    for (ExportFormat format : kFormatOrder) {
        if (ExportFormatAllowed(target, format)) {
            formats.push_back(format);
        }
    }
    return formats;
}

Result<std::string> ResolveExportPath(const std::string& path, ExportFormat format)
{
    if (path.empty()) {
        return Result<std::string>::Failure(MakeError("EXP-016",
            "書き出す先が決まっていません。", "ファイル名を決めてください。"));
    }
    const std::string_view extension = ExportFormatExtension(format);
    if (extension.empty()) {
        return Result<std::string>::Failure(MakeError("EXP-016",
            "書き出す先が決まっていません。", "形式に対応する拡張子がありません。"));
    }
    if (EndsWith(path, extension)) {
        return Result<std::string>::Success(path);
    }
    // .stp や .stl を .step へ勝手に直さない。無ければ足すだけにする。
    return Result<std::string>::Success(path + std::string(extension));
}

Result<ExportOutcome> RunExport(const ExportRequest& request,
    const ExportContentMaker& makeContent)
{
    using Out = Result<ExportOutcome>;
    if (request.targetCount <= 0) {
        return Out::Failure(MakeError("EXP-015", "出すものが選ばれていません。",
            std::string(ExportTargetNameJa(request.target))
                + " が1つも選ばれていません。表示・非表示だけでは決めません。"));
    }
    if (!ExportFormatAllowed(request.target, request.format)) {
        return Out::Failure(MakeError("EXP-017",
            "その形式では出せません。",
            std::string(ExportTargetNameJa(request.target)) + " を "
                + std::string(ExportFormatNameJa(request.format))
                + " で出すことはできません。"));
    }
    const auto path = ResolveExportPath(request.path, request.format);
    if (!path.HasValue()) {
        return Out::Failure(path.Diagnostics());
    }
    if (!request.overwrite && io::PathExists(path.Value())) {
        return Out::Failure(MakeError("EXP-018",
            "同じ名前のファイルが既にあります。",
            path.Value() + " を上書きしてよいか確かめてから、もう一度どうぞ。"));
    }
    if (!makeContent) {
        return Out::Failure(MakeError("EXP-013", "書き出せませんでした。",
            "中身を作る手立てが渡されていません。"));
    }
    // 検査をすべて通ってから、はじめて中身を作らせる。
    // 先に作らせると、落ちたときに無駄な計算が残る。
    const auto content = makeContent();
    if (!content.HasValue()) {
        return Out::Failure(content.Diagnostics());
    }
    if (content.Value().empty()) {
        // 0バイトのファイルを残さない。
        return Out::Failure(MakeError("EXP-013", "書き出せませんでした。",
            "中身が空でした。ファイルは作っていません。"));
    }
    const auto written = io::WriteFileAtomically(path.Value(), content.Value());
    if (!written.HasValue()) {
        return Out::Failure(written.Diagnostics());
    }
    ExportOutcome outcome;
    outcome.path = path.Value();
    outcome.byteCount = content.Value().size();
    outcome.keptBackup = !written.Value().backupPath.empty();
    return Out::Success(std::move(outcome));
}

} // namespace kachakacha::v2::app
