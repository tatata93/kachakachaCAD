#pragma once
#include "kachakacha/app/BooleanInputState.h"
#include "kachakacha/modeling/ShapeMesh.h"
#include "kachakacha/modeling/GuideSurfaceResult.h"
#include <QString>
#include <functional>
#include <vector>
class QWidget;
class V2Viewport;
class V2OperationPanelHost;
class QPointF;
struct OverlapSource {
    kachakacha::v2::base::EntityId id;
    QString name;
    kachakacha::v2::modeling::KernelShapeHandle handle;
    kachakacha::v2::modeling::ShapeMesh mesh;
    bool hidden = false;
};
using OverlapAction = std::function<void(const OverlapSource&, const OverlapSource&, int,
    kachakacha::v2::app::BooleanKind, const std::vector<int>&)>;
void OpenOverlapBrowser(V2Viewport& viewport, V2OperationPanelHost& host, std::vector<OverlapSource> sources, double tolerance,
    QString missing, OverlapAction action);
void InstallContactPicker(QWidget& viewport, std::function<bool(const QPointF&)> pick);
