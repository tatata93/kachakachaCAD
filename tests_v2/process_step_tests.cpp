// モードごとの手順(ui-workflows §9 / §10 / §11)。
#include "kachakacha/app/ProcessSteps.h"
#include "kachakacha/base/TestHarness.h"

#include <set>
#include <string>

using kachakacha::v2::app::AllUiModes;
using kachakacha::v2::app::BuildExportSummary;
using kachakacha::v2::app::BuildProcessSteps;
using kachakacha::v2::app::CurrentStepNumber;
using kachakacha::v2::app::ProcessContext;
using kachakacha::v2::app::ProcessStep;
using kachakacha::v2::app::StepState;
using kachakacha::v2::app::StepStateNameJa;
using kachakacha::v2::app::UiMode;
using kachakacha::v2::base::Diagnostic;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;
using kachakacha::v2::test::RequireNear;

namespace {

[[nodiscard]] std::string FirstCode(const std::vector<Diagnostic>& diagnostics)
{
    return diagnostics.empty() ? std::string("(なし)") : diagnostics.front().code;
}

[[nodiscard]] const ProcessStep* Find(const std::vector<ProcessStep>& steps,
    std::string_view id)
{
    for (const ProcessStep& step : steps) {
        if (step.id == id) {
            return &step;
        }
    }
    return nullptr;
}

} // namespace

KACHA_V2_TEST(process, 4つのモードすべてに手順がある)
{
    for (UiMode mode : AllUiModes()) {
        const auto steps = BuildProcessSteps(mode, ProcessContext{});
        Require(!steps.empty(), "手順がある");
        for (const ProcessStep& step : steps) {
            Require(!step.id.empty(), "IDがある");
            Require(!step.titleJa.empty(), "題がある");
        }
    }
}

KACHA_V2_TEST(process, 番号は1から順に振られる)
{
    for (UiMode mode : AllUiModes()) {
        const auto steps = BuildProcessSteps(mode, ProcessContext{});
        for (std::size_t at = 0; at < steps.size(); ++at) {
            RequireEqual(std::to_string(steps[at].number), std::to_string(at + 1),
                "番号が順に振られる");
        }
    }
}

KACHA_V2_TEST(process, 段のIDはモードの中で重ならない)
{
    for (UiMode mode : AllUiModes()) {
        std::set<std::string> ids;
        for (const ProcessStep& step : BuildProcessSteps(mode, ProcessContext{})) {
            Require(ids.insert(step.id).second, "重ならない: " + step.id);
        }
    }
}

KACHA_V2_TEST(process, 製作の手順は契約どおり10段)
{
    const auto steps = BuildProcessSteps(UiMode::Fabrication, ProcessContext{});
    RequireEqual(std::to_string(steps.size()), "10", "10段");
    RequireEqual(steps[0].titleJa, "元部品と範囲", "1段目");
    RequireEqual(steps[9].titleJa, "現在状態を固定 / 出力へ", "10段目");
}

KACHA_V2_TEST(process, 部品の手順は契約どおり7段)
{
    const auto steps = BuildProcessSteps(UiMode::Part, ProcessContext{});
    RequireEqual(std::to_string(steps.size()), "7", "7段");
    RequireEqual(steps[0].titleJa, "入力輪郭", "1段目");
    RequireEqual(steps[6].titleJa, "確定", "7段目");
}

KACHA_V2_TEST(process, 進めない段には必ず理由がある)
{
    // 理由の無い灰色のボタンは、利用者にとって行き止まりである。
    for (UiMode mode : AllUiModes()) {
        for (const ProcessStep& step : BuildProcessSteps(mode, ProcessContext{})) {
            if (step.state == StepState::Done) {
                continue;
            }
            Require(!step.blockedReasonJa.empty(),
                std::string("理由がある: ") + step.id);
        }
    }
}

KACHA_V2_TEST(process, 前の段が済むまで先へ入れない)
{
    const auto steps = BuildProcessSteps(UiMode::Fabrication, ProcessContext{});
    // 何も選んでいないので1段目で止まる。
    RequireEqual(std::to_string(CurrentStepNumber(steps)), "1", "1段目");
    bool sawBlocked = false;
    for (const ProcessStep& step : steps) {
        if (step.number == 1) {
            Require(step.state != StepState::Blocked, "1段目には入れる");
            continue;
        }
        Require(step.state == StepState::Blocked, "先へは入れない");
        RequireEqual(step.blockedReasonJa, "前の段がまだ済んでいません。", "理由");
        sawBlocked = true;
    }
    Require(sawBlocked, "止まっている段がある");
}

KACHA_V2_TEST(process, 部品を選ぶと製作の手順が進む)
{
    ProcessContext context;
    context.selectedPartCount = 1;
    const auto steps = BuildProcessSteps(UiMode::Fabrication, context);
    RequireEqual(std::to_string(CurrentStepNumber(steps)), "7", "生成まで進む");
    Require(Find(steps, "fab.source")->state == StepState::Done, "元部品は済み");
    Require(Find(steps, "fab.generate")->state != StepState::Done, "生成はまだ");
    Require(Find(steps, "fab.generate")->blockedReasonJa.find("自動では")
            != std::string::npos,
        "自動では作り直さないと言う");
}

