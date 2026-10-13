#pragma once

#include "kachakacha/kernel/OcctBoolean.h"
#include "kachakacha/kernel/OcctThicken.h"
#include "kachakacha/modeling/WorkPlane.h"
#include <optional>
#include <functional>
class QWidget;
class QString;

namespace V2GeometryJob {
bool Active();
void CancelPreview(QWidget* parent);
using DocumentReader = std::function<kachakacha::v2::base::Result<bool>(
    const QString&, const QString&, const std::function<bool()>&)>;
kachakacha::v2::base::Result<bool> PrepareDocument(QWidget* parent, const QString& path, const DocumentReader& reader);
int Worker(const QString& directory);
kachakacha::v2::base::Result<kachakacha::v2::kernel::ThickenedSolid> Thicken(
    QWidget* parent, kachakacha::v2::modeling::KernelShapeHandle source,
    double thickness, kachakacha::v2::fabrication::ThicknessPlacement placement,
    const std::optional<kachakacha::v2::modeling::WorkPlaneFrame>& plane,
    const kachakacha::v2::geometry::GeometryTolerance& tolerance, bool interactivePreview = false, bool correctOpposite = false);
kachakacha::v2::base::Result<kachakacha::v2::kernel::BooleanBuildResult> Boolean(
    QWidget* parent, kachakacha::v2::kernel::BooleanOperation operation,
    kachakacha::v2::modeling::KernelShapeHandle first,
    const std::vector<kachakacha::v2::modeling::KernelShapeHandle>& tools, double tolerance);
}
