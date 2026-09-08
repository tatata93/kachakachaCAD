#pragma once

//! 操作の案内(AT-UIX-002)。
//!
//! どのコマンドでも、画面には次の6つがそろっていなければならない。
//!   道具の名前 / いまの手順 / 次の手順 / 確定のしかた / 取消のしかた / 選択数
//!
//! 空欄や「未定」のような仮の文字を出してはならない。
//! V1 は道具によって案内が出たり出なかったりで、
//! 「次に何をすればいいのか」が分からない場面があった。
//!
//! 文言は core が作る。画面はそれを並べるだけにする。

#include "kachakacha/app/CommandCatalog.h"
#include "kachakacha/modeling/ToolController.h"

#include <string>

namespace kachakacha::v2::app {

struct OperationGuide {
    std::string toolNameJa;
    std::string currentStepJa;
    std::string nextStepJa;
    std::string confirmJa;
    std::string cancelJa;
    int selectionCount = 0;

    //! 6つがそろっているか。1つでも空なら false。
    [[nodiscard]] bool IsComplete() const noexcept
    {
        return !toolNameJa.empty() && !currentStepJa.empty() && !nextStepJa.empty()
            && !confirmJa.empty() && !cancelJa.empty() && selectionCount >= 0;
    }

    //! 画面へ出す1行。
    [[nodiscard]] std::string ToStatusLine() const;
};

//! 道具に入っているコマンドの案内。進み具合はツールの状態から取る。
[[nodiscard]] OperationGuide BuildToolGuide(const CommandDescriptor& command,
    const modeling::ToolPrompt& prompt, int selectionCount);

//! 道具に入らないコマンド(すぐ効く・窓を開く)の案内。
[[nodiscard]] OperationGuide BuildCommandGuide(const CommandDescriptor& command,
    int selectionCount, bool enabled);

//! そのコマンドの案内を作る。道具かどうかで作り分ける。
[[nodiscard]] OperationGuide BuildGuide(const CommandDescriptor& command,
    const modeling::ToolPrompt& prompt, int selectionCount, bool enabled);

//! 仮の文字が混ざっていないか。試験で使う。
[[nodiscard]] bool ContainsPlaceholder(const std::string& text);

} // namespace kachakacha::v2::app
