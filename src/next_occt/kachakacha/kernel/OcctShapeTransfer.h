#pragma once
#include "kachakacha/base/Diagnostic.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include <string>

namespace kachakacha::v2::kernel {
// Disposable same-build IPC only. Persistent KCD/frozen-part BRep stays text.
// Version, exact byte count, corruption checksum and native validity are checked.
[[nodiscard]] base::Result<std::string> CaptureTransferShape(modeling::KernelShapeHandle shape);
[[nodiscard]] base::Result<modeling::KernelShapeHandle> RestoreTransferShape(const std::string& bytes);
}
