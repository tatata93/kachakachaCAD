// 操作の案内が、どのコマンドでもそろっていること(AT-UIX-002)。
#include "kachakacha/app/OperationGuide.h"
#include "kachakacha/base/TestHarness.h"

#include <string>

using kachakacha::v2::app::BuildCommandGuide;
using kachakacha::v2::app::BuildGuide;
using kachakacha::v2::app::BuildToolGuide;
using kachakacha::v2::app::CommandCatalog;
using kachakacha::v2::app::CommandDescriptor;
using kachakacha::v2::app::CommandMode;
using kachakacha::v2::app::ContainsPlaceholder;
using kachakacha::v2::app::FindCommand;
using kachakacha::v2::app::OperationGuide;
using kachakacha::v2::modeling::ToolPrompt;
using kachakacha::v2::test::Require;
using kachakacha::v2::test::RequireEqual;

namespace {

[[nodiscard]] ToolPrompt Prompt(int remaining, bool more, bool canFinish,
    const std::string& message)
{
    ToolPrompt prompt;
    prompt.remainingPoints = remaining;
    prompt.acceptsMorePoints = more;
    prompt.canFinish = canFinish;
    prompt.messageJa = message;
    return prompt;
}

} // namespace

KACHA_V2_TEST(guide, 全コマンドで6つそろう)
{
    for (const CommandDescriptor& command : CommandCatalog()) {
        const OperationGuide guide =
            BuildGuide(command, Prompt(2, false, false, "始点を置きます。"), 0, true);
        Require(guide.IsComplete(),
            std::string(command.id) + ": 案内が欠けている");
    }
}

KACHA_V2_TEST(guide, 使えないコマンドでも6つそろう)
{
    for (const CommandDescriptor& command : CommandCatalog()) {
        const OperationGuide guide = BuildGuide(command, ToolPrompt{}, 0, false);
        Require(guide.IsComplete(),
            std::string(command.id) + ": 使えないときも案内は要る");
    }
}

KACHA_V2_TEST(guide, 仮の文字が混ざらない)
{
    for (const CommandDescriptor& command : CommandCatalog()) {
        const OperationGuide guide =
            BuildGuide(command, Prompt(1, false, true, "終点を置きます。"), 3, true);
        for (const std::string& text : {guide.toolNameJa, guide.currentStepJa,
                 guide.nextStepJa, guide.confirmJa, guide.cancelJa}) {
            Require(!ContainsPlaceholder(text),
                std::string(command.id) + ": 仮の文字がある \"" + text + "\"");
        }
    }
}

KACHA_V2_TEST(guide, 仮の文字を見つけられる)
{
    Require(ContainsPlaceholder("TODO: あとで書く"), "TODO を見つける");
    Require(ContainsPlaceholder("未定です"), "未定 を見つける");
    Require(ContainsPlaceholder("点を置きます…"), "三点リーダを見つける");
    Require(!ContainsPlaceholder("始点を置きます。"), "ふつうの文は通す");
}

KACHA_V2_TEST(guide, 残りの点数が案内に出る)
{
    const CommandDescriptor* command = FindCommand("draw.arc");
    Require(command != nullptr, "円弧がある");
    const OperationGuide three =
        BuildToolGuide(*command, Prompt(3, false, false, "1点目を置きます。"), 0);
    Require(three.nextStepJa.find("3") != std::string::npos, "あと3点と言う");
    const OperationGuide one =
        BuildToolGuide(*command, Prompt(1, false, false, "3点目を置きます。"), 0);
    Require(one.nextStepJa.find("次の点で決まります") != std::string::npos,
        "次で決まると言う");
}

KACHA_V2_TEST(guide, 点をいくつでも置ける道具はそう言う)
{
    const CommandDescriptor* command = FindCommand("draw.polyline");
    Require(command != nullptr, "ポリラインがある");
    const OperationGuide guide =
        BuildToolGuide(*command, Prompt(0, true, true, "点を置きます。"), 0);
    Require(guide.nextStepJa.find("続けて") != std::string::npos, "続けて置くと言う");
    Require(guide.confirmJa.find("Enter") != std::string::npos, "決め方を言う");
}

KACHA_V2_TEST(guide, 取消のしかたが必ず書いてある)
{
    for (const CommandDescriptor& command : CommandCatalog()) {
        const OperationGuide guide =
            BuildGuide(command, Prompt(1, false, true, "点を置きます。"), 0, true);
        Require(!guide.cancelJa.empty(), std::string(command.id) + ": 取消が無い");
    }
}

KACHA_V2_TEST(guide, 選択数が出る)
{
    const CommandDescriptor* command = FindCommand("wire.trim");
    Require(command != nullptr, "トリムがある");
    const OperationGuide guide =
        BuildToolGuide(*command, Prompt(1, false, false, "切る線を選びます。"), 2);
    RequireEqual(std::to_string(guide.selectionCount), "2", "選択数");
    Require(guide.ToStatusLine().find("選択 2 件") != std::string::npos,
        "1行の中に選択数が出る");
}

KACHA_V2_TEST(guide, 使えないときは理由が現在の手順に出る)
{
    const CommandDescriptor* command = FindCommand("part.extrude");
    Require(command != nullptr, "押し出しがある");
    const OperationGuide guide = BuildCommandGuide(*command, 0, false);
    RequireEqual(guide.currentStepJa, std::string(command->predicateFailureJa),
        "使えない理由が出る");
}

KACHA_V2_TEST(guide, 窓を開くコマンドは決め方が窓になる)
{
    const CommandDescriptor* command = FindCommand("export.step");
    Require(command != nullptr, "STEPがある");
    Require(command->mode == CommandMode::Dialog, "窓を開く");
    const OperationGuide guide = BuildCommandGuide(*command, 1, true);
    Require(guide.confirmJa.find("OK") != std::string::npos, "窓のOKと言う");
}

KACHA_V2_TEST(guide, 1行にまとめられる)
{
    const CommandDescriptor* command = FindCommand("draw.line");
    Require(command != nullptr, "直線がある");
    const OperationGuide guide =
        BuildToolGuide(*command, Prompt(2, false, false, "始点を置きます。"), 0);
    const std::string line = guide.ToStatusLine();
    Require(line.find("直線") != std::string::npos, "道具の名前");
    Require(line.find("始点") != std::string::npos, "いまの手順");
    Require(line.find("次") != std::string::npos, "次の手順");
    Require(line.find("Esc") != std::string::npos, "取消");
}

KACHA_V2_TEST(guide, 同じ入力からは毎回同じ案内が出る)
{
    const CommandDescriptor* command = FindCommand("draw.circle");
    Require(command != nullptr, "円がある");
    const std::string reference =
        BuildToolGuide(*command, Prompt(2, false, false, "中心を置きます。"), 0)
            .ToStatusLine();
    for (int attempt = 0; attempt < 5; ++attempt) {
        RequireEqual(
            BuildToolGuide(*command, Prompt(2, false, false, "中心を置きます。"), 0)
                .ToStatusLine(),
            reference, "毎回同じ");
    }
}

KACHA_V2_TEST_MAIN("operation_guide_tests")