KACHA_V2_TEST(process, 作って型紙まで行くと固定まで進む)
{
    ProcessContext context;
    context.selectedPartCount = 2;
    context.fabricationBuilt = true;
    context.panelCount = 4;
    context.patternBuilt = true;
    const auto steps = BuildProcessSteps(UiMode::Fabrication, context);
    RequireEqual(std::to_string(CurrentStepNumber(steps)), "10", "10段目");
    Require(Find(steps, "fab.pattern")->state == StepState::Done, "型紙は済み");
    Require(Find(steps, "fab.freeze")->state != StepState::Blocked, "固定に入れる");
}

KACHA_V2_TEST(process, 押し出しは出すものを選ぶまで進まない)
{
    ProcessContext context;
    context.extrudeProfileCount = 2;
    const auto steps = BuildProcessSteps(UiMode::Part, context);
    Require(Find(steps, "part.profile")->state == StepState::Done, "輪郭は済み");
    Require(Find(steps, "part.output")->state != StepState::Done, "出力はまだ");
    RequireEqual(std::to_string(CurrentStepNumber(steps)), "4", "出力の段で止まる");

    context.extrudeHasOutput = true;
    const auto next = BuildProcessSteps(UiMode::Part, context);
    RequireEqual(std::to_string(CurrentStepNumber(next)), "7", "確定まで進む");
}

KACHA_V2_TEST(process, 出力は対象を選ぶまで進まない)
{
    const auto steps = BuildProcessSteps(UiMode::Output, ProcessContext{});
    RequireEqual(std::to_string(CurrentStepNumber(steps)), "1", "対象の段");
    Require(Find(steps, "out.target")->blockedReasonJa.find("表示・非表示")
            != std::string::npos,
        "表示だけで決めないと言う");

    ProcessContext context;
    context.exportTargetChosen = true;
    context.exportValidated = true;
    const auto ready = BuildProcessSteps(UiMode::Output, context);
    RequireEqual(std::to_string(CurrentStepNumber(ready)), "4", "書き出しの段");
}

KACHA_V2_TEST(process, 段の状態に4つとも名前がある)
{
    std::set<std::string> names;
    for (StepState state : {StepState::NotStarted, StepState::InProgress,
             StepState::Done, StepState::Blocked}) {
        const std::string name{StepStateNameJa(state)};
        Require(!name.empty() && name != "不明", "名前がある");
        Require(names.insert(name).second, "重ならない");
    }
}

KACHA_V2_TEST(process, 手順が台帳のコマンドを指している)
{
    // 段が指すコマンドは、台帳に載っているものでなければならない。
    for (UiMode mode : AllUiModes()) {
        for (const ProcessStep& step : BuildProcessSteps(mode, ProcessContext{})) {
            for (const std::string& id : step.commandIds) {
                Require(kachakacha::v2::app::FindCommand(id) != nullptr,
                    "台帳にある: " + id);
            }
        }
    }
}

KACHA_V2_TEST(process, 出力前の検査に契約どおりの行が出る)
{
    const auto summary = BuildExportSummary(3, 3, 8, 0.20, 30.0, 0);
    Require(summary.HasValue(), "作れる");
    RequireEqual(std::to_string(summary.Value().linesJa.size()), "6", "6行");
    RequireEqual(summary.Value().linesJa[0], "対象 3部品", "1行目");
    RequireEqual(summary.Value().linesJa[1], "閉じたソリッド 3/3", "2行目");
    RequireEqual(summary.Value().linesJa[2], "開口 8", "3行目");
    RequireEqual(summary.Value().linesJa[3], "最小肉厚 0.20 mm", "4行目");
    RequireEqual(summary.Value().linesJa[4], "現在の組立率 30.0%", "5行目");
    RequireEqual(summary.Value().linesJa[5], "警告 0", "6行目");
}

KACHA_V2_TEST(process, 対象0では検査を出さずに理由を言う)
{
    const auto refused = BuildExportSummary(0, 0, 0, 0.2, 0.0, 0);
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "EXP-015", "選ばれていない");
    Require(refused.Diagnostics().front().detailsJa.find("表示・非表示")
            != std::string::npos,
        "表示だけで決めないと言う");
}

KACHA_V2_TEST(process, 閉じた立体の数が対象より多ければ断る)
{
    const auto refused = BuildExportSummary(2, 3, 0, 0.2, 0.0, 0);
    Require(!refused.HasValue(), "断る");
    RequireEqual(FirstCode(refused.Diagnostics()), "EXP-015", "数が合わない");
}

KACHA_V2_TEST(process, 同じ状況なら同じ手順が出る)
{
    ProcessContext context;
    context.selectedPartCount = 1;
    context.fabricationBuilt = true;
    for (UiMode mode : AllUiModes()) {
        const auto once = BuildProcessSteps(mode, context);
        const auto again = BuildProcessSteps(mode, context);
        RequireEqual(std::to_string(once.size()), std::to_string(again.size()), "同じ数");
        for (std::size_t at = 0; at < once.size(); ++at) {
            RequireEqual(once[at].id, again[at].id, "同じ並び");
            Require(once[at].state == again[at].state, "同じ状態");
        }
    }
}

KACHA_V2_TEST_MAIN("process_step_tests")
