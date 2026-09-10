#include "kachakacha/app/ProcessSteps.h"

#include <cstdio>

namespace kachakacha::v2::app {
namespace {

using base::MakeError;
using base::Result;

//! 段を1つ足す。番号は呼ぶ順に振られる。
struct StepBuilder {
    std::vector<ProcessStep> steps;
    bool blockedFromHere = false;

    void Add(std::string id, std::string titleJa, bool done, bool started,
        std::string needsJa, std::vector<std::string> commandIds = {})
    {
        ProcessStep step;
        step.id = std::move(id);
        step.number = static_cast<int>(steps.size()) + 1;
        step.titleJa = std::move(titleJa);
        step.commandIds = std::move(commandIds);
        if (blockedFromHere) {
            step.state = StepState::Blocked;
            step.blockedReasonJa = "前の段がまだ済んでいません。";
        } else if (done) {
            step.state = StepState::Done;
        } else if (started) {
            step.state = StepState::InProgress;
            step.blockedReasonJa = std::move(needsJa);
        } else {
            step.state = StepState::NotStarted;
            step.blockedReasonJa = std::move(needsJa);
        }
        if (!done) {
            // ここから先は、この段が済むまで入れない。
            blockedFromHere = true;
        }
        steps.push_back(std::move(step));
    }
};

[[nodiscard]] std::vector<ProcessStep> DrawingSteps(const ProcessContext& context)
{
    StepBuilder builder;
    builder.Add("draw.plane", "作業平面を決める", true, true, {},
        {"workplane.create", "workplane.set_active"});
    builder.Add("draw.curves", "線を描く", context.selectedWireCount > 0
            || context.guideRowCount > 0,
        true, "線を1本以上描いてください。",
        {"draw.line", "draw.arc", "draw.circle", "draw.bezier", "draw.spline"});
    builder.Add("draw.edit", "線を整える", true, true, {},
        {"wire.trim", "wire.extend", "wire.fillet", "wire.chamfer",
            "wire.offset", "wire.meet"});
    return builder.steps;
}

[[nodiscard]] std::vector<ProcessStep> PartSteps(const ProcessContext& context)
{
    StepBuilder builder;
    builder.Add("part.profile", "入力輪郭", context.extrudeProfileCount > 0, false,
        "押し出す輪郭を選んでください。", {"selection.activate"});
    builder.Add("part.direction", "方向", context.extrudeProfileCount > 0, false,
        "輪郭を選ぶと向きが決まります。");
    builder.Add("part.extent", "終端", context.extrudeProfileCount > 0, false,
        "距離か、届かせる相手を決めてください。");
    builder.Add("part.output", "出力", context.extrudeHasOutput, false,
        "ワイヤー・側面・部品のどれを出すかを選んでください。");
    builder.Add("part.boolean", "部品演算", context.extrudeHasOutput, false,
        "新規・足す・引くのどれかを選んでください。");
    builder.Add("part.preview", "結果プレビューと検査", context.extrudeHasOutput, false,
        "出すものを選ぶと検査できます。");
    builder.Add("part.commit", "確定", false, false, "検査を通してから確定できます。",
        {"part.extrude", "part.from_wire_cage"});
    return builder.steps;
}

[[nodiscard]] std::vector<ProcessStep> FabricationSteps(const ProcessContext& context)
{
    StepBuilder builder;
    builder.Add("fab.source", "元部品と範囲", context.selectedPartCount > 0, false,
        "元になる部品を1つ以上選んでください。近くの部品を自動では選びません。");
    builder.Add("fab.strategy", "作り方", context.selectedPartCount > 0, false,
        "1枚・少数・完全分割・混合から選んでください。");
    builder.Add("fab.tolerance", "再現度と偏差", context.selectedPartCount > 0, false,
        "許せる偏差を決めてください。");
    builder.Add("fab.bend", "曲げ方向", context.selectedPartCount > 0, false,
        "自動・U・V・両方から選んでください。");
    builder.Add("fab.relief", "切れ目", context.selectedPartCount > 0, false,
        "切れ目を入れるかどうかを決めてください。");
    builder.Add("fab.manual", "手動境界", context.selectedPartCount > 0, false,
        "必要なら境界に役割を付けてください。");
    builder.Add("fab.generate", "生成", context.fabricationBuilt, false,
        "「プレビュー更新」で作ってください。設定を変えても自動では作り直しません。",
        {"fabrication.create", "fabrication.preview_update"});
    builder.Add("fab.panels", "部材一覧と誤差", context.panelCount > 0, false,
        "生成すると部材と誤差が出ます。");
    builder.Add("fab.pattern", "型紙 / 組立確認", context.patternBuilt, false,
        "型紙を作ってください。", {"fabrication.create_pattern"});
    builder.Add("fab.freeze", "現在状態を固定 / 出力へ", false, false,
        "型紙まで出来たら固定できます。",
        {"fabrication.freeze_state", "derived.freeze"});
    return builder.steps;
}

[[nodiscard]] std::vector<ProcessStep> OutputSteps(const ProcessContext& context)
{
    StepBuilder builder;
    builder.Add("out.target", "対象", context.exportTargetChosen, false,
        "出すものを選んでください。表示・非表示だけでは決めません。");
    builder.Add("out.format", "形式", context.exportTargetChosen, false,
        "対象を選ぶと出せる形式が決まります。");
    builder.Add("out.validate", "検査", context.exportValidated, false,
        "出力の前に検査してください。", {"export.validate"});
    builder.Add("out.write", "書き出し", false, false, "検査を通してから書き出せます。",
        {"export.stl", "export.step", "export.svg", "export.dxf"});
    return builder.steps;
}

} // namespace

std::string_view StepStateNameJa(StepState state) noexcept
{
    switch (state) {
    case StepState::NotStarted: return "まだ";
    case StepState::InProgress: return "途中";
    case StepState::Done:       return "済み";
    case StepState::Blocked:    return "入れません";
    }
    return "不明";
}

std::vector<ProcessStep> BuildProcessSteps(UiMode mode, const ProcessContext& context)
{
    switch (mode) {
    case UiMode::Drawing:     return DrawingSteps(context);
    case UiMode::Part:        return PartSteps(context);
    case UiMode::Fabrication: return FabricationSteps(context);
    case UiMode::Output:      return OutputSteps(context);
    }
    return {};
}

int CurrentStepNumber(const std::vector<ProcessStep>& steps)
{
    for (const ProcessStep& step : steps) {
        if (step.state != StepState::Done) {
            return step.number;
        }
    }
    return 0;
}

Result<ExportSummary> BuildExportSummary(int targetCount, int closedSolidCount,
    int openingCount, double minimumThicknessMm, double assemblyPercent,
    int warningCount)
{
    using Out = Result<ExportSummary>;
    if (targetCount <= 0) {
        // 「選択0なら実行不可で理由を表示する」(§11.1)。
        return Out::Failure(MakeError("EXP-015", "出すものが選ばれていません。",
            "対象を1つ以上選んでください。表示・非表示だけでは決めません。"));
    }
    if (closedSolidCount < 0 || closedSolidCount > targetCount) {
        return Out::Failure(MakeError("EXP-015", "出すものが選ばれていません。",
            "閉じた立体の数が対象の数と合いません。"));
    }
    ExportSummary summary;
    summary.targetCount = targetCount;
    summary.closedSolidCount = closedSolidCount;
    summary.openingCount = openingCount;
    summary.minimumThicknessMm = minimumThicknessMm;
    summary.assemblyPercent = assemblyPercent;
    summary.warningCount = warningCount;
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "対象 %d部品", targetCount);
    summary.linesJa.emplace_back(buffer);
    std::snprintf(buffer, sizeof(buffer), "閉じたソリッド %d/%d", closedSolidCount,
        targetCount);
    summary.linesJa.emplace_back(buffer);
    std::snprintf(buffer, sizeof(buffer), "開口 %d", openingCount);
    summary.linesJa.emplace_back(buffer);
    std::snprintf(buffer, sizeof(buffer), "最小肉厚 %.2f mm", minimumThicknessMm);
    summary.linesJa.emplace_back(buffer);
    std::snprintf(buffer, sizeof(buffer), "現在の組立率 %.1f%%", assemblyPercent);
    summary.linesJa.emplace_back(buffer);
    std::snprintf(buffer, sizeof(buffer), "警告 %d", warningCount);
    summary.linesJa.emplace_back(buffer);
    return Out::Success(std::move(summary));
}

} // namespace kachakacha::v2::app
