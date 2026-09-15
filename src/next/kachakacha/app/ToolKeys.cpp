#include "kachakacha/app/ToolKeys.h"

namespace kachakacha::v2::app {

ToolKeyAction ActionForConfirmKey(const ToolKeyContext& context) noexcept
{
    if (!context.previewActive) {
        return ToolKeyAction::Ignore;
    }
    if (context.inTypingField && context.valueChanged) {
        return ToolKeyAction::CommitValueAndWait;
    }
    return ToolKeyAction::Confirm;
}

ToolKeyAction ActionForCancelKey(const ToolKeyContext& context) noexcept
{
    return context.previewActive ? ToolKeyAction::Cancel : ToolKeyAction::Ignore;
}

} // namespace kachakacha::v2::app
