#include "kachakacha/app/ExportPanel.h"

#include "kachakacha/io/AtomicFile.h"

#include <string>

namespace kachakacha::v2::app {
namespace {

using base::MakeError;
using base::Result;

//! 対象の並び。画面はいつもこの順で出す。
const ExportTarget kTargetOrder[] = {
    ExportTarget::VisibleParts,
    ExportTarget::SelectedParts,
    ExportTarget::SelectedFabricationPanels,
    ExportTarget::CurrentPattern,
    ExportTarget::SelectedWires,
    ExportTarget::Project,
};

[[nodiscard]] std::string Number(int value)
{
    return std::to_string(value);
}

} // namespace

ExportCounts ExportCountsFrom(const ProcessContext& context, int visiblePartCount,
    bool hasDocument) noexcept
{
    ExportCounts counts;
    counts.visibleParts = visiblePartCount > 0 ? visiblePartCount : 0;
    counts.selectedParts = context.selectedPartCount > 0 ? context.selectedPartCount : 0;
    // 部材は製作モデルが出来ていないと出せない。数だけあっても中身が無い。
    counts.selectedFabricationPanels =
        (context.fabricationBuilt && context.panelCount > 0) ? context.panelCount : 0;
    counts.patternPages = context.patternBuilt ? 1 : 0;
    counts.selectedWires = context.selectedWireCount > 0 ? context.selectedWireCount : 0;
    counts.project = hasDocument ? 1 : 0;
    return counts;
}

int ExportCountFor(const ExportCounts& counts, ExportTarget target) noexcept
{
    switch (target) {
    case ExportTarget::VisibleParts:              return counts.visibleParts;
    case ExportTarget::SelectedParts:             return counts.selectedParts;
    case ExportTarget::SelectedFabricationPanels: return counts.selectedFabricationPanels;
    case ExportTarget::CurrentPattern:            return counts.patternPages;
    case ExportTarget::SelectedWires:             return counts.selectedWires;
    case ExportTarget::Project:                   return counts.project;
    }
    return 0;
}

std::vector<ExportTargetRow> BuildExportTargetRows(const ExportCounts& counts)
{
    std::vector<ExportTargetRow> rows;
    rows.reserve(std::size(kTargetOrder));
    for (ExportTarget target : kTargetOrder) {
        ExportTargetRow row;
        row.target = target;
        row.count = ExportCountFor(counts, target);
        row.selectable = row.count > 0;
        row.labelJa = std::string(ExportTargetNameJa(target)) + "(" + Number(row.count) + ")";
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<ExportFormatRow> BuildExportFormatRows(ExportTarget target)
{
    const ExportFormat kAll[] = {ExportFormat::Stl, ExportFormat::Step, ExportFormat::Svg,
        ExportFormat::Dxf, ExportFormat::Pdf, ExportFormat::Kcd2};
    std::vector<ExportFormatRow> rows;
    rows.reserve(std::size(kAll));
    for (ExportFormat format : kAll) {
        ExportFormatRow row;
        row.format = format;
        row.selectable = ExportFormatAllowed(target, format);
        row.labelJa = std::string(ExportFormatNameJa(format));
        rows.push_back(std::move(row));
    }
    return rows;
}

ExportPanelState BeginExportPanel(const ExportCounts& counts)
{
    ExportPanelState state;
    state.target = kTargetOrder[0];
    for (ExportTarget target : kTargetOrder) {
        if (ExportCountFor(counts, target) > 0) {
            state.target = target;
            break;
        }
    }
    const std::vector<ExportFormat> formats = AllowedFormatsFor(state.target);
    if (!formats.empty()) {
        state.format = formats.front();
    }
    return state;
}

ExportPanelState RetargetForCounts(const ExportPanelState& state,
    const ExportCounts& counts)
{
    // 自分で選んだ対象が、いまも出せるなら動かさない。勝手に移すと選び直しになる。
    if (state.targetChosenByUser && ExportCountFor(counts, state.target) > 0) {
        return state;
    }
    // そうでなければ、いま出せるものへ移す。
    // 出力先と上書きの承諾は対象と関係ないので、決めたものを消さない。
    ExportPanelState next = BeginExportPanel(counts);
    next.path = state.path;
    next.overwrite = state.overwrite;
    return next;
}

Result<ExportPanelState> SetExportPanelTarget(const ExportPanelState& state,
    ExportTarget target, const ExportCounts& counts)
{
    using Out = Result<ExportPanelState>;
    if (ExportCountFor(counts, target) <= 0) {
        return Out::Failure(MakeError("EXP-015", "出すものが選ばれていません。",
            std::string(ExportTargetNameJa(target))
                + " が1つも選ばれていません。先に選んでから、もう一度どうぞ。"));
    }
    ExportPanelState next = state;
    next.target = target;
    next.targetChosenByUser = true;
    if (!ExportFormatAllowed(target, next.format)) {
        const std::vector<ExportFormat> formats = AllowedFormatsFor(target);
        if (formats.empty()) {
            return Out::Failure(MakeError("EXP-017", "その形式では出せません。",
                std::string(ExportTargetNameJa(target)) + " を出せる形式がありません。"));
        }
        next.format = formats.front();
    }
    return Out::Success(next);
}

Result<ExportPanelState> SetExportPanelFormat(const ExportPanelState& state,
    ExportFormat format)
{
    using Out = Result<ExportPanelState>;
    if (!ExportFormatAllowed(state.target, format)) {
        return Out::Failure(MakeError("EXP-017", "その形式では出せません。",
            std::string(ExportTargetNameJa(state.target)) + " を "
                + std::string(ExportFormatNameJa(format)) + " で出すことはできません。"));
    }
    ExportPanelState next = state;
    next.format = format;
    return Out::Success(next);
}

ExportPanelState SetExportPanelPath(const ExportPanelState& state, const std::string& path)
{
    ExportPanelState next = state;
    next.path = path;
    // 出力先を変えたら、上書きの承諾は取り消す。別のファイルへの承諾ではないためである。
    next.overwrite = false;
    return next;
}

ExportPanelState SetExportPanelOverwrite(const ExportPanelState& state, bool overwrite)
{
    ExportPanelState next = state;
    next.overwrite = overwrite;
    return next;
}

Result<ExportRequest> ToExportRequest(const ExportPanelState& state,
    const ExportCounts& counts)
{
    using Out = Result<ExportRequest>;
    const int count = ExportCountFor(counts, state.target);
    if (count <= 0) {
        return Out::Failure(MakeError("EXP-015", "出すものが選ばれていません。",
            std::string(ExportTargetNameJa(state.target))
                + " が1つも選ばれていません。表示・非表示だけでは決めません。"));
    }
    if (!ExportFormatAllowed(state.target, state.format)) {
        return Out::Failure(MakeError("EXP-017", "その形式では出せません。",
            std::string(ExportTargetNameJa(state.target)) + " を "
                + std::string(ExportFormatNameJa(state.format))
                + " で出すことはできません。"));
    }
    const auto path = ResolveExportPath(state.path, state.format);
    if (!path.HasValue()) {
        return Out::Failure(path.Diagnostics());
    }
    if (!state.overwrite && io::PathExists(path.Value())) {
        return Out::Failure(MakeError("EXP-018", "同じ名前のファイルが既にあります。",
            path.Value() + " を上書きしてよいか確かめてから、もう一度どうぞ。"));
    }
    ExportRequest request;
    request.target = state.target;
    request.format = state.format;
    request.targetCount = count;
    request.path = path.Value();
    request.overwrite = state.overwrite;
    return Out::Success(request);
}

bool CanRunExport(const ExportPanelState& state, const ExportCounts& counts)
{
    return ToExportRequest(state, counts).HasValue();
}

std::string ExportBlockReasonJa(const ExportPanelState& state, const ExportCounts& counts)
{
    const auto request = ToExportRequest(state, counts);
    if (request.HasValue()) {
        return {};
    }
    for (const base::Diagnostic& diagnostic : request.Diagnostics()) {
        if (diagnostic.IsError()) {
            return diagnostic.code + " " + diagnostic.summaryJa;
        }
    }
    return {};
}

std::string ExportOutcomeTextJa(const ExportOutcome& outcome)
{
    std::string text = outcome.path + " へ書き出しました("
        + std::to_string(outcome.byteCount) + " バイト)。";
    if (outcome.keptBackup) {
        text += "元のファイルは控えとして残しています。";
    }
    return text;
}

std::string ExportSelectionTextJa(const ExportPanelState& state, const ExportCounts& counts)
{
    return std::string(ExportTargetNameJa(state.target)) + " "
        + Number(ExportCountFor(counts, state.target)) + " 件を "
        + std::string(ExportFormatNameJa(state.format)) + " で";
}

} // namespace kachakacha::v2::app
