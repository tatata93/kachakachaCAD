#include "V2MainWindow.h"
#include "V2ToolBindings.h"
#include "kachakacha/app/OperationGuide.h"
#include <QString>
using namespace kachakacha::v2::app;
using kachakacha::v2::modeling::DrawingTool;
using v2ui::ToolBinding;
using v2ui::kToolBindings;

void V2MainWindow::RefreshGuide()
{
    const DrawingTool tool = session_->CurrentTool();
    std::string_view commandId = "selection.activate";
    for (const ToolBinding& binding : kToolBindings) {
        if (binding.tool == tool) {
            commandId = binding.commandId;
            break;
        }
    }
    const CommandDescriptor* command = FindCommand(commandId);
    if (command == nullptr) {
        return;
    }
    // いまの進み具合はツールが持っている。画面で数え直さない。
    // 案内は入力段階だけを読む。画面中央の仮の吸着・下見は計算しない。
    auto prompt = session_->Prompt();
    prompt.canFinish = true;
    const auto guide = kachakacha::v2::app::BuildGuide(*command, prompt,
        selectionCount_, true);
    SetStatus(QString::fromUtf8(guide.ToStatusLine().c_str()));
}

